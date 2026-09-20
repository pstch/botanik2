/**
 * @file net_sim.cpp
 * @brief HAL::Network backends. OPTA wraps Mbed sockets; the simulator uses real BSD
 *        sockets but resolves every name to loopback, so it only ever reaches the
 *        in-process mock responder (botanik_sim/network.h) - never the public
 *        network. The transport here is protocol-agnostic on both targets;
 *        NTP/MQTT wire knowledge lives above it. The TARGET_OPTA /
 *        TARGET_SIMULATOR split is legal here: this file is part of the HAL.
 */

#include "libbotanik/hal/net.h"

#include "libbotanik/config/net.gen.h"

#include "libbotanik/hal/platform.h"

#ifdef TARGET_SIMULATOR

// Simulator backend (real BSD sockets, loopback-only)
//
// Thin and protocol-agnostic, like the OPTA backend: real host sockets, but
// hermetic by construction - udp_sendto/tcp_connect divert every destination to
// this instance's loopback, where the in-process mock NTP/MQTT responder
// (botanik_sim/network.h) answers, so the process can never send a packet
// off-host whatever a config carries. DNS is the one emulated piece: it mirrors
// the OPTA gethostbyname_async poll model rather than blocking in getaddrinfo.
// No NTP/MQTT wire knowledge here; that stays in the responder and in network/.

#include <cerrno>
#include <cstring>

