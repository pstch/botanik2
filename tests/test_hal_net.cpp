/**
 * @file test_hal_net.cpp
 * @brief Host-side smoke tests for the HAL::Network simulator backend. The backend is
 *        hermetic: DNS is emulated and sockets refuse any non-loopback destination,
 *        so the suite stays offline and deterministic by construction.
 */
#include <cstdint>
#include <cstring>

#include "doctest.h"

#include "libbotanik/hal/net.h"
// HAL::Clock::sleep_ms / uptime_ms (poll the async DNS)
#include "libbotanik/hal/platform.h"

using namespace Botanik::HAL;

namespace {
    // Poll an async DNS handle to a terminal result, bounded so a hang fails the
    // test instead of spinning forever.
    Network::DnsResult dns_settle(Network::DnsHandle h, Network::Ip4* out) {
        uint64_t start = Clock::uptime_ms();
        for (;;) {
            Network::DnsResult r = Network::dns_poll(h, out);
            if (r != Network::DNS_PENDING) return r;
            if (Clock::uptime_ms() - start > 15000) return r;
            Clock::sleep_ms(5);
        }
    }
}

TEST_CASE("HAL::Network sim: the link is up immediately") {
    CHECK(Network::link_status() == Network::LINK_UP);
    Network::Ip4 ip;
    CHECK(Network::link_ip(&ip));   // some address (host primary or loopback)
}

TEST_CASE("HAL::Network sim: DNS of a guaranteed-bad name fails, not pending forever") {
    Network::DnsHandle h = Network::dns_begin("nonexistent.invalid");
    REQUIRE(h != 0);
    Network::Ip4 out;
    // Emulated async resolver: poll until terminal, which must be FAILED, not pending
    // forever.
    CHECK(dns_settle(h, &out) == Network::DNS_FAILED);
}

TEST_CASE("HAL::Network sim: UDP socket opens and closes") {
    Network::UdpSocket s = Network::udp_open();
    REQUIRE(s >= 0);
    Network::Ip4 dst = {{127, 0, 0, 1}};            // loopback, no listener on this port
    uint8_t pkt[4] = {0};
    CHECK(Network::udp_sendto(s, dst, 9, pkt, sizeof(pkt)));     // local send succeeds
    uint8_t rx[8]; Network::Ip4 from; uint16_t port = 0;
    // would-block: no reply
    CHECK(Network::udp_recvfrom(s, rx, sizeof(rx), &from, &port) == 0);
    Network::udp_close(s);
}

TEST_CASE("HAL::Network sim: TCP connect to a dead loopback port never reports connected") {
    Network::TcpSocket s = Network::tcp_open();
    REQUIRE(s >= 0);
    Network::Ip4 dst = {{127, 0, 0, 1}};            // loopback, nothing listening on port 9
    int r = Network::tcp_connect(s, dst, 9);
    CHECK(r <= 0);                               // 0 in-progress or <0 immediate refuse
    Network::tcp_close(s);
}

TEST_CASE("HAL::Network sim: an off-host destination is diverted, never contacted") {
    // The sim must never put a packet on the wire, whatever address it is handed
    // - including a non-loopback IPv4 literal that bypasses DNS, which is what a
    // site's real broker address is.
    //
    // It used to REFUSE those outright. That kept the guarantee but made the
    // shipped configuration untestable: the moment the site declared its actual
    // broker (192.168.1.43:58443) the sim could not connect to anything, so the
    // MQTT state machine never ran. Diverting to the in-process responder keeps
    // the same guarantee - the real address is never contacted - while letting
    // the sim exercise the config the hardware will run.
    Network::Ip4 ext = {{8, 8, 8, 8}};              // clearly off-host
    uint8_t pkt[4] = {0};

    Network::UdpSocket u = Network::udp_open();
    REQUIRE(u >= 0);
    CHECK(Network::udp_sendto(u, ext, 123, pkt, sizeof(pkt)));   // diverted, not refused
    Network::udp_close(u);

    Network::TcpSocket t = Network::tcp_open();
    REQUIRE(t >= 0);
    CHECK(Network::tcp_connect(t, ext, 58443) >= 0);             // diverted to the mock broker
    Network::tcp_close(t);
}
