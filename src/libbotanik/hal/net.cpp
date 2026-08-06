// REVIEW_35: TODO
/**
 * @file net.cpp
 * @brief HAL::Network backends. OPTA wraps Mbed sockets; the simulator uses real BSD
 *        sockets but resolves every name to loopback, so it only ever reaches the
 *        in-process mock responder (botanik_sim/network.h) - never the public
 *        network. The transport here is protocol-agnostic on both targets;
 *        NTP/MQTT wire knowledge lives above it. The TARGET_OPTA /
 *        TARGET_SIMULATOR split is legal here: this file is part of the HAL.
 */

#include "libbotanik/hal/net.h"       // HAL::Network - the link/DNS/UDP/TCP surface implemented here

#include "libbotanik_config.h"        // Config::NET_* - socket counts, timeouts, mock ports

#include "libbotanik/hal/platform.h"  // HAL::Clock::uptime_ms - DNS/connect deadlines

#ifdef TARGET_OPTA
// ---- OPTA backend (Mbed) ----

#include <cstring>                    // memcpy / memset / strlen - address and buffer handling

// Mbed netsocket - resolved from the installed mbed_opta core, not this tree.
#include "netsocket/EthernetInterface.h" // EthernetInterface - the wired link device
#include "netsocket/NetworkInterface.h" // NetworkInterface - gethostbyname_async, status callbacks
#include "netsocket/SocketAddress.h"  // SocketAddress - what DNS resolves into
#include "netsocket/TCPSocket.h"      // TCPSocket - the MQTT connection
#include "netsocket/UDPSocket.h"      // UDPSocket - the NTP exchange

namespace Botanik { namespace HAL { namespace Network {

    namespace {
        // The single wired interface, built lazily (not during static init).
        NetworkInterface* eth() { static EthernetInterface e; return &e; }

        // Fixed socket tables: small integer ids map to Mbed socket objects.
        const int MAX_UDP = Config::NET_MAX_UDP;
        const int MAX_TCP = Config::NET_MAX_TCP;
        UDPSocket g_udp[MAX_UDP]; bool g_udp_used[MAX_UDP] = {};
        TCPSocket g_tcp[MAX_TCP]; bool g_tcp_used[MAX_TCP] = {};
        // An ACCEPTED connection is not one of ours: Mbed's TCPSocket::accept()
        // hands back a socket it allocated, and it has to be deleted rather than
        // reused. So a slot is either the embedded object above or that pointer,
        // and sock() is what every call goes through.
        TCPSocket* g_tcp_ptr[MAX_TCP] = {};
        TCPSocket* sock(TcpSocket s) {
            return (g_tcp_ptr[s] != 0) ? g_tcp_ptr[s] : &g_tcp[s];
        }
        bool tcp_valid(TcpSocket s) { return s >= 0 && s < MAX_TCP && g_tcp_used[s]; }
        // Claim a free slot; -1 when the budget is spent.
        int tcp_slot() {
            for (int i = 0; i < MAX_TCP; ++i) if (!g_tcp_used[i]) return i;
            return -1;
        }

        // DNS slot table: handle -> {state, addr}. Async callback publishes the
        // state.
        // st: 0 free, 1 pending, 2 ok, 3 failed
        const int MAX_DNS = Config::NET_MAX_DNS;
        static_assert(MAX_DNS >= (int)Config::DNS_CACHE_SIZE,
                      "NET_MAX_DNS below DNS_CACHE_SIZE: NameResolver can leave "
                      "every row PENDING, and the surplus lookups would fail here");
        struct DnsSlot { volatile uint8_t st; Ip4 addr; int mbed_handle; DnsHandle id; };
        DnsSlot g_dns[MAX_DNS];
        DnsHandle g_dns_next = 1;
        void on_dns(DnsSlot* slot, int result, SocketAddress* a) {
            if (result > 0 && a != 0) {
                nsapi_addr_t na = a->get_addr();
                memcpy(slot->addr.b, na.bytes, 4);
                HAL::Memory::dsb(); slot->st = 2;
            } else { HAL::Memory::dsb(); slot->st = 3; }
        }

        SocketAddress make_addr(const Ip4& a, uint16_t port) {
            SocketAddress s; s.set_ip_bytes(a.b, NSAPI_IPv4); s.set_port(port); return s;
        }
        int wouldblock(nsapi_size_or_error_t r) { return r == NSAPI_ERROR_WOULD_BLOCK; }
    }  // namespace

