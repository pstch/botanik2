/**
 * @file test_hal_net.cpp
 * @brief Host-side smoke tests for the HAL::Net simulator backend (real BSD
 *        sockets). Kept offline-deterministic: DNS uses a guaranteed-unresolvable
 *        .invalid name, TCP connects to a non-routable TEST-NET-1 address.
 */
#include <cstdint>
#include <cstring>

#include "doctest.h"

#include "libbotanik/hal/net.h"
// HAL::sleep_ms / uptime_ms (poll the async DNS)
#include "libbotanik/hal/platform.h"

using namespace Botanik::HAL;

namespace {
    // Poll an async DNS handle to a terminal result, bounded so a hang fails the
    // test instead of spinning forever.
    Net::DnsResult dns_settle(Net::DnsHandle h, Net::Ip4* out) {
        uint64_t start = uptime_ms();
        for (;;) {
            Net::DnsResult r = Net::dns_poll(h, out);
            if (r != Net::DNS_PENDING) return r;
            if (uptime_ms() - start > 15000) return r;
            sleep_ms(5);
        }
    }
}

TEST_CASE("HAL::Net sim: the link is up immediately") {
    CHECK(Net::link_status() == Net::LINK_UP);
    Net::Ip4 ip;
    CHECK(Net::link_ip(&ip));   // some address (host primary or loopback)
}

TEST_CASE("HAL::Net sim: DNS of a guaranteed-bad name fails, not pending forever") {
    Net::DnsHandle h = Net::dns_begin("nonexistent.invalid");
    REQUIRE(h != 0);
    Net::Ip4 out;
    // Async (getaddrinfo_a): poll until terminal, which must be FAILED, not
    // pending forever.
    CHECK(dns_settle(h, &out) == Net::DNS_FAILED);
}

TEST_CASE("HAL::Net sim: UDP socket opens and closes") {
    Net::UdpSocket s = Net::udp_open();
    REQUIRE(s >= 0);
    Net::Ip4 dst = {{192, 0, 2, 1}};            // TEST-NET-1, non-routable
    uint8_t pkt[4] = {0};
    CHECK(Net::udp_sendto(s, dst, 123, pkt, sizeof(pkt)));   // local send succeeds
    uint8_t rx[8]; Net::Ip4 from; uint16_t port = 0;
    // would-block: no reply
    CHECK(Net::udp_recvfrom(s, rx, sizeof(rx), &from, &port) == 0);
    Net::udp_close(s);
}

TEST_CASE("HAL::Net sim: TCP connect to a dead address never reports connected") {
    Net::TcpSocket s = Net::tcp_open();
    REQUIRE(s >= 0);
    Net::Ip4 dst = {{192, 0, 2, 1}};            // TEST-NET-1: connect can never complete
    int r = Net::tcp_connect(s, dst, 1883);
    CHECK(r <= 0);                               // 0 in-progress or <0 immediate refuse
    Net::tcp_close(s);
}
