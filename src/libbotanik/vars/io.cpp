/**
 * @file io.cpp
 * @brief Io: default seeding, staleness expiry, and the fail-safe sweep.
 */

#include "io.h"

#include <cstdlib>   // strtof

namespace Botanik { namespace Vars {

    namespace {
        /** @brief Parse a declared default as a float; 0 when absent or
         *  unparseable. Runs once at init, never in the scan. */
        float def_f(const char* text) {
            if (text == nullptr || text[0] == '\0') return 0.0f;
            char* end = nullptr;
            float v = strtof(text, &end);
            // Reject trailing junk: a malformed default should read as 0 rather
            // than as whatever prefix happened to parse.
            return (end != nullptr && *end == '\0') ? v : 0.0f;
        }
        /** @brief Parse a declared default as a boolean. Accepts the numeric
         *  form the tables use ("0"/"1"); anything non-zero is true. */
        bool def_b(const char* text) {
            if (text == nullptr || text[0] == '\0') return false;
            return !(text[0] == '0' && text[1] == '\0');
        }
    }

    void Io::init() {
        for (uint16_t i = 0; i < _t.ai_n; i++) {
            _t.ai_v[i].def   = def_f(_t.ai[i].var.def);
            _t.ai_v[i].value = _t.ai_v[i].def;
            _t.ai_v[i].valid = false;      // nothing has read it yet
        }
        for (uint16_t i = 0; i < _t.ao_n; i++) {
            _t.ao_v[i].def   = def_f(_t.ao[i].var.def);
            _t.ao_v[i].value = _t.ao_v[i].def;
            _t.ao_v[i].valid = false;      // safe state until logic demands one
        }
        for (uint16_t i = 0; i < _t.di_n; i++) {
            _t.di_v[i].def   = def_b(_t.di[i].var.def);
            _t.di_v[i].value = _t.di_v[i].def;
            _t.di_v[i].valid = false;
        }
        for (uint16_t i = 0; i < _t.do_n; i++) {
            _t.do_v[i].def   = def_b(_t.dout[i].var.def);
            _t.do_v[i].value = _t.do_v[i].def;
            _t.do_v[i].valid = false;
        }
    }

    void Io::all_outputs_safe() {
        for (uint16_t i = 0; i < _t.ao_n; i++) {
            _t.ao_v[i].value = _t.ao_v[i].def;
            _t.ao_v[i].valid = false;
        }
        for (uint16_t i = 0; i < _t.do_n; i++) {
            _t.do_v[i].value = _t.do_v[i].def;
            _t.do_v[i].valid = false;
        }
    }

    void Io::expire_stale(uint64_t now_ms) {
        // A timeout of 0 means "never expires" - useful for a value a driver
        // publishes only on change.
        for (uint16_t i = 0; i < _t.ai_n; i++) {
            uint32_t t = _t.ai[i].timeout_ms;
            if (t != 0 && _t.ai_v[i].valid && (now_ms - _t.ai_v[i].timestamp) > t)
                _t.ai_v[i].valid = false;
        }
        for (uint16_t i = 0; i < _t.di_n; i++) {
            uint32_t t = _t.di[i].timeout_ms;
            if (t != 0 && _t.di_v[i].valid && (now_ms - _t.di_v[i].timestamp) > t)
                _t.di_v[i].valid = false;
        }
    }

}}  // namespace Botanik::Vars
