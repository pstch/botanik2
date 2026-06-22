/**
 * @file net.cpp
 * @brief HAL::Net backends. OPTA wraps Mbed sockets; the simulator uses Linux BSD
 *        sockets so NTP/MQTT genuinely run on the host. The TARGET_OPTA / TARGET_SIMULATOR
 *        split is legal here: this file is part of the HAL.
 */

#include "libbotanik/hal/net.h"
#include "libbotanik/hal/platform.h"

#ifdef TARGET_OPTA
// ---- OPTA backend (Mbed) ----

#include "netsocket/EthernetInterface.h"
#include "netsocket/NetworkInterface.h"
#include "netsocket/SocketAddress.h"
#include "netsocket/UDPSocket.h"
#include "netsocket/TCPSocket.h"
#include <cstring>

namespace Botanik { namespace HAL { namespace Net {

    namespace {
        // The single wired interface, built lazily (not during static init).
        NetworkInterface* eth() { static EthernetInterface e; return &e; }

        // Fixed socket tables: small integer ids map to Mbed socket objects.
        const int MAX_UDP = 2;
        const int MAX_TCP = 2;
        UDPSocket g_udp[MAX_UDP]; bool g_udp_used[MAX_UDP] = {false, false};
        TCPSocket g_tcp[MAX_TCP]; bool g_tcp_used[MAX_TCP] = {false, false};

        // DNS slot table: handle -> {state, addr}. Async callback publishes state.
        // st: 0 free, 1 pending, 2 ok, 3 failed
        const int MAX_DNS = 4;
        struct DnsSlot { volatile uint8_t st; Ip4 addr; int mbed_handle; DnsHandle id; };
        DnsSlot g_dns[MAX_DNS];
        DnsHandle g_dns_next = 1;
        void on_dns(DnsSlot* slot, int result, SocketAddress* a) {
            if (result > 0 && a != 0) {
                nsapi_addr_t na = a->get_addr();
                slot->addr.b[0]=na.bytes[0]; slot->addr.b[1]=na.bytes[1];
                slot->addr.b[2]=na.bytes[2]; slot->addr.b[3]=na.bytes[3];
                HAL::dsb(); slot->st = 2;
            } else { HAL::dsb(); slot->st = 3; }
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
    bool link_ip(Ip4* out) {
        if (!out) return false;
        SocketAddress a;
        if (eth()->get_ip_address(&a) != NSAPI_ERROR_OK) return false;
        nsapi_addr_t na = a.get_addr();
        out->b[0]=na.bytes[0]; out->b[1]=na.bytes[1]; out->b[2]=na.bytes[2]; out->b[3]=na.bytes[3];
        return true;
    }

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
            uint8_t st = HAL::ordered_read(&g_dns[i].st);
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
        for (int i = 0; i < MAX_TCP; ++i) if (!g_tcp_used[i]) {
            g_tcp[i].close();
            if (g_tcp[i].open(eth()) != NSAPI_ERROR_OK) return -1;
            g_tcp[i].set_blocking(false); g_tcp_used[i] = true; return i;
        }
        return -1;
    }
    int tcp_connect(TcpSocket s, const Ip4& a, uint16_t port) {
        if (s < 0 || s >= MAX_TCP || !g_tcp_used[s]) return -1;
        nsapi_error_t r = g_tcp[s].connect(make_addr(a, port));   // re-poll friendly
        if (r == NSAPI_ERROR_OK || r == NSAPI_ERROR_IS_CONNECTED) return 1;
        if (r == NSAPI_ERROR_IN_PROGRESS || r == NSAPI_ERROR_ALREADY) return 0;
        return -1;
    }
    int tcp_send(TcpSocket s, const void* buf, size_t len) {
        if (s < 0 || s >= MAX_TCP || !g_tcp_used[s]) return -1;
        nsapi_size_or_error_t r = g_tcp[s].send(buf, len);
        if (wouldblock(r)) return 0;
        return r < 0 ? -1 : (int)r;
    }
    int tcp_recv(TcpSocket s, void* buf, size_t len) {
        if (s < 0 || s >= MAX_TCP || !g_tcp_used[s]) return -1;
        nsapi_size_or_error_t r = g_tcp[s].recv(buf, len);
        if (wouldblock(r)) return 0;
        if (r == 0) return -1;     // peer closed
        return r < 0 ? -1 : (int)r;
    }
    void tcp_close(TcpSocket s) { if (s >= 0 && s < MAX_TCP && g_tcp_used[s]) { g_tcp[s].close(); g_tcp_used[s] = false; } }

}}}

#else
// ---- Simulator backend (Linux BSD sockets) ----

#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

namespace Botanik { namespace HAL { namespace Net {

