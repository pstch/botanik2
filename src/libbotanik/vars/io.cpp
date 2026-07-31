/**
 * @file io.cpp
 * @brief Io: default seeding, staleness expiry, and the fail-safe sweep.
 */

#include "io.h"

#include <cstdlib>   // strtof

namespace Botanik { namespace Vars {

    // The inert sink out-of-range accesses resolve to. Named so it is obvious
    // in a log or a debugger that a bad index was used.
    const Variable Io::_sink_desc = { "<none>", "", "", "", "0", 0, nullptr };
    AnalogValue    Io::_sink_a = {};
    DigitalValue   Io::_sink_d = {};

    namespace {
        /** @brief Parse a declared default as a float; 0 when absent or
         *  unparseable. Runs once at init, never in the scan. */
        float def_f(const char* text) {
            if (text == nullptr || text[0] == '\0') return 0.0f;
            char* end = nullptr;
            float v = strtof(text, &end);
            // Reject trailing junk: a malformed default should read as 0 rather
            // than as whatever prefix happened to parse.
            if (end == nullptr) return 0.0f;
            while (*end == ' ' || *end == '\t') end++;   // as params parsing does
            return (*end == '\0') ? v : 0.0f;
        }
        /** @brief Parse a declared default as a boolean. Accepts the numeric
         *  form the tables use ("0"/"1"); anything non-zero is true. */
        bool def_b(const char* text) {
            if (text == nullptr || text[0] == '\0') return false;
            return !(text[0] == '0' && text[1] == '\0');
        }
    }

    namespace {
        /** @brief The common part of any descriptor. An internal's descriptor
         *  IS the Variable (it has nothing to add), so the two overloads let
         *  one template walk every table. By pointer, not reference: returning
         *  a const reference to a parameter invites a use-after-free if the
         *  argument is ever a temporary. */
        inline const Variable* var_of(const Variable* v) { return v; }
        template <typename Desc>
        inline const Variable* var_of(const Desc* d) { return &d->var; }

        /** @brief Seed one table: parse each declared default into its slot and
         *  start invalid, so a variable reads as its default until something
         *  gives it a real value. */
        template <typename Desc, typename Val, typename Parse>
        void seed(const Desc* d, Val* v, uint16_t n, Parse parse) {
            for (uint16_t i = 0; i < n; i++) {
                v[i].def   = parse(var_of(&d[i])->def);
                v[i].value = v[i].def;
                v[i].valid = false;
            }
        }

        /** @brief Drop one output table to its declared safe state. */
        template <typename Val>
        void to_safe(Val* v, uint16_t n) {
            for (uint16_t i = 0; i < n; i++) { v[i].value = v[i].def; v[i].valid = false; }
        }

        /** @brief Invalidate inputs whose last good read is older than their
         *  declared timeout. A timeout of 0 means "never expires", for a value
         *  a driver publishes only on change. */
        template <typename Desc, typename Val>
        void expire(const Desc* d, Val* v, uint16_t n, uint64_t now_ms) {
            for (uint16_t i = 0; i < n; i++) {
                uint32_t t = d[i].timeout_ms;
                if (t != 0 && v[i].valid && (now_ms - v[i].timestamp) > t) v[i].valid = false;
            }
        }
    }

    void Io::init() {
        seed(_t.ai,   _t.ai_v, _t.ai_n, def_f);
        seed(_t.ao,   _t.ao_v, _t.ao_n, def_f);
        seed(_t.di,   _t.di_v, _t.di_n, def_b);
        seed(_t.dout, _t.do_v, _t.do_n, def_b);
        seed(_t.ia,   _t.ia_v, _t.ia_n, def_f);
        seed(_t.id,   _t.id_v, _t.id_n, def_b);
    }

    void Io::all_outputs_safe() {
        to_safe(_t.ao_v, _t.ao_n);
        to_safe(_t.do_v, _t.do_n);
    }

    void Io::expire_stale(uint64_t now_ms) {
        expire(_t.ai, _t.ai_v, _t.ai_n, now_ms);
        expire(_t.di, _t.di_v, _t.di_n, now_ms);
    }

}}  // namespace Botanik::Vars
