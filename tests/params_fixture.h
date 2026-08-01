#ifndef BOTANIK_TEST_PARAMS_FIXTURE_H
#define BOTANIK_TEST_PARAMS_FIXTURE_H

/**
 * @file params_fixture.h
 * @brief A small variable bank carrying the config parameters the library
 *        components read (net / NTP / MQTT / geo), for the tests that drive
 *        those components.
 *
 * The components look their values up BY NAME, so this only has to declare the
 * same names - not the same layout, and not the site's whole table.
 */

#include <cstdint>
#include <cstring>

#include "libbotanik/vars/model.h"
#include "libbotanik/vars/params.h"

namespace botanik_test {

    class ParamFixture {
        static const uint16_t CAP = 64;

        #define FX_NUM(nm, t, dflt) \
            { Botanik::Vars::Scalar::U(0), Botanik::Vars::Scalar::U(1000000000), \
              Botanik::Vars::Scalar::U(dflt), nm, "", "", nullptr, nullptr, \
              { Botanik::Vars::XP_NONE, t, 0, 0, 0, 0, 1.0f, 0.0f }, \
              0, 0, 0, t, Botanik::Vars::ROLE_PARAM, 0, Botanik::Vars::STR_NONE, \
              Botanik::Vars::F_BOUNDED, {} }
        #define FX_STR(nm, off) \
            { Botanik::Vars::Scalar::U(0), Botanik::Vars::Scalar::U(0), \
              Botanik::Vars::Scalar::S(""), nm, "", "", nullptr, nullptr, \
              { Botanik::Vars::XP_NONE, Botanik::Vars::T_STR, 0, 0, 0, 0, 1.0f, 0.0f }, \
              0, off, CAP, Botanik::Vars::T_STR, Botanik::Vars::ROLE_PARAM, 0, \
              Botanik::Vars::STR_NONE, 0, {} }
        #define FX_F32(nm) \
            { Botanik::Vars::Scalar::F(-180.0f), Botanik::Vars::Scalar::F(180.0f), \
              Botanik::Vars::Scalar::F(0.0f), nm, "deg", "", nullptr, nullptr, \
              { Botanik::Vars::XP_NONE, Botanik::Vars::T_F32, 0, 0, 0, 0, 1.0f, 0.0f }, \
              0, 0, 0, Botanik::Vars::T_F32, Botanik::Vars::ROLE_PARAM, 4, \
              Botanik::Vars::STR_NONE, Botanik::Vars::F_BOUNDED, {} }

        static const Botanik::Vars::Variable* table(uint16_t* n) {
            using namespace Botanik::Vars;
            static const Variable v[] = {
                FX_NUM("net_serial", T_U32, 0), FX_NUM("net_dhcp", T_BOOL, 1), FX_STR("net_ip", 0),
                FX_NUM("ntp_serial", T_U32, 0), FX_NUM("ntp_enabled", T_BOOL, 1),
                FX_NUM("ntp_poll_s", T_U16, 64), FX_STR("ntp_server1", CAP),
                FX_NUM("mqtt_serial", T_U32, 0), FX_NUM("mqtt_enabled", T_BOOL, 1),
                FX_NUM("mqtt_port", T_U16, 1883), FX_STR("mqtt_broker", 2 * CAP),
                FX_NUM("geo_serial", T_U32, 0), FX_F32("geo_latitude"), FX_F32("geo_longitude"),
            };
            *n = (uint16_t)(sizeof(v) / sizeof(v[0]));
            return v;
        }
        #undef FX_NUM
        #undef FX_STR
        #undef FX_F32

        Botanik::Vars::Value _vals[14]{};
        char                 _pool[3 * CAP]{};
        Botanik::Vars::Bank  _bank;

    public:
        ParamFixture() {
            using namespace Botanik::Vars;
            uint16_t n = 0;
            const Variable* v = table(&n);
            VarTables t = {};
            t.var = v; t.val = _vals; t.count = n;
            t.pool = _pool; t.pool_bytes = (uint16_t)sizeof(_pool);
            t.param_first = 0; t.param_count = n;
            _bank = Bank(t);
            _bank.init();
        }

        /** @brief What the components under test are handed. */
        Botanik::Vars::Bank& bank() { return _bank; }

        /** @name Drive a value directly, as if an operator had committed it
         *  @{ */
        void set_num(const char* name, double v) {
            Botanik::Vars::Var h = _bank.var(_bank.find(name));
            h.set((float)v);
        }
        void set_str(const char* name, const char* text) {
            using namespace Botanik::Vars;
            const VarRef r = _bank.find(name);
            if (r.index == VAR_NONE) return;
            const Variable* d = _bank.var(r).meta();
            std::strncpy(_pool + d->str_off, text, CAP - 1);
            _pool[d->str_off + CAP - 1] = 0;
        }
        /** @} */

        /** @brief No-op: edits take effect immediately. Kept so the tests read
         *  the way they did against the old fixture. */
        void publish() {}
    };

}  // namespace botanik_test

#endif // BOTANIK_TEST_PARAMS_FIXTURE_H
