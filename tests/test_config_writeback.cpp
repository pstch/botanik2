/**
 * @file test_config_writeback.cpp
 * @brief Full-loop test of the PLC config write-back: PlcConfigHandler.set()
 *        stages the input mirror, the host TX ships it, the emulated PLC applies
 *        the group and reflects it on the output serial, and the host RX surfaces
 *        the new value on image() with the group serial bumped once.
 */
#include <cstdint>
#include <cstring>

#include "doctest.h"

#include "libbotanik/plcio/sync.h"
#include "botanik_sim/emulator.h"
#include "libbotanik/core/config_plc.h"

using namespace Botanik;
using namespace Botanik::PlcIO;

namespace {
    struct Empty {};

    const Cfg::ConfigParam* find(const char* name) {
        for (uint8_t i = 0; i < Cfg::CONFIG_PARAM_COUNT; i++)
            if (strcmp(Cfg::CONFIG_PARAMS[i].name, name) == 0) return &Cfg::CONFIG_PARAMS[i];
        return nullptr;
    }
}

TEST_CASE("PLC config write-back: host edit reflects via the emulated PLC") {
    PlcInput<Empty>  in{};
    PlcOutput<Empty> out{};
    uint32_t ts = 0;

    PlcSyncHandler<Empty, Empty> host(&in, &out, &ts, nullptr);
    PlcEmulator<Empty, Empty>    emu(&in, &out, &ts);
    Cfg::PlcConfigHandler        cfg(host.context(), nullptr);

    // The "PLC" publishes an initial config (NTP group serial 1, poll 64).
    PlcOutputConfig_t init{};
    init.ntp_serial  = 1;
    init.ntp_enabled = 1;
    init.ntp_poll_s  = 64;
    init.geo_serial  = 1;
    init.geo_latitude_e7 = 100000000;   // 10.0 deg
    emu.publish_config(init);

    // Pump both state machines, advancing the shared clock so heartbeats fire.
    auto pump = [&](int n) {
        for (int i = 0; i < n; i++) {
            ts += Config::PLC_HEARTBEAT_MS / 5 + 1;
            host.step(ts);
            emu.step();
        }
    };

    pump(40);
    REQUIRE(cfg.image()->ntp_poll_s == 64);      // initial config synced to host
    uint32_t ntp_serial0 = cfg.image()->ntp_serial;

    // Host edits ntp_poll_s. set() stages the mirror + requests a TX write.
    const Cfg::ConfigParam* poll = find("ntp_poll_s");
    REQUIRE(poll != nullptr);
    CHECK(cfg.set(*poll, "300"));

    pump(40);

    // The edit round-tripped: the PLC applied it and reflected it on the output,
    // and the NTP group serial advanced exactly once (so the consumer re-applies).
    CHECK(cfg.image()->ntp_poll_s == 300);
    CHECK(cfg.image()->ntp_serial == ntp_serial0 + 1);

    // Untouched groups (geo) did not re-apply.
    CHECK(cfg.image()->geo_serial == 1);
    CHECK(cfg.image()->geo_latitude_e7 == 100000000);

    // A second edit bumps the serial again and keeps other NTP fields intact.
    CHECK(cfg.set(*poll, "128"));
    pump(40);
    CHECK(cfg.image()->ntp_poll_s == 128);
    CHECK(cfg.image()->ntp_enabled == 1);        // preserved across the write
    CHECK(cfg.image()->ntp_serial == ntp_serial0 + 2);
}

TEST_CASE("PLC config write-back: a string edit crosses the wire") {
    PlcInput<Empty>  in{};
    PlcOutput<Empty> out{};
    uint32_t ts = 0;
    PlcSyncHandler<Empty, Empty> host(&in, &out, &ts, nullptr);
    PlcEmulator<Empty, Empty>    emu(&in, &out, &ts);
    Cfg::PlcConfigHandler        cfg(host.context(), nullptr);

    PlcOutputConfig_t init{};
    init.net_serial = 1;
    strcpy(init.net_ip, "192.168.0.2");
    emu.publish_config(init);

    auto pump = [&](int n) {
        for (int i = 0; i < n; i++) { ts += Config::PLC_HEARTBEAT_MS / 5 + 1; host.step(ts); emu.step(); }
    };
    pump(40);
    REQUIRE(strcmp(cfg.image()->net_ip, "192.168.0.2") == 0);

    const Cfg::ConfigParam* ip = find("net_ip");
    REQUIRE(ip != nullptr);
    CHECK(cfg.set(*ip, "10.1.2.3"));
    pump(40);
    CHECK(strcmp(cfg.image()->net_ip, "10.1.2.3") == 0);
}