    namespace {
        // Set O_NONBLOCK on a fd; returns the fd (or -1 on failure after closing it).
        int set_nonblock(int fd) {
            int fl = fcntl(fd, F_GETFL, 0);
            if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) { ::close(fd); return -1; }
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
    }  // namespace

    // Link: the host network is always up. No device to configure.
    void      link_configure(const LinkConfig&) {}
    void      link_disconnect() {}
    LinkState link_status() { return LINK_UP; }
    bool      link_ip(Ip4* out) {
        if (out == 0) return false;
        out->b[0] = 127; out->b[1] = 0; out->b[2] = 0; out->b[3] = 1;   // loopback: sufficient (callers only log / check non-null)
        return true;
    }

    // DNS: synchronous getaddrinfo surfaced through the poll API. The handle encodes the
    // terminal result directly (no in-flight state): 2 == OK, 3 == FAILED. Resolved address
    // in a single global slot - safe only because the caller polls immediately after
    // dns_begin (no interleaved concurrent lookups).
    namespace {
        Ip4      g_dns_addr;
        DnsHandle g_dns_next = 1;
    }
    DnsHandle dns_begin(const char* host) {
        if (host == 0 || host[0] == '\0') return 0;
        // A valid dotted-quad literal is handled by the caller before reaching here.
        // Any remaining all-numeric string (digits and '.') is a malformed literal;
        // hardware Mbed DNS treats it as a hostname and fails, so reject it here too
        // rather than letting getaddrinfo coerce classful notation (e.g. "1.2.3" -> 1.2.0.3).
        for (const char* p = host; *p; ++p)
            if ((*p < '0' || *p > '9') && *p != '.') goto not_numeric;
        return (++g_dns_next << 2) | 3;   // all-numeric malformed literal - FAILED
        not_numeric:
        addrinfo hints; memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        addrinfo* res = 0;
        DnsHandle h = ++g_dns_next;
        if (getaddrinfo(host, 0, &hints, &res) != 0 || res == 0)
            return (h << 2) | 3;   // FAILED
        sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(res->ai_addr);
        memcpy(g_dns_addr.b, &sin->sin_addr, 4);
        freeaddrinfo(res);
        return (h << 2) | 2;       // OK
    }
    DnsResult dns_poll(DnsHandle h, Ip4* out) {
        if ((h & 3) == 2) { if (out) *out = g_dns_addr; return DNS_OK; }
        return DNS_FAILED;
    }
    void dns_cancel(DnsHandle) {}

    UdpSocket udp_open() {
        int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        return fd < 0 ? -1 : set_nonblock(fd);
    }
    bool udp_sendto(UdpSocket s, const Ip4& a, uint16_t port, const void* buf, size_t len) {
        if (s < 0) return false;
        sockaddr_in sin = make_sin(a, port);
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
        sockaddr_in sin = make_sin(a, port);
        int r = ::connect(s, reinterpret_cast<sockaddr*>(&sin), sizeof(sin));
        if (r == 0) return 1;                              // connected at once (loopback)
        if (errno == EINPROGRESS || errno == EALREADY) return 0;
        if (errno == EISCONN) return 1;
        return -1;
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
    void tcp_close(TcpSocket s) { if (s >= 0) ::close(s); }

}}}

#endif // TARGET_OPTA / simulator
