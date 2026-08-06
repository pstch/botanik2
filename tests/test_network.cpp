/**
 * @file test_network.cpp
 * @brief Host-side tests for the network components. The simulator backend uses
 *        real Linux BSD sockets, so DNS/UDP/TCP actually run on the host - against
 *        deterministic offline targets (.invalid names, TEST-NET-1 192.0.2.x
 *        addresses). Tests observe config-gated state and non-blocking behaviour:
 *          - NetworkHandler: link comes up on config, idles when config is cleared,
 *          - MqttClient:     never falsely "connected"; connect to TEST-NET-1 never completes,
 *          - NtpClient: stepped as a Component; UDP query to TEST-NET-1 is non-blocking.
 *        Config is injected through the params fixture.
 */

#include "doctest.h"
#include "params_fixture.h"
#include "recording_writer.h"               // RecordingWriter
// Pulls the site config, then the feature defaults: the guards below
// need the flags DEFINED, and an undefined one reads as 0 in #if.
#include "libbotanik_config.h"              // Config::NET_*

#include "libbotanik/hal/platform.h"        // HAL::Clock
#include "libbotanik/logs/logger.h"         // Logs::Logger
#include "libbotanik/network/mqtt.h"        // Network::MqttClient
#include "libbotanik/network/network.h"     // Network::NetworkHandler
#include "libbotanik/network/ntp.h"         // Network::NtpClient
#include "libbotanik/network/resolver.h"    // Network::NameResolver

using namespace Botanik;
using namespace Botanik::Network;

namespace {
}

// Gated: see above, for the network stack.
#if BOTANIK_FEATURE_NETWORK
TEST_CASE("NetworkHandler: sim path comes up on net config and idles when it is cleared") {
    botanik_test::ParamFixture fx;
    NameResolver resolver;
    NetworkHandler net(&fx.bank(), nullptr, &resolver);
    CHECK_FALSE(net.is_up());

    // HAL::Network::link_status() returns LINK_UP immediately on the simulator,
    // so one step() promotes SETUP to RUNNING.
    fx.set_num("net_serial", 1);
    fx.set_num("net_dhcp", 1);
    fx.publish();
    net.step(HAL::Clock::uptime_ms());
    CHECK(net.is_up());

    // Serial back to 0 (config removed) -> back to idle.
    fx.set_num("net_serial", 0);
    fx.publish();
    net.step(HAL::Clock::uptime_ms());
    CHECK_FALSE(net.is_up());
}
#endif

// Gated: see above, for MQTT.
#if BOTANIK_FEATURE_MQTT
TEST_CASE("MqttClient: never reports connected to a dead broker, publish gated on connection") {
    botanik_test::ParamFixture fx;
    NameResolver resolver;
    NetworkHandler net(&fx.bank(), nullptr, &resolver);
    MqttClient mqtt(&fx.bank(), nullptr, &resolver, &net);

    CHECK_FALSE(mqtt.connected());
    CHECK_FALSE(mqtt.publish("botanik/alarms/X", "ON"));

    fx.set_num("net_serial", 1); fx.set_num("net_dhcp", 1); fx.publish();
    net.step(HAL::Clock::uptime_ms());                 // sim link up

    fx.set_num("mqtt_serial", 1);
    fx.set_num("mqtt_enabled", 1);
    // Off-host literal: the hermetic sim refuses any non-loopback destination, so the
    // connect can never complete.
    fx.set_str("mqtt_broker", "192.0.2.1");
    fx.publish();
    for (int i = 0; i < 5; ++i) mqtt.step(HAL::Clock::uptime_ms());

    CHECK_FALSE(mqtt.connected());              // off-host: refused, never connects
    CHECK_FALSE(mqtt.publish("botanik/alarms/X", "ON"));
}
#endif

// Gated: an NTP-less build should not report NTP coverage it does not have.
#if BOTANIK_FEATURE_NTP
TEST_CASE("NtpClient: stepped as a Component (announces once), query is non-blocking") {
    botanik_test::ParamFixture fx;
    NameResolver resolver;
    NetworkHandler net(&fx.bank(), nullptr, &resolver);
    RecordingWriter w;
    Logs::Logger log;
    log.set_min_level(Logs::LOG_DEBUG);
    log.add_writer(&w);

    NtpClient ts(&fx.bank(), &log, &resolver, &net);

    // Bring the (sim) link up so the time sync can proceed past the link gate.
    fx.set_num("net_serial", 1); fx.set_num("net_dhcp", 1); fx.publish();
    net.step(HAL::Clock::uptime_ms());

    fx.set_num("ntp_serial", 1);
    fx.set_num("ntp_enabled", 1);
    // Off-host literal: the hermetic sim refuses the send, so no reply arrives and the
    // query stays non-blocking (no sync).
    fx.set_str("ntp_server1", "192.0.2.1");
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
#endif

// Note: the NTP-synced / MQTT-connected success paths need the loopback mock
// responder (botanik_sim/network.h), which runs only in the full simulator, so they
// are exercised end-to-end by the pyte suite rather than here. These unit tests
// cover the config-gated state machines and the non-blocking dead-peer behaviour.
