/**
 * @file test_hal_net.cpp
 * @brief Host-side smoke tests for the HAL::Network simulator backend. The backend is
 *        hermetic: DNS is emulated and sockets refuse any non-loopback destination,
 *        so the suite stays offline and deterministic by construction.
 */
#include <cstdint>                    // uint8_t/uint16_t/uint64_t - Ip4 octets, ports, deadlines

#include "doctest.h"                  // the test framework (vendored single header); last so it sees our types

// Pulls the site config, then the feature defaults: the guards below
// need the flags DEFINED, and an undefined one reads as 0 in #if.
#include "libbotanik_config.h"        // Config::NET_* - the mock peer's addresses and timeouts

#include "libbotanik/hal/net.h"       // HAL::Network - under test (against the hermetic backend)
// HAL::Clock::sleep_ms / uptime_ms (poll the async DNS)
#include "libbotanik/hal/platform.h"  // HAL::Clock::uptime_ms / sleep_ms - pumping the async calls

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

TEST_CASE("HAL::Network sim: a listener accepts a connection and carries bytes") {
    // What the Modbus TCP slave needs from the HAL, end to end: bind, accept,
    // and a byte in each direction. The sim binds this instance's own 127/8
    // address, so this runs unchanged beside every other test process.
    const uint16_t PORT = 502;                  // privileged: the backend shifts it
    Network::TcpSocket srv = Network::tcp_listen(PORT);
    REQUIRE(srv >= 0);
    CHECK(Network::tcp_accept(srv) < 0);        // nothing pending yet

    Network::TcpSocket cli = Network::tcp_open();
    REQUIRE(cli >= 0);
    // Aimed at an address the firmware would use; it lands on the listener
    // because THIS process serves that port, not on the mock broker.
    Network::Ip4 any = {{192, 168, 1, 43}};
    REQUIRE(Network::tcp_connect(cli, any, PORT) >= 0);

    Network::TcpSocket peer = -1;
    for (int i = 0; i < 100 && peer < 0; ++i) {  // loopback connect is near-instant
        peer = Network::tcp_accept(srv);
        if (peer < 0) Clock::sleep_ms(2);
    }
    REQUIRE(peer >= 0);

    const uint8_t req[] = {0, 1, 2, 3};
    CHECK(Network::tcp_send(cli, req, sizeof(req)) == (int)sizeof(req));
    uint8_t got[8] = {0};
    int n = 0;
    for (int i = 0; i < 100 && n <= 0; ++i) {
        n = Network::tcp_recv(peer, got, sizeof(got));
        if (n == 0) Clock::sleep_ms(2);          // 0 is would-block, not a close
    }
    REQUIRE(n == (int)sizeof(req));
    CHECK(got[3] == 3);

    const uint8_t rsp[] = {9};
    CHECK(Network::tcp_send(peer, rsp, sizeof(rsp)) == 1);
    n = 0;
    for (int i = 0; i < 100 && n <= 0; ++i) {
        n = Network::tcp_recv(cli, got, sizeof(got));
        if (n == 0) Clock::sleep_ms(2);
    }
    REQUIRE(n == 1);
    CHECK(got[0] == 9);

    // A closed peer reads as closed, not as would-block: that is what tells the
    // server to free the connection slot.
    Network::tcp_close(cli);
    n = 0;
    for (int i = 0; i < 100 && n == 0; ++i) {
        n = Network::tcp_recv(peer, got, sizeof(got));
        if (n == 0) Clock::sleep_ms(2);
    }
    CHECK(n < 0);

    Network::tcp_close(peer);
    Network::tcp_close(srv);
}

TEST_CASE("HAL::Network sim: a released port stops capturing connects") {
    // The listening-port table is what keeps a connect from being diverted to the
    // mock broker. A stale row would send a later connect to a port nothing
    // answers on, so closing the listener has to retire it.
    const uint16_t PORT = 15502;
    Network::TcpSocket srv = Network::tcp_listen(PORT);
    REQUIRE(srv >= 0);
    Network::tcp_close(srv);

    Network::Ip4 any = {{192, 168, 1, 43}};
    Network::TcpSocket cli = Network::tcp_open();
    REQUIRE(cli >= 0);
    CHECK(Network::tcp_connect(cli, any, PORT) >= 0);   // diverted again, not refused
    Network::tcp_close(cli);
}

#endif
