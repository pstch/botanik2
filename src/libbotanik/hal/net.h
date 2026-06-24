#ifndef HAL_NET_H
#define HAL_NET_H

/**
 * @file net.h
 * @brief Minimal HAL socket layer: a single network link, non-blocking DNS, UDP and TCP.
 *
 * Portable surface used by the network components (resolver / NTP / MQTT). The
 * two backends live in net.cpp: Mbed sockets on OPTA, Linux BSD sockets on the
 * simulator. No Mbed or POSIX socket headers leak through this header. The link
 * is implicit: a socket is opened on the active interface, so callers never hold
 * an interface handle.
 */

#include <cstddef>
#include <cstdint>

namespace Botanik { namespace HAL { namespace Network {

    /** @name Address */
    /** @{ */
    /** @brief IPv4 address as four octets (network order). */
    struct Ip4 { uint8_t b[4]; };
    /** @} */

#ifdef TARGET_SIMULATOR
    /** @name Simulator loopback endpoints
     *  The sim is hermetic: names resolve to mock_loopback_ip() and an in-process
     *  mock responder (botanik_sim/network.h) answers NTP/MQTT over loopback. The
     *  sim has no privileged ports, so the responder binds these high ports; the
     *  HAL routes the well-known NTP port (123) here, and the sim's MQTT config
     *  points at MQTT_MOCK_PORT directly.
     *  @{ */
    static const uint16_t NTP_MOCK_PORT  = 10123;
    static const uint16_t MQTT_MOCK_PORT = 11883;

    /** @brief Per-process loopback address. 127.0.0.0/8 is entirely loopback on
     *  Linux, so each sim instance resolves/binds to its own 127.x.y.z (derived
     *  from the PID); concurrent simulators then never collide on the shared mock
     *  ports without needing a network namespace. Stable within a process. */
    Ip4 mock_loopback_ip();
    /** @} */
#endif

    /** @name Link (the single network interface) */
    /** @{ */
    /** @brief Bring-up parameters. Dotted-decimal strings; empty = unset. When
     *  dhcp is true the static fields are ignored. */
    struct LinkConfig {
        bool dhcp;
        char ip[16], netmask[16], gateway[16], dns1[16], dns2[16];
    };
    enum LinkState { LINK_DOWN, LINK_CONNECTING, LINK_UP };

    /** @brief Start a non-blocking (re)bring-up from @p cfg. */
    void      link_configure(const LinkConfig& cfg);
    /** @brief Tear the link down (idempotent). */
    void      link_disconnect();
    LinkState link_status();
    /** @brief Current address into @p out; false if the link is not up. */
    bool      link_ip(Ip4* out);
    /** @brief Active netmask into @p out; false if the link is not up. */
    bool      link_netmask(Ip4* out);
    /** @brief Active default gateway into @p out; false if unavailable. */
    bool      link_gateway(Ip4* out);
    /** @brief Active DNS server @p index (0/1) into @p out; false if none.
     *  Reflects the DHCP lease or the static config, whichever is in use. */
    bool      link_dns(uint8_t index, Ip4* out);
    /** @} */

    /** @name DNS (non-blocking, poll model) */
    /** @{ */
    typedef uint32_t DnsHandle;        ///< 0 == invalid / could-not-start
    enum DnsResult { DNS_PENDING, DNS_OK, DNS_FAILED };

    /** @brief Start resolving @p host; returns 0 if a lookup could not be started.
     *  Caller contract: poll the handle with dns_poll until a terminal result, or
     *  release it with dns_cancel. Leaked handles exhaust the small fixed slot
     *  table (OPTA). */
    DnsHandle dns_begin(const char* host);
    /** @brief Poll @p h. On DNS_OK fills @p out; DNS_OK/DNS_FAILED consume the
     *  handle. */
    DnsResult dns_poll(DnsHandle h, Ip4* out);
    /** @brief Cancel an in-flight lookup (idempotent). */
    void      dns_cancel(DnsHandle h);
    /** @} */

    /** @name UDP / TCP
     *  recv returns >0 bytes, 0 would-block, <0 closed/error. A socket id < 0 is
     *  invalid. */
    /** @{ */
    typedef int32_t UdpSocket;
    typedef int32_t TcpSocket;

    UdpSocket udp_open();
    bool      udp_sendto(UdpSocket s, const Ip4& addr, uint16_t port,
                         const void* buf, size_t len);
    int       udp_recvfrom(UdpSocket s, void* buf, size_t len, Ip4* from, uint16_t* port);
    void      udp_close(UdpSocket s);

    TcpSocket tcp_open();
    /** @brief Non-blocking connect: 1 connected, 0 in-progress (poll again),
     *  <0 error. */
    int       tcp_connect(TcpSocket s, const Ip4& addr, uint16_t port);
    /** @brief Send: bytes written, <0 error. */
    int       tcp_send(TcpSocket s, const void* buf, size_t len);
    /** @brief Recv: >0 bytes, 0 would-block, <0 closed. */
    int       tcp_recv(TcpSocket s, void* buf, size_t len);
    void      tcp_close(TcpSocket s);
    /** @} */

}}}  // namespace Botanik::HAL::Network

#endif // HAL_NET_H
