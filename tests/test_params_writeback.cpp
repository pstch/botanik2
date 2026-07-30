/**
 * @file test_config_writeback.cpp
 * @brief Full-loop test of the PLC config write-back with config as ordinary
 *        payload fields: the PLC store's set() stages an edit into the input
 *        payload mirror at the parameter's in_offset, the host TX ships it, the
 *        emulated PLC (played here, since group-apply is site logic now) latches
 *        a group when its commit serial advances and reflects it on the output,
 *        and the host RX surfaces the new value on image() by name.
 */
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "doctest.h"

#include "libbotanik/plcio/sync.h"
#include "botanik_sim/emulator.h"
#include "libbotanik/params/store_plc.h"
#include "plc_params_fixture.h"   // FixtureConfig + fixture_params

using namespace Botanik;
using namespace Botanik::PlcIO;

namespace {
    using botanik_test::FixtureConfig;
    struct CfgIn  { FixtureConfig config; };
    struct CfgOut { FixtureConfig config; };

    // Play the PLC's per-group config apply (a site's fill_output does this in
    // the real sim): for each group whose input commit serial advanced, adopt
    // its staged fields and reflect the serial on the output.
    void apply_config(CfgOut* out, const CfgIn* in) {
        FixtureConfig& oc = out->config;
        const FixtureConfig& ic = in->config;
        if (ic.net_serial && ic.net_serial != oc.net_serial) {
            strncpy(oc.net_ip, ic.net_ip, sizeof(oc.net_ip) - 1);
            oc.net_ip[sizeof(oc.net_ip) - 1] = '\0';
            oc.net_serial = ic.net_serial;
        }
        if (ic.ntp_serial && ic.ntp_serial != oc.ntp_serial) {
            oc.ntp_poll_s   = ic.ntp_poll_s;
            oc.ntp_enabled  = ic.ntp_enabled;
            oc.ntp_serial   = ic.ntp_serial;
        }
    }

    const Params::Param* find(Params::ParamHandler& h, const char* name) {
        return h.find(name, (uint16_t)strlen(name));
    }
}

TEST_CASE("PLC config write-back: host edit reflects via the emulated PLC") {
    PlcInput<CfgIn>  in{};
    PlcOutput<CfgOut> out{};
    uint32_t ts = 0;

    PlcSyncHandler<CfgIn, CfgOut> host(&in, &out, &ts, nullptr);
    PlcEmulator<CfgIn, CfgOut>    emu(&in, &out, &ts);
    Params::PlcParamStore           store(host.context());
    Params::ParamHandler            cfg(&store, nullptr);
    uint8_t np = 0;
    const Params::Param* fp = botanik_test::fixture_params(&np);
    cfg.set_site_params(fp, np);

    // The "PLC" publishes an initial config (NTP group serial 1, poll 64).
    emu.output_data()->config.ntp_serial  = 1;
    emu.output_data()->config.ntp_enabled = 1;
    emu.output_data()->config.ntp_poll_s  = 64;
    emu.output_data()->config.geo_serial   = 1;
    emu.output_data()->config.geo_latitude = 10.0f;
    emu.context()->write();

    auto pump = [&](int n) {
        for (int i = 0; i < n; i++) {
            ts += Config::PLC_HEARTBEAT_MS / 5 + 1;
            host.step(ts);
            emu.step();
            apply_config(emu.output_data(), emu.input_data());
            emu.context()->write();
        }
    };

    pump(40);
    REQUIRE(get_int(&cfg, "ntp_poll_s") == 64);      // initial config synced to host
    uint32_t ntp_serial0 = group_serial(&cfg, "ntp");

    // Host edits ntp_poll_s (stage), then bumps the "ntp" serial (commit).
    const Params::Param* poll = find(cfg, "ntp_poll_s");
    const Params::Param* ntp  = find(cfg, "ntp");
    REQUIRE(poll != nullptr);
    REQUIRE(ntp != nullptr);
    CHECK(cfg.set(*poll, "300"));
    CHECK(cfg.set(*ntp, "2"));

    pump(40);

    // The edit round-tripped: the PLC applied it and reflected it on the output,
    // and the NTP commit serial advanced (so the consumer re-applies).
    CHECK(get_int(&cfg, "ntp_poll_s") == 300);
    CHECK(group_serial(&cfg, "ntp") == 2);
    CHECK(group_serial(&cfg, "ntp") != ntp_serial0);

    // Untouched groups (geo) did not re-apply.
    CHECK(group_serial(&cfg, "geo") == 1);
    CHECK(get_f32(&cfg, "geo_latitude") == doctest::Approx(10.0f));

    // A second edit + commit round-trips the newly staged value. (Only staged
    // fields are applied - the transport does not preserve un-staged group
    // fields; a coherent group is the editor's responsibility.)
    CHECK(cfg.set(*poll, "128"));
    CHECK(cfg.set(*ntp, "3"));
    pump(40);
    CHECK(get_int(&cfg, "ntp_poll_s") == 128);
    CHECK(group_serial(&cfg, "ntp") == 3);
}

TEST_CASE("PLC config write-back: a string edit crosses the wire") {
    PlcInput<CfgIn>  in{};
    PlcOutput<CfgOut> out{};
    uint32_t ts = 0;
    PlcSyncHandler<CfgIn, CfgOut> host(&in, &out, &ts, nullptr);
    PlcEmulator<CfgIn, CfgOut>    emu(&in, &out, &ts);
    Params::PlcParamStore           store(host.context());
    Params::ParamHandler            cfg(&store, nullptr);
    uint8_t np = 0;
    const Params::Param* fp = botanik_test::fixture_params(&np);
    cfg.set_site_params(fp, np);

    emu.output_data()->config.net_serial = 1;
    strcpy(emu.output_data()->config.net_ip, "192.168.0.2");
    emu.context()->write();

    auto pump = [&](int n) {
        for (int i = 0; i < n; i++) {
            ts += Config::PLC_HEARTBEAT_MS / 5 + 1;
            host.step(ts); emu.step();
            apply_config(emu.output_data(), emu.input_data());
            emu.context()->write();
        }
    };
    pump(40);
    REQUIRE(strcmp(get_str(&cfg, "net_ip"), "192.168.0.2") == 0);

    const Params::Param* ip  = find(cfg, "net_ip");
    const Params::Param* net = find(cfg, "net");
    REQUIRE(ip != nullptr);
    REQUIRE(net != nullptr);
    CHECK(cfg.set(*ip, "10.1.2.3"));
    CHECK(cfg.set(*net, "2"));
    pump(40);
    CHECK(strcmp(get_str(&cfg, "net_ip"), "10.1.2.3") == 0);
}
