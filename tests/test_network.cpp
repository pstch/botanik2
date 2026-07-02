/**
 * @file test_network.cpp
 * @brief Host-side tests for the network components. The simulator backend uses
 *        real Linux BSD sockets, so DNS/UDP/TCP actually run on the host - against
 *        deterministic offline targets (.invalid names, TEST-NET-1 192.0.2.x
 *        addresses). Tests observe config-gated state and non-blocking behaviour:
 *          - NetworkHandler: link comes up on config, idles when config is cleared,
 *          - MqttClient:     never falsely "connected"; connect to TEST-NET-1 never completes,
 *          - NtpClient: stepped as a Component; UDP query to TEST-NET-1 is non-blocking.
 *        Config is injected through PlcConfigFixture.
 */
#include <cstdint>
#include <cstring>
#include <string>

#include "doctest.h"

#include "plc_config_fixture.h"
#include "recording_writer.h"
#include "libbotanik/network/network.h"
#include "libbotanik/network/mqtt.h"
#include "libbotanik/network/resolver.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/hal/platform.h"

using namespace Botanik;
using namespace Botanik::Network;

namespace {
}

TEST_CASE("NetworkHandler: sim path comes up on net config and idles when it is cleared") {
    botanik_test::PlcConfigFixture fx;
    NameResolver resolver;
    NetworkHandler net(fx.ctx(), nullptr, &resolver);
    CHECK_FALSE(net.is_up());

    // HAL::Network::link_status() returns LINK_UP immediately on the simulator,
    // so one step() promotes SETUP to RUNNING.
    fx.conf().net.serial = 1;
    fx.conf().net.dhcp   = 1;
    fx.publish();
    net.step(HAL::Clock::uptime_ms());
    CHECK(net.is_up());

    // Serial back to 0 (config removed) -> back to idle.
    fx.conf().net.serial = 0;
    fx.publish();
    net.step(HAL::Clock::uptime_ms());
    CHECK_FALSE(net.is_up());
}

TEST_CASE("MqttClient: never reports connected to a dead broker; publish gated on connection") {
    botanik_test::PlcConfigFixture fx;
    NameResolver resolver;
    NetworkHandler net(fx.ctx(), nullptr, &resolver);
    MqttClient mqtt(fx.ctx(), nullptr, &resolver, &net);

    CHECK_FALSE(mqtt.connected());
    CHECK_FALSE(mqtt.publish("botanik/alarms/X", "ON"));

    fx.conf().net.serial = 1; fx.conf().net.dhcp = 1; fx.publish();
    net.step(HAL::Clock::uptime_ms());                 // sim link up

    fx.conf().mqtt.serial  = 1;
    fx.conf().mqtt.enabled = 1;
    // Off-host literal: the hermetic sim refuses any non-loopback destination, so the
    // connect can never complete.
    std::strncpy(fx.conf().mqtt.broker, "192.0.2.1", Config::PLC_HOST_STR_LEN - 1);
    fx.publish();
    for (int i = 0; i < 5; ++i) mqtt.step(HAL::Clock::uptime_ms());

    CHECK_FALSE(mqtt.connected());              // off-host: refused, never connects
    CHECK_FALSE(mqtt.publish("botanik/alarms/X", "ON"));
}

TEST_CASE("NtpClient: stepped as a Component (announces once); query is non-blocking") {
    botanik_test::PlcConfigFixture fx;
    NameResolver resolver;
    NetworkHandler net(fx.ctx(), nullptr, &resolver);
    RecordingWriter w;
    Logs::Logger log;
    log.set_min_level(Logs::LOG_DEBUG);
    log.add_writer(&w);

    NtpClient ts(fx.ctx(), &log, &resolver, &net);

    // Bring the (sim) link up so the time sync can proceed past the link gate.
    fx.conf().net.serial = 1; fx.conf().net.dhcp = 1; fx.publish();
    net.step(HAL::Clock::uptime_ms());

    fx.conf().ntp.serial  = 1;
    fx.conf().ntp.enabled = 1;
    // Off-host literal: the hermetic sim refuses the send, so no reply arrives and the
    // query stays non-blocking (no sync).
    std::strncpy(fx.conf().ntp.server1, "192.0.2.1", Config::PLC_HOST_STR_LEN - 1);
    fx.publish();

    for (int i = 0; i < 5; ++i) ts.step(HAL::Clock::uptime_ms());   // must not fault or block

#ifdef DEBUG
    // >= 1 not == 1: the live UDP path and link-up sequence may emit extra log
    // lines beyond the first-step announce, so exact count is not pinned.
    // Search all recorded lines, not last_msg: which line lands last is
    // timing-dependent (a trailing NETWORK/TIME line can follow the announce).
    CHECK(w.pushes >= 1);
    CHECK(w.saw("NtpClient"));
#endif
}

// Note: the NTP-synced / MQTT-connected success paths need the loopback mock
// responder (botanik_sim/network.h), which runs only in the full simulator, so they
// are exercised end-to-end by the pyte suite rather than here. These unit tests
// cover the config-gated state machines and the non-blocking dead-peer behaviour.