    void link_configure(const LinkConfig& c) {
        NetworkInterface* n = eth();
        n->disconnect();
        n->set_blocking(false);
        if (c.dhcp) { n->set_dhcp(true); }
        else {
            SocketAddress ip, mask, gw;
            ip.set_ip_address(c.ip); mask.set_ip_address(c.netmask); gw.set_ip_address(c.gateway);
            n->set_network(ip, mask, gw);
            SocketAddress dns;
            if (c.dns1[0] && dns.set_ip_address(c.dns1)) n->add_dns_server(dns, 0);
            if (c.dns2[0] && dns.set_ip_address(c.dns2)) n->add_dns_server(dns, 0);
        }
        n->connect();
    }
    void link_disconnect() { eth()->disconnect(); }
    LinkState link_status() {
        nsapi_connection_status_t st = eth()->get_connection_status();
        if (st == NSAPI_STATUS_GLOBAL_UP || st == NSAPI_STATUS_LOCAL_UP) return LINK_UP;
        if (st == NSAPI_STATUS_CONNECTING) return LINK_CONNECTING;
        return LINK_DOWN;
    }
    namespace {
        // Copy a resolved SocketAddress into our Ip4 (network-order octets).
        bool addr_to_ip4(const SocketAddress& a, Ip4* out) {
            nsapi_addr_t na = a.get_addr();
            if (na.version != NSAPI_IPv4) return false;
            memcpy(out->b, na.bytes, 4);
            return true;
        }
    }
    // Each link_* getter is the same shape: read one Mbed SocketAddress, copy it
    // out as an Ip4. CALL is the eth() method (it fills the local `a`).
    #define LINK_GETTER(CALL) \
        if (!out) return false; \
        SocketAddress a; \
        if (eth()->CALL != NSAPI_ERROR_OK) return false; \
        return addr_to_ip4(a, out)
    bool link_ip(Ip4* out)                 { LINK_GETTER(get_ip_address(&a)); }
    bool link_netmask(Ip4* out)            { LINK_GETTER(get_netmask(&a)); }
    bool link_gateway(Ip4* out)            { LINK_GETTER(get_gateway(&a)); }
    bool link_dns(uint8_t index, Ip4* out) { LINK_GETTER(get_dns_server((int)index, &a)); }
    #undef LINK_GETTER

    DnsHandle dns_begin(const char* host) {
        if (host == 0 || host[0] == '\0') return 0;
        DnsSlot* slot = 0;
        for (int i = 0; i < MAX_DNS; ++i) if (g_dns[i].st == 0) { slot = &g_dns[i]; break; }
        if (slot == 0) return 0;
        slot->id = ++g_dns_next; slot->mbed_handle = 0; slot->st = 1;   // pending before the call
        nsapi_value_or_error_t r = eth()->gethostbyname_async(
            host, mbed::Callback<void(int, SocketAddress*)>(&on_dns, slot), NSAPI_IPv4);
        if (r == NSAPI_ERROR_OK) return slot->id;        // resolved synchronously; st published
        if (r < 0) { slot->st = 0; return 0; }           // failed to start
        slot->mbed_handle = (int)r;
        return slot->id;
    }
    DnsResult dns_poll(DnsHandle h, Ip4* out) {
        for (int i = 0; i < MAX_DNS; ++i) {
            if (g_dns[i].id != h || g_dns[i].st == 0) continue;
            uint8_t st = HAL::Memory::ordered_read(&g_dns[i].st);
            if (st == 1) return DNS_PENDING;
            if (st == 2) { if (out) *out = g_dns[i].addr; g_dns[i].st = 0; return DNS_OK; }
            g_dns[i].st = 0; return DNS_FAILED;
        }
        return DNS_FAILED;
    }
    void dns_cancel(DnsHandle h) {
        for (int i = 0; i < MAX_DNS; ++i) {
            if (g_dns[i].id != h || g_dns[i].st == 0) continue;
            if (g_dns[i].st == 1 && g_dns[i].mbed_handle != 0)
                eth()->gethostbyname_async_cancel(g_dns[i].mbed_handle);
            g_dns[i].st = 0; return;
        }
    }

