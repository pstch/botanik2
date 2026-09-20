/**
 * @file net_opta.cpp
 * @brief HAL::Network backends. OPTA wraps Mbed sockets; the simulator uses real BSD
 *        sockets but resolves every name to loopback, so it only ever reaches the
 *        in-process mock responder (botanik_sim/network.h) - never the public
 *        network. The transport here is protocol-agnostic on both targets;
 *        NTP/MQTT wire knowledge lives above it. The TARGET_OPTA /
 *        TARGET_SIMULATOR split is legal here: this file is part of the HAL.
 */

#include "libbotanik/hal/net.h"

#include "libbotanik/config/net.h"

#include "libbotanik/hal/platform.h"

#ifdef TARGET_OPTA

// OPTA backend (Mbed)

#include <cstring>

// Mbed netsocket - resolved from the installed mbed_opta core, not this tree.
#include "netsocket/EthernetInterface.h"
#include "netsocket/NetworkInterface.h"
#include "netsocket/SocketAddress.h"
#include "netsocket/TCPSocket.h"
#include "netsocket/UDPSocket.h"

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
        // What last_error() reports. Only the calls that have a code worth
        // keeping write it; @see net.h.
        nsapi_error_t g_last_error = NSAPI_ERROR_OK;
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
        // Not an nsapi answer, so it gets one of its own: the budget is spent.
        if (i < 0) { g_last_error = NSAPI_ERROR_NO_SOCKET; return -1; }
        g_tcp[i].close();
        nsapi_error_t e = g_tcp[i].open(eth());
        if (e != NSAPI_ERROR_OK) { g_last_error = e; return -1; }
        // Bound to the wildcard address: the link's own IP is what DHCP says it
        // is, and it changes under a lease renewal without the listener caring.
        e = g_tcp[i].bind(port);
        if (e == NSAPI_ERROR_OK) e = g_tcp[i].listen(1);
        if (e != NSAPI_ERROR_OK) { g_last_error = e; g_tcp[i].close(); return -1; }
        g_last_error = NSAPI_ERROR_OK;
        g_tcp[i].set_blocking(false);
        g_tcp_ptr[i] = 0; g_tcp_used[i] = true;
        return i;
    }
    TcpSocket tcp_accept(TcpSocket listener) {
        if (!tcp_valid(listener)) return -1;
        int i = tcp_slot();
        // Distinct from -1: the peer's handshake has completed and nothing will
        // read it. Silence here is what let an oversubscribed budget look like
        // an idle port. @see TCP_NO_SLOT
        if (i < 0) return TCP_NO_SLOT;
        nsapi_error_t e = NSAPI_ERROR_OK;
        TCPSocket* peer = sock(listener)->accept(&e);
        if (peer == 0) return -1;
        // accept() is documented to answer null on failure, so a non-null peer
        // WITH an error should not happen. It is still the only path here that
        // owns an allocation before it decides, and a leak on a socket the
        // firmware accepts for the life of the unit is not a thing to leave to
        // a vendor's documentation.
        if (e != NSAPI_ERROR_OK) { peer->close(); delete peer; return -1; }
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

    int last_error() { return (int)g_last_error; }

}}}

#endif // TARGET_OPTA
