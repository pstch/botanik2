/**
 * @file test_network.cpp
 * @brief Host-side tests for the network components. Their real work (DNS / UDP / TCP /
 *        device bring-up) is Mbed-only and compile-checked, not run here; what the host
 *        can observe is the config-gated state these expose:
 *          - NetworkHandler: the sim bring-up path (up on config, idle when cleared),
 *          - MqttClient:     never falsely "connected", publish refused when not,
 *          - TimeSyncHandler: it is stepped as a Component (first-step announce).
 *        Config is injected through PlcConfigFixture.
 */
#include <cstdint>
#include <cstring>
#include <string>

#include "doctest.h"

#include "plc_config_fixture.h"
#include "libbotanik/network/network.h"
#include "libbotanik/network/mqtt.h"
#include "libbotanik/network/resolver.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/hal/platform.h"

using namespace Botanik;
using namespace Botanik::Network;

namespace {
    // Records what the logger forwards, to observe a component's first-step announce.
    struct RecordingWriter : Logs::LogWriter {
        int  pushes = 0;
        char last_msg[Config::LOG_PLC_MESSAGE_LEN] = {0};
        void push(const PlcIO::LogEntry_t* e) override {
            pushes++; memcpy(last_msg, e->message, sizeof(last_msg));
        }
        void drain() override {}
        size_t format(const PlcIO::LogEntry_t*, char*, size_t) override { return 0; }
        size_t write(const char*, size_t) override { return 0; }
    };
}

TEST_CASE("NetworkHandler: sim path comes up on net config and idles when it is cleared") {
    botanik_test::PlcConfigFixture fx;
    NameResolver resolver;
    NetworkHandler net(fx.ctx(), nullptr, &resolver);
    CHECK_FALSE(net.is_up());

    // With Mbed absent, the sim bring-up path reports the link up immediately.
    fx.conf().net.serial = 1;
    fx.conf().net.dhcp   = 1;
    fx.publish();
    net.step(HAL::uptime_ms());
    CHECK(net.is_up());

    // Serial back to 0 (config removed) -> back to idle.
    fx.conf().net.serial = 0;
    fx.publish();
    net.step(HAL::uptime_ms());
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
    net.step(HAL::uptime_ms());                 // sim link up

    fx.conf().mqtt.serial  = 1;
    fx.conf().mqtt.enabled = 1;
    std::strncpy(fx.conf().mqtt.broker, "192.0.2.1", Config::PLC_HOST_STR_LEN - 1);  // TEST-NET-1
    fx.publish();
    for (int i = 0; i < 5; ++i) mqtt.step(HAL::uptime_ms());

    CHECK_FALSE(mqtt.connected());              // connect to 192.0.2.1 never completes
    CHECK_FALSE(mqtt.publish("botanik/alarms/X", "ON"));
}

TEST_CASE("TimeSyncHandler: stepped as a Component (announces once); query is non-blocking") {
    botanik_test::PlcConfigFixture fx;
    NameResolver resolver;
    NetworkHandler net(fx.ctx(), nullptr, &resolver);
    RecordingWriter w;
    Logs::Logger log;
    log.set_min_level(PlcIO::LOG_DEBUG);
    log.add_writer(&w);

    TimeSyncHandler ts(fx.ctx(), &log, &resolver, &net);

    // Bring the (sim) link up so the time sync can proceed past the link gate.
    fx.conf().net.serial = 1; fx.conf().net.dhcp = 1; fx.publish();
    net.step(HAL::uptime_ms());

    fx.conf().ntp.serial  = 1;
    fx.conf().ntp.enabled = 1;
    // TEST-NET-1 literal: a real UDP send goes nowhere, so the reply never arrives and
    // the query stays non-blocking (no DNS, no real NTP traffic).
    std::strncpy(fx.conf().ntp.server1, "192.0.2.1", Config::PLC_HOST_STR_LEN - 1);
    fx.publish();

    for (int i = 0; i < 5; ++i) ts.step(HAL::uptime_ms());   // must not fault or block

#ifdef DEBUG
    CHECK(w.pushes >= 1);
    CHECK(std::string(w.last_msg).find("TimeSyncHandler") != std::string::npos);
#endif
}