    UdpSocket udp_open() {
        for (int i = 0; i < MAX_UDP; ++i) if (!g_udp_used[i]) {
            g_udp[i].close();
            if (g_udp[i].open(eth()) != NSAPI_ERROR_OK) return -1;
            g_udp[i].set_blocking(false); g_udp_used[i] = true; return i;
        }
        return -1;
    }
    bool udp_sendto(UdpSocket s, const Ip4& a, uint16_t port, const void* buf, size_t len) {
        if (s < 0 || s >= MAX_UDP || !g_udp_used[s]) return false;
        return g_udp[s].sendto(make_addr(a, port), buf, len) == (nsapi_size_or_error_t)len;
    }
    int udp_recvfrom(UdpSocket s, void* buf, size_t len, Ip4* from, uint16_t* port) {
        if (s < 0 || s >= MAX_UDP || !g_udp_used[s]) return -1;
        SocketAddress fa;
        nsapi_size_or_error_t r = g_udp[s].recvfrom(&fa, buf, len);
        if (wouldblock(r)) return 0;
        if (r < 0) return -1;
        if (from) { nsapi_addr_t na = fa.get_addr(); memcpy(from->b, na.bytes, 4); }
        if (port) *port = fa.get_port();
        return (int)r;
    }
    void udp_close(UdpSocket s) { if (s >= 0 && s < MAX_UDP && g_udp_used[s]) { g_udp[s].close(); g_udp_used[s] = false; } }

    TcpSocket tcp_open() {
        int i = tcp_slot();
        if (i < 0) return -1;
        g_tcp[i].close();
        if (g_tcp[i].open(eth()) != NSAPI_ERROR_OK) return -1;
        g_tcp[i].set_blocking(false);
        g_tcp_ptr[i] = 0; g_tcp_used[i] = true;
        return i;
    }
    int tcp_connect(TcpSocket s, const Ip4& a, uint16_t port) {
        if (!tcp_valid(s)) return -1;
        nsapi_error_t r = sock(s)->connect(make_addr(a, port));   // re-poll friendly
        if (r == NSAPI_ERROR_OK || r == NSAPI_ERROR_IS_CONNECTED) return 1;
        if (r == NSAPI_ERROR_IN_PROGRESS || r == NSAPI_ERROR_ALREADY) return 0;
        return -1;
    }
    TcpSocket tcp_listen(uint16_t port) {
        int i = tcp_slot();
        if (i < 0) return -1;
        g_tcp[i].close();
        if (g_tcp[i].open(eth()) != NSAPI_ERROR_OK) return -1;
        // Bound to the wildcard address: the link's own IP is what DHCP says it
        // is, and it changes under a lease renewal without the listener caring.
        if (g_tcp[i].bind(port) != NSAPI_ERROR_OK ||
            g_tcp[i].listen(1)   != NSAPI_ERROR_OK) { g_tcp[i].close(); return -1; }
        g_tcp[i].set_blocking(false);
        g_tcp_ptr[i] = 0; g_tcp_used[i] = true;
        return i;
    }
    TcpSocket tcp_accept(TcpSocket listener) {
        if (!tcp_valid(listener)) return -1;
        int i = tcp_slot();
        if (i < 0) return -1;              // no room: leave it pending, retry next beat
        nsapi_error_t e = NSAPI_ERROR_OK;
        TCPSocket* peer = sock(listener)->accept(&e);
        if (peer == 0 || e != NSAPI_ERROR_OK) return -1;
        peer->set_blocking(false);
        g_tcp_ptr[i] = peer; g_tcp_used[i] = true;
        return i;
    }
    bool tcp_peer(TcpSocket s, Ip4* out) {
        if (!tcp_valid(s) || out == 0) return false;
        SocketAddress a;
        if (sock(s)->getpeername(&a) != NSAPI_ERROR_OK) return false;
        return addr_to_ip4(a, out);
    }
    int tcp_send(TcpSocket s, const void* buf, size_t len) {
        if (!tcp_valid(s)) return -1;
        nsapi_size_or_error_t r = sock(s)->send(buf, len);
        if (wouldblock(r)) return 0;
        return r < 0 ? -1 : (int)r;
    }
    int tcp_recv(TcpSocket s, void* buf, size_t len) {
        if (!tcp_valid(s)) return -1;
        nsapi_size_or_error_t r = sock(s)->recv(buf, len);
        if (wouldblock(r)) return 0;
        if (r == 0) return -1;     // peer closed
        return r < 0 ? -1 : (int)r;
    }
    void tcp_close(TcpSocket s) {
        if (!tcp_valid(s)) return;
        sock(s)->close();
        // An accepted socket is Mbed's allocation; close() alone would leak it.
        if (g_tcp_ptr[s] != 0) { delete g_tcp_ptr[s]; g_tcp_ptr[s] = 0; }
        g_tcp_used[s] = false;
    }

}}}

