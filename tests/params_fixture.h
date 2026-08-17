#ifndef BOTANIK_TEST_PARAMS_FIXTURE_H
#define BOTANIK_TEST_PARAMS_FIXTURE_H

/**
 * @file params_fixture.h
 * @brief A small bank of the config parameters the library components read
 *        (net / NTP / MQTT / geo). Lookup is BY NAME, so only the names must
 *        match - not the layout, and not the site's whole table.
 *
 * @warning The SPELLING is load-bearing and unchecked: a name the component does
 * not read resolves to the inert sink and reads 0, so a test asserting it did
 * nothing passes without reaching the code. Keep in step with `library_vars.h`.
 */

#include <cstdint>
#include <cstring>

#include "libbotanik/vars/declare.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/params.h"

namespace botanik_test {

    class ParamFixture {
        static const uint16_t CAP = 64;

        #define FX_NUM(nm, t, dflt) \
            Botanik::Vars::P(nm, t, dflt, 0, 1000000000, 0, "", 0, "", "")
        #define FX_STR(nm, slot) \
            Botanik::Vars::PS(nm, "", CAP, Botanik::Vars::STR_NONE, slot, "", "")
        #define FX_F32(nm) \
            Botanik::Vars::P(nm, Botanik::Vars::T_F32, 0, -180, 180, 4, "deg", 0, "", "")

        static const Botanik::Vars::Descriptor* table(uint16_t* n) {
            using namespace Botanik::Vars;
            static const Descriptor v[] = {
                FX_NUM("net_serial", T_U32, 0), FX_NUM("net_dhcp", T_BOOL, 1), FX_STR("net_ip", 0),
                FX_NUM("ntp_serial", T_U32, 0), FX_NUM("ntp_enable", T_BOOL, 1),
                FX_NUM("ntp_poll_sec", T_U16, 64), FX_STR("ntp_srv1", CAP),
                FX_NUM("mqtt_serial", T_U32, 0), FX_NUM("mqtt_enable", T_BOOL, 1),
                FX_NUM("mqtt_port", T_U16, 1883), FX_STR("mqtt_host", 2 * CAP),
                // A miss here reads 0, which silently puts the site on the
                // equator. See the @warning above.
                FX_NUM("geo_serial", T_U32, 0), FX_F32("geo_lat_deg"), FX_F32("geo_lon_deg"),
            };
            *n = (uint16_t)(sizeof(v) / sizeof(v[0]));
            return v;
        }
        #undef FX_NUM
        #undef FX_STR
        #undef FX_F32

        Botanik::Vars::Slot _vals[14]{};
        char                 _pool[3 * CAP]{};
        Botanik::Vars::Bank  _bank;

    public:
        ParamFixture() {
            using namespace Botanik::Vars;
            uint16_t n = 0;
            const Descriptor* v = table(&n);
            _bank.register_vars(v, n);
            _bank.freeze();
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
            // The bank is the only thing that hands out a pool pointer, and it
            // is the only thing that can prove the pointer is inside the pool.
            uint16_t cap = 0;
            char* dst = _bank.text(_bank.var(r), &cap);
            if (dst == nullptr) return;
            std::strncpy(dst, text, cap - 1);
            dst[cap - 1] = 0;
        }
        /** @} */

        /** @brief No-op: edits take effect immediately. */
        void publish() {}
    };

}  // namespace botanik_test

#endif // BOTANIK_TEST_PARAMS_FIXTURE_H
