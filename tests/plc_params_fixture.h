#ifndef TESTS_PLC_PARAMS_FIXTURE_H
#define TESTS_PLC_PARAMS_FIXTURE_H

/**
 * @file plc_config_fixture.h
 * @brief Test helper: drive a real PlcSyncHandler over throwaway shared regions
 *        so a test can populate the PLC->host config values and read them back
 *        through a PlcParamHandler - the exact path the live components consume.
 *
 * Config parameters are ordinary payload fields now, so the fixture defines a
 * small config payload (net / NTP / geo) carried in both the input and output
 * regions, and registers a matching parameter table on the handler. Set fields
 * via conf(), then publish() pushes them through one RX transaction so the
 * handler's image() reflects them.
 */

#include <cstddef>
#include <cstdint>

#include "libbotanik/plcio/sync.h"
#include "libbotanik/params/store_plc.h"

namespace botanik_test {

    /** @brief Config payload carried by both PLC regions (a site stand-in),
     *  covering the net / NTP / MQTT / geo the library components read. Each
     *  group's commit serial is an ordinary u32 field named after the group. */
    struct FixtureConfig {
        float    geo_latitude;
        float    geo_longitude;
        uint32_t net_serial;
        uint32_t ntp_serial;
        uint32_t mqtt_serial;
        uint32_t geo_serial;
        uint16_t ntp_poll_s;
        uint8_t  net_dhcp;
        uint8_t  ntp_enabled;
        uint8_t  mqtt_enabled;
        uint8_t  _pad[3];
        char     net_ip[64];
        char     ntp_server1[64];
        char     mqtt_broker[64];
    };

    // Parameter table over FixtureConfig (out_offset == in_offset here: the two
    // regions share the layout). The group name doubles as its serial parameter.
#define FX_OFF(f) (int16_t)offsetof(FixtureConfig, f)
    inline const Botanik::Params::Param* fixture_params(uint8_t* count) {
        using namespace Botanik;
        using namespace Botanik::Params;
        static const Param params[] = {
            {{"net",          FX_OFF(net_serial),   FX_OFF(net_serial),   SCALAR_U32,    0, nullptr, "",  "commit",""}, {"0",0,1000000000,0, STR_NONE, "net"}},
            {{"net_dhcp",     FX_OFF(net_dhcp),     FX_OFF(net_dhcp),     SCALAR_U8,     0, nullptr, "",  "dhcp",""},   {"1",0,1,0, STR_NONE, "net"}},
            {{"net_ip",       FX_OFF(net_ip),       FX_OFF(net_ip),       SCALAR_STRING, 0, nullptr, "",  "ip",""},     {"",0,0,64, STR_IPV4, "net"}},
            {{"ntp",          FX_OFF(ntp_serial),   FX_OFF(ntp_serial),   SCALAR_U32,    0, nullptr, "",  "commit",""}, {"0",0,1000000000,0, STR_NONE, "ntp"}},
            {{"ntp_enabled",  FX_OFF(ntp_enabled),  FX_OFF(ntp_enabled),  SCALAR_U8,     0, nullptr, "",  "en",""},     {"1",0,1,0, STR_NONE, "ntp"}},
            {{"ntp_poll_s",   FX_OFF(ntp_poll_s),   FX_OFF(ntp_poll_s),   SCALAR_U16,    0, nullptr, "s", "poll",""},   {"64",0,65535,0, STR_NONE, "ntp"}},
            {{"ntp_server1",  FX_OFF(ntp_server1),  FX_OFF(ntp_server1),  SCALAR_STRING, 0, nullptr, "",  "srv",""},    {"",0,0,64, STR_HOST, "ntp"}},
            {{"mqtt",         FX_OFF(mqtt_serial),  FX_OFF(mqtt_serial),  SCALAR_U32,    0, nullptr, "",  "commit",""}, {"0",0,1000000000,0, STR_NONE, "mqtt"}},
            {{"mqtt_enabled", FX_OFF(mqtt_enabled), FX_OFF(mqtt_enabled), SCALAR_U8,     0, nullptr, "",  "en",""},     {"1",0,1,0, STR_NONE, "mqtt"}},
            {{"mqtt_broker",  FX_OFF(mqtt_broker),  FX_OFF(mqtt_broker),  SCALAR_STRING, 0, nullptr, "",  "brk",""},    {"",0,0,64, STR_HOST, "mqtt"}},
            {{"geo",          FX_OFF(geo_serial),   FX_OFF(geo_serial),   SCALAR_U32,    0, nullptr, "",  "commit",""}, {"0",0,1000000000,0, STR_NONE, "geo"}},
            {{"geo_latitude", FX_OFF(geo_latitude), FX_OFF(geo_latitude), SCALAR_F32,    4, nullptr, "deg","lat",""},   {"48.8566",-90,90,0, STR_NONE, "geo"}},
            {{"geo_longitude",FX_OFF(geo_longitude),FX_OFF(geo_longitude),SCALAR_F32,    4, nullptr, "deg","lon",""},   {"2.3522",-180,180,0, STR_NONE, "geo"}},
        };
        *count = (uint8_t)(sizeof(params) / sizeof(params[0]));
        return params;
    }
#undef FX_OFF

    struct PlcConfigFixture {
        struct In  { FixtureConfig config; };
        struct Out { FixtureConfig config; };

        Botanik::PlcIO::PlcInput<In>            in{};
        Botanik::PlcIO::PlcOutput<Out>          out{};
        uint32_t                                ts = 0;
        Botanik::PlcIO::PlcSyncHandler<In, Out> handler{&in, &out, &ts, nullptr};
        /** @brief A PLC-backed store + the generic handler over it, mirroring how
         *  BotanikFirmware wires the pair (core/firmware.h). */
        Botanik::Params::PlcParamStore          store{handler.context()};
        Botanik::Params::ParamHandler           cfg{&store, nullptr};

        PlcConfigFixture() {
            uint8_t n = 0;
            const Botanik::Params::Param* p = fixture_params(&n);
            cfg.set_site_params(p, n);
        }

        /** @brief The config the mock PLC publishes. Set fields, then publish(). */
        FixtureConfig& conf() { return out.data.config; }

        /** @brief Push the current conf into the handler's RX snapshot via one
         *  PLC->host transaction: bump the PLC sequence, then run the RX machine
         *  through IDLE -> READ (copies the payload) -> ACK. */
        void publish() {
            out.sync.txSeqNum += 1;               // signal a fresh PLC message
            for (int i = 0; i < 4; ++i) handler.step(0);   // now_ms unused by the PLC handler
        }

        /** @brief The populated context to hand a component under test. */
        Botanik::PlcIO::PlcSyncContext* ctx() { return handler.context(); }
    };

}  // namespace botanik_test

#endif // TESTS_PLC_PARAMS_FIXTURE_H