#else
// ---- Simulator backend (real BSD sockets, loopback-only) ----
//
// Thin and protocol-agnostic, like the OPTA backend: it opens real host sockets
// but is hermetic by construction - udp_sendto/tcp_connect refuse any destination
// that isn't 127.0.0.0/8, so the process can never send a packet off-host no matter
// what a config carries. DNS reinforces this by resolving every name to 127.0.0.1,
// where the in-process mock NTP/MQTT responder (botanik_sim/network.h) answers; the
// loopback guard also catches non-loopback IPv4 literals that bypass DNS. DNS is the
// one emulated piece - it mirrors the OPTA gethostbyname_async poll model rather than
// blocking in getaddrinfo. No NTP/MQTT wire knowledge lives here; that stays in the
// responder and in network/.

#include <cerrno>                     // errno / EAGAIN / EINPROGRESS - non-blocking send/recv results
#include <cstring>                    // memcpy / memset / strcmp / strlen - address and buffer handling

// POSIX sockets - the simulator backend talks to a real in-process peer.
#include <fcntl.h>                    // fcntl / O_NONBLOCK - every socket is non-blocking
#include <netinet/in.h>               // sockaddr_in / htons / ntohs - the peer address
#include <sys/socket.h>               // socket / connect / send / recvfrom - the transport itself
#include <unistd.h>                   // close - releasing a socket slot

namespace Botanik { namespace HAL { namespace Network {

    namespace {
        // Set O_NONBLOCK + close-on-exec on a fd; returns the fd (or -1 on
        // failure after closing it). FD_CLOEXEC so System::reset()'s execve
        // re-exec drops the socket instead of leaking it into the new image
        // (a leaked TCP listener/peer would keep its port bound).
        int set_nonblock(int fd) {
            int fl = fcntl(fd, F_GETFL, 0);
            if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) { ::close(fd); return -1; }
            if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) { ::close(fd); return -1; }
            return fd;
        }
        sockaddr_in make_sin(const Ip4& a, uint16_t port) {
            sockaddr_in s;
            memset(&s, 0, sizeof(s));
            s.sin_family = AF_INET;
            s.sin_port   = htons(port);
            memcpy(&s.sin_addr, a.b, 4);
            return s;
        }
        // The ports this process is listening on, in sim (shifted) form; port 0
        // is a free row.
        //
        // What they are for is tcp_connect: a connect aimed at a port WE serve is
        // the firmware talking to its own server, which is already hermetic and
        // must reach that port rather than the mock broker. Everything else is
        // still diverted. The fd is kept beside the port so tcp_close() can
        // retire the row - a stale one would divert a later connect to a port
        // nothing answers on any more.
        struct Listener { int fd; uint16_t port; };
        Listener g_listen[Config::NET_MAX_TCP] = {};
        bool listening_on(uint16_t p) {
            for (int i = 0; i < Config::NET_MAX_TCP; ++i)
                if (g_listen[i].port == p) return true;
            return false;
        }
        void listener_forget(int fd) {
            for (int i = 0; i < Config::NET_MAX_TCP; ++i)
                if (g_listen[i].port != 0 && g_listen[i].fd == fd) g_listen[i].port = 0;
        }
        bool set4(Ip4* out, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
            if (out == 0) return false;
            out->b[0] = a; out->b[1] = b; out->b[2] = c; out->b[3] = d;
            return true;
        }
    }  // namespace

    /** @brief Where a packet actually goes, whatever address the firmware aimed
     *  at: the in-process responder.
     *
     *  Refusing a non-loopback address kept the sim hermetic, but it also made
     *  the shipped configuration untestable - the moment the site declared its
     *  real broker (an IP literal, which no mock resolver ever sees) nothing
     *  connected. Diverting keeps the guarantee (the real address is never
     *  contacted) and lets the sim run the config the hardware will run.
     *
     *  It must be mock_loopback_ip(), not 127.0.0.1: the responder binds a
     *  PID-derived 127/8 address so two sims cannot fight over the shared mock
     *  ports, and a packet sent to .0.1 lands nowhere. */
    Ip4 mock_loopback_ip();
    namespace { Ip4 to_mock_peer(const Ip4&) { return mock_loopback_ip(); } }