// POSIX sockets - the simulator backend talks to a real in-process peer.
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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
        // is a free row. For tcp_connect: a connect aimed at a port WE serve is
        // the firmware talking to its own server, which must reach that port
        // rather than the mock broker. The fd is kept beside the port so
        // tcp_close() can retire the row - a stale one would divert a later
        // connect to a port nothing answers on any more.
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
     *  Diverting rather than refusing keeps the guarantee (the real address is
     *  never contacted) while letting the sim run the config the hardware runs,
     *  IP literals included.
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
        /** @brief id 0 marks a FREE slot, and 0 is also the invalid handle
         *  (@see net.h), so every lookup here has to reject it before the walk:
         *  dns_poll(0) otherwise matched the first free slot, whose ready_ms is
         *  0, and answered DNS_OK with the loopback address. */
        struct DnsSlot { DnsHandle id; uint64_t ready_ms; bool fail; };
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
        if (h == 0) return DNS_FAILED;
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
        if (h == 0) return;
        for (int i = 0; i < MAX_DNS; ++i) if (g_dns[i].id == h) { g_dns[i].id = 0; return; }
    }

    UdpSocket udp_open() {
        int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        return fd < 0 ? -1 : set_nonblock(fd);
    }
    namespace {
        /** @brief Where each UDP socket was last aimed, BEFORE the diversion.
         *
         *  The emulation must be transparent in both directions: if recvfrom()
         *  reported the physical peer, code that checks the source (NtpClient
         *  does, so a stray datagram cannot set the clock) would reject every
         *  reply on the simulator while working on hardware.
         *
         *  One row per socket, keyed by fd. */
        struct UdpAim { int fd; Ip4 addr; uint16_t port; };
        UdpAim g_udp_aim[Config::NET_MAX_UDP] = {};

        void aim_remember(int fd, const Ip4& a, uint16_t port) {
            for (int i = 0; i < Config::NET_MAX_UDP; ++i)
                if (g_udp_aim[i].fd == fd) { g_udp_aim[i].addr = a; g_udp_aim[i].port = port; return; }
            for (int i = 0; i < Config::NET_MAX_UDP; ++i)
                if (g_udp_aim[i].fd == 0) { g_udp_aim[i] = UdpAim{ fd, a, port }; return; }
        }
        const UdpAim* aim_of(int fd) {
            for (int i = 0; i < Config::NET_MAX_UDP; ++i)
                if (g_udp_aim[i].fd == fd) return &g_udp_aim[i];
            return nullptr;
        }
        void aim_forget(int fd) {
            for (int i = 0; i < Config::NET_MAX_UDP; ++i)
                if (g_udp_aim[i].fd == fd) g_udp_aim[i] = UdpAim{};
        }
    }  // namespace

    bool udp_sendto(UdpSocket s, const Ip4& a, uint16_t port, const void* buf, size_t len) {
        if (s < 0) return false;
        // Hermetic: whatever the site aims at, the packet goes to the in-process
        // responder (see to_mock_peer) - and no privileged ports in the sim, so
        // a well-known port (NTP's 123) is shifted by the same rule a listener
        // binds under.
        aim_remember(s, a, port);
        const uint16_t phys = sim_port(port);
        sockaddr_in sin = make_sin(to_mock_peer(a), phys);
        ssize_t n = ::sendto(s, buf, len, 0, reinterpret_cast<sockaddr*>(&sin), sizeof(sin));
        return n == (ssize_t)len;
    }
    int udp_recvfrom(UdpSocket s, void* buf, size_t len, Ip4* from, uint16_t* port) {
        if (s < 0) return -1;
        sockaddr_in sin; socklen_t sl = sizeof(sin);
        ssize_t n = ::recvfrom(s, buf, len, 0, reinterpret_cast<sockaddr*>(&sin), &sl);
        if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
        // Undo the diversion: an answer from the mock is reported as coming from
        // whoever this socket was aimed at. Anything else (nothing sends here in
        // practice) is passed through as itself.
        const UdpAim* aim = aim_of(s);
        Ip4 phys_from; memcpy(phys_from.b, &sin.sin_addr, 4);
        const uint16_t phys_port = ntohs(sin.sin_port);
        const bool from_mock = aim != nullptr &&
            memcmp(phys_from.b, to_mock_peer(aim->addr).b, 4) == 0 &&
            phys_port == sim_port(aim->port);
        if (from) *from = from_mock ? aim->addr : phys_from;
        if (port) *port = from_mock ? aim->port : phys_port;
        return (int)n;
    }
    void udp_close(UdpSocket s) { if (s >= 0) { aim_forget(s); ::close(s); } }

    TcpSocket tcp_open() {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        return fd < 0 ? -1 : set_nonblock(fd);
    }
    int tcp_connect(TcpSocket s, const Ip4& a, uint16_t port) {
        if (s < 0) return -1;
        // Hermetic, exactly as udp_sendto: any peer and any port land on the
        // in-process broker. The exception is a port this process is itself
        // listening on: that is the firmware's own server, already in-process,
        // and diverting it would make the one thing the sim can genuinely test
        // (a client reaching our slave) unreachable.
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
        // Rebind over our own TIME_WAIT: any connection this listener ACCEPTED
        // leaves an entry on the same local port, and a bind without this is
        // refused for as long as one lingers.
        //
        // Not SO_REUSEPORT: two live listeners on one port is exactly the
        // collision the per-instance 127/8 address below exists to prevent.
        const int one = 1;
        (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        // Bound to this instance's own 127/8 address, not the wildcard: several
        // sims (and several test binaries under a parallel ctest) run at once and
        // would otherwise fight over one port.
        sockaddr_in sin = make_sin(mock_loopback_ip(), p);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) < 0 ||
            ::listen(fd, 4) < 0) {
            // close() would overwrite what last_error() is here to report.
            const int why = errno;
            ::close(fd);
            errno = why;
            return -1;
        }
        g_listen[slot].fd = fd; g_listen[slot].port = p;
        return fd;
    }
    TcpSocket tcp_accept(TcpSocket listener) {
        // Never TCP_NO_SLOT: a host fd needs no table, so the budget the OPTA
        // runs out of does not exist here. That asymmetry is exactly why an
        // oversubscribed NET_MAX_TCP could ship - nothing on this side of the
        // HAL can reach it.
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
        ssize_t n = ::send(s, buf, len, MSG_NOSIGNAL);
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

    // errno IS the backend code here, and every call above leaves it set.
    int last_error() { return errno; }

}}}

#endif // TARGET_SIMULATOR
