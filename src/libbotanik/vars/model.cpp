/**
 * @file model.cpp
 * @brief Bank: seeding, lookup, staleness expiry and the fail-safe sweep.
 *
 * Each is one loop with a role switch. The predecessor needed ten hand-written
 * calls naming six tables, which is where the drift lived: expire_stale covered
 * two of them and the fail-safe sweep another two, and nothing said why.
 */

#include "model.h"

#include <cstring>

namespace Botanik { namespace Vars {

    // Named so a bad index is obvious in a log or a debugger.
    const Variable Bank::_sink_d = {
        Scalar::U(0), Scalar::U(0), Scalar::U(0),
        "<none>", "", "", nullptr,
        { XP_NONE, T_U8, 0, 0, 0, 0, 1.0f, 0.0f },
        0, 0, 0, T_U8, ROLE_INTERNAL, 0, STR_NONE, 0, {}
    };
    Value Bank::_sink_v = {};

    namespace {
        /** @brief Copy @p src into @p dst, always NUL-terminating. */
        void copy_bounded(char* dst, uint16_t cap, const char* src) {
            if (dst == nullptr || cap == 0) return;
            if (src == nullptr) { dst[0] = 0; return; }
            uint16_t n = 0;
            while (src[n] != 0 && n + 1u < cap) { dst[n] = src[n]; n++; }
            dst[n] = 0;
        }
    }  // namespace

    void Bank::init() {
        for (uint16_t i = 0; i < _t.count; i++) {
            const Variable& d = _t.var[i];
            _t.val[i].v        = d.def;
            _t.val[i].stamp_ms = 0;
            // A parameter always HAS a value - its factory default until the
            // store loads one - so it starts valid. Everything else starts
            // invalid, so it reads as its default until something real writes
            // it, and the console can say "no data" rather than presenting a
            // default as a measurement.
            _t.val[i].valid    = (d.role == ROLE_PARAM);

            if (d.type == T_STR && _t.pool != nullptr && d.str_cap != 0 &&
                (uint32_t)d.str_off + d.str_cap <= _t.pool_bytes) {
                copy_bounded(_t.pool + d.str_off, d.str_cap, d.def.s);
            }
        }
    }

    VarRef Bank::find(const char* name) const {
        VarRef r = { VAR_NONE };
        if (name == nullptr) return r;
        for (uint16_t i = 0; i < _t.count; i++) {
            const char* n = _t.var[i].name;
            if (n != nullptr && strcmp(n, name) == 0) { r.index = i; return r; }
        }
        return r;
    }

    VarRef Bank::find(const char* name, uint16_t len) const {
        VarRef r = { VAR_NONE };
        if (name == nullptr || len == 0) return r;
        for (uint16_t i = 0; i < _t.count; i++) {
            const char* n = _t.var[i].name;
            if (n == nullptr) continue;
            if (strncmp(n, name, len) == 0 && n[len] == 0) { r.index = i; return r; }
        }
        return r;
    }

    void Bank::all_outputs_safe() {
        for (uint16_t i = 0; i < _t.count; i++)
            if (_t.var[i].role == ROLE_OUTPUT) _t.val[i].valid = false;
    }

    void Bank::expire_stale(uint64_t now_ms) {
        for (uint16_t i = 0; i < _t.count; i++) {
            const Variable& d = _t.var[i];
            if (d.role != ROLE_INPUT || d.timeout_ms == 0) continue;
            if (_t.val[i].valid && (now_ms - _t.val[i].stamp_ms) > d.timeout_ms)
                _t.val[i].valid = false;
        }
    }

}}  // namespace Botanik::Vars