    Ip4 mock_loopback_ip() {
        // 127/8 is all loopback on Linux: spread the PID across the low octets so
        // two sim instances land on distinct addresses and don't fight over the
        // shared mock ports. getpid() is stable across a reset re-exec (same PID),
        // so the rebound address matches.
        uint32_t h = (uint32_t)getpid();
        Ip4 a;
        a.b[0] = 127;
        a.b[1] = (uint8_t)(h >> 16);
        a.b[2] = (uint8_t)(h >> 8);
        a.b[3] = (uint8_t)h;
        if (a.b[3] == 0) a.b[3] = 1;   // keep a nonzero host octet
        return a;
    }

    // Link: always up at a fixed loopback address (callers only log / non-null
    // check it).
    void      link_configure(const LinkConfig&) {}
    void      link_disconnect() {}
    LinkState link_status() { return LINK_UP; }
    // Hermetic loopback link: report plausible mock parameters (127.0.0.1, /8)
    // so the UI has something to show (the sim has no real lease).
    bool      link_ip(Ip4* out)      { return set4(out, 127, 0, 0, 1); }
    bool      link_netmask(Ip4* out) { return set4(out, 255, 0, 0, 0); }
    bool      link_gateway(Ip4* out) { return set4(out, 127, 0, 0, 1); }
    bool      link_dns(uint8_t index, Ip4* out) {
        return index > 0 ? false : set4(out, 127, 0, 0, 1);   // single mock resolver
    }

    // DNS: emulated async resolver mirroring the OPTA gethostbyname_async poll
    // model - dns_begin stamps a short deadline, dns_poll returns PENDING until it
    // passes, then 127.0.0.1 (the loopback responder), or FAILED for an
    // unresolvable name. An all-numeric malformed literal is rejected up front
    // (Mbed DNS would fail it too).
    namespace {
        const int      MAX_DNS     = Config::NET_MAX_DNS;
        static_assert(MAX_DNS >= (int)Config::DNS_CACHE_SIZE,
                      "NET_MAX_DNS below DNS_CACHE_SIZE: NameResolver can leave "
                      "every row PENDING, and the surplus lookups would fail here");
        const uint32_t DNS_STEP_MS = 25;   // emulated lookup latency
        struct DnsSlot { DnsHandle id; uint64_t ready_ms; bool fail; };   // id 0 = free
        DnsSlot   g_dns[MAX_DNS] = {};
        DnsHandle g_dns_next = 1;
        // Refused by the emulated resolver: a *.invalid name (RFC 6761) or
        // anything with a character illegal in a hostname (so junk like a trailing
        // space fails, as on real DNS).
        bool unresolvable(const char* h) {
            size_t n = strlen(h);
            if (n >= 8 && strcmp(h + n - 8, ".invalid") == 0) return true;
            for (const char* p = h; *p; ++p) {
                char c = *p;
                bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '-';
                if (!ok) return true;
            }
            return false;
        }
    }
    DnsHandle dns_begin(const char* host) {
        if (host == 0 || host[0] == '\0') return 0;
        bool numeric = true;
        for (const char* p = host; *p; ++p)
            if ((*p < '0' || *p > '9') && *p != '.') { numeric = false; break; }
        if (numeric) return 0;          // malformed numeric literal -> could-not-start
        DnsSlot* s = 0;
        for (int i = 0; i < MAX_DNS; ++i) if (g_dns[i].id == 0) { s = &g_dns[i]; break; }
        if (s == 0) return 0;
        s->id       = ++g_dns_next;
        s->ready_ms = Clock::uptime_ms() + DNS_STEP_MS;
        s->fail     = unresolvable(host);
        return s->id;
    }
    DnsResult dns_poll(DnsHandle h, Ip4* out) {
        for (int i = 0; i < MAX_DNS; ++i) {
            if (g_dns[i].id != h) continue;
            if (Clock::uptime_ms() < g_dns[i].ready_ms) return DNS_PENDING;
            bool fail = g_dns[i].fail;
            g_dns[i].id = 0;            // consume the slot
            if (fail) return DNS_FAILED;
            // Resolve to this instance's loopback so the firmware reaches its own
            // in-process responder (and not a sibling sim's).
            if (out) *out = mock_loopback_ip();
            return DNS_OK;
        }
        return DNS_FAILED;
    }
    void dns_cancel(DnsHandle h) {
        for (int i = 0; i < MAX_DNS; ++i) if (g_dns[i].id == h) { g_dns[i].id = 0; return; }
    }

