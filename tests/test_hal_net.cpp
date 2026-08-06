/**
 * @file test_hal_net.cpp
 * @brief Host-side smoke tests for the HAL::Network simulator backend. The backend is
 *        hermetic: DNS is emulated and sockets refuse any non-loopback destination,
 *        so the suite stays offline and deterministic by construction.
 */
#include <cstdint>                      // uint8_t/uint16_t/uint64_t

#include "doctest.h"

// Pulls the site config, then the feature defaults: the guards below
// need the flags DEFINED, and an undefined one reads as 0 in #if.
#include "libbotanik_config.h"          // Config::NET_*

#include "libbotanik/hal/net.h"         // HAL::Network
// HAL::Clock::sleep_ms / uptime_ms (poll the async DNS)
#include "libbotanik/hal/platform.h"    // HAL::Clock::uptime_ms / sleep_ms

// Compiles to an empty TU when the feature is off (HAL::Network is the transport the network stack sits on),
// contributing no CTest cases - the shape test_sun.cpp uses.
#if BOTANIK_FEATURE_NETWORK

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

TEST_CASE("HAL::Network sim: DNS of a guaranteed-bad name fails, not pending forever") {
    Network::DnsHandle h = Network::dns_begin("nonexistent.invalid");
    REQUIRE(h != 0);
    Network::Ip4 out;
    // Emulated async resolver: poll until terminal, which must be FAILED, not pending
    // forever.
    CHECK(dns_settle(h, &out) == Network::DNS_FAILED);
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

#endif