    UdpSocket udp_open() {
        int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        return fd < 0 ? -1 : set_nonblock(fd);
    }
    bool udp_sendto(UdpSocket s, const Ip4& a, uint16_t port, const void* buf, size_t len) {
        if (s < 0) return false;
        // Hermetic: whatever the site aims at, the packet goes to the in-process
        // responder (see to_mock_peer) - and no privileged ports in the sim, so
        // the well-known NTP port is routed too.
        if (port == 123) port = NTP_MOCK_PORT;
        sockaddr_in sin = make_sin(to_mock_peer(a), port);
        ssize_t n = ::sendto(s, buf, len, 0, reinterpret_cast<sockaddr*>(&sin), sizeof(sin));
        return n == (ssize_t)len;
    }
    int udp_recvfrom(UdpSocket s, void* buf, size_t len, Ip4* from, uint16_t* port) {
        if (s < 0) return -1;
        sockaddr_in sin; socklen_t sl = sizeof(sin);
        ssize_t n = ::recvfrom(s, buf, len, 0, reinterpret_cast<sockaddr*>(&sin), &sl);
        if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
        if (from) memcpy(from->b, &sin.sin_addr, 4);
        if (port) *port = ntohs(sin.sin_port);
        return (int)n;
    }
    void udp_close(UdpSocket s) { if (s >= 0) ::close(s); }

    TcpSocket tcp_open() {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        return fd < 0 ? -1 : set_nonblock(fd);
    }
    int tcp_connect(TcpSocket s, const Ip4& a, uint16_t port) {
        if (s < 0) return -1;
        // Hermetic, exactly as udp_sendto: any peer and any port land on the
        // in-process broker. Matching one well-known port tied the sim to
        // whatever the site happened to configure, which broke the moment the
        // site declared its real broker (192.168.1.43:58443).
        //
        // The exception is a port this process is itself listening on: that is
        // the firmware's own server, already in-process, and diverting it would
        // make the one thing the sim can genuinely test - a client reaching our
        // slave - unreachable.
        port = listening_on(sim_port(port)) ? sim_port(port) : MQTT_MOCK_PORT;
        sockaddr_in sin = make_sin(to_mock_peer(a), port);
        int r = ::connect(s, reinterpret_cast<sockaddr*>(&sin), sizeof(sin));
        if (r == 0) return 1;                              // connected at once (loopback)
        if (errno == EINPROGRESS || errno == EALREADY) return 0;
        if (errno == EISCONN) return 1;
        return -1;
    }
    TcpSocket tcp_listen(uint16_t port) {
        const uint16_t p = sim_port(port);       // no privileged ports in the sim
        int slot = -1;
        for (int i = 0; i < Config::NET_MAX_TCP; ++i) if (g_listen[i].port == 0) { slot = i; break; }
        if (slot < 0) return -1;
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        if (set_nonblock(fd) < 0) return -1;     // closed the fd already
        // Bound to this instance's own 127/8 address, not the wildcard: several
        // sims (and several test binaries under a parallel ctest) run at once and
        // would otherwise fight over one port.
        sockaddr_in sin = make_sin(mock_loopback_ip(), p);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) < 0 ||
            ::listen(fd, 4) < 0) { ::close(fd); return -1; }
        g_listen[slot].fd = fd; g_listen[slot].port = p;
        return fd;
    }
    TcpSocket tcp_accept(TcpSocket listener) {
        if (listener < 0) return -1;
        int fd = ::accept(listener, 0, 0);
        if (fd < 0) return -1;                   // nothing pending, or a dead listener
        return set_nonblock(fd);
    }
    bool tcp_peer(TcpSocket s, Ip4* out) {
        if (s < 0 || out == 0) return false;
        sockaddr_in sin; socklen_t sl = sizeof(sin);
        if (::getpeername(s, reinterpret_cast<sockaddr*>(&sin), &sl) < 0) return false;
        memcpy(out->b, &sin.sin_addr, 4);
        return true;
    }
    int tcp_send(TcpSocket s, const void* buf, size_t len) {
        if (s < 0) return -1;
        ssize_t n = ::send(s, buf, len, 0);
        if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
        return (int)n;
    }
    int tcp_recv(TcpSocket s, void* buf, size_t len) {
        if (s < 0) return -1;
        ssize_t n = ::recv(s, buf, len, 0);
        if (n == 0) return -1;                             // peer closed
        if (n < 0)  return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
        return (int)n;
    }
    void tcp_close(TcpSocket s) {
        if (s < 0) return;
        listener_forget(s);                      // no-op for an ordinary socket
        ::close(s);
    }

}}}

#endif // TARGET_OPTA / simulator
