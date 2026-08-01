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

#include "../logs/logger.h"   // a refused persist is a fault, not a shrug

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
            // Overrides are a live-session tool; a fresh table has none.
            _t.val[i].ovr_on   = false;
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

    void Bank::load_params() {
        if (_store == nullptr) return;
        for (uint16_t k = 0; k < _t.param_count; k++) {
            const uint16_t i = (uint16_t)(_t.param_first + k);
            // A missing key is normal: the parameter keeps the declared default
            // that init() already seeded.
            (void)_store->load(_t.var[i], _t.val[i], _t.pool);
        }
    }

    void Bank::enable_control(ControlInhibit source) {
        _inhibits &= (uint8_t)~(uint8_t)source;
        // Nothing to do on the way up: the outputs sit at their safe state and
        // the scan drives them from there on the next beat.
    }

    void Bank::disable_control(ControlInhibit source) {
        const bool was_enabled = control_enabled();
        _inhibits |= (uint8_t)source;
        // The sweep runs on the transition only: if control was already
        // withheld the outputs are at their safe state and nothing has been able
        // to move them since.
        if (!was_enabled) return;
        for (uint16_t i = 0; i < _t.count; i++) {
            if (_t.var[i].role != ROLE_OUTPUT) continue;
            // Released, not merely ignored: an override left staged here would
            // fire the instant control came back, which is the last thing anyone
            // wants after whatever took control away in the first place.
            _t.val[i].ovr_on = false;
            _t.val[i].valid  = false;
        }
    }

    void Bank::expire_stale(uint64_t now_ms) {
        for (uint16_t i = 0; i < _t.count; i++) {
            const Variable& d = _t.var[i];
            if (d.role != ROLE_INPUT || d.timeout_ms == 0) continue;
            if (_t.val[i].valid && (now_ms - _t.val[i].stamp_ms) > d.timeout_ms)
                _t.val[i].valid = false;
        }
    }

    // Writing
    // ------------------------------------------------------------------------

    namespace {
        /** @brief Parse @p text as @p d's type into @p out, optionally holding
         *  it to the declared band. Shared by set() and override_set(), which
         *  differ only in whether the band applies. */
        SetResult parse_for(const Variable& d, const char* text, bool bounded,
                            Scalar* out) {
            if (parse_scalar(d.type, text, out) != PARSE_OK) return SET_INVALID;
            if (bounded && (d.flags & F_BOUNDED) != 0 &&
                !in_range(d.type, *out, d.min, d.max)) return SET_INVALID;
            return SET_OK;
        }
    }  // namespace

    SetResult Bank::set(Var h, const char* text) {
        const Variable* d = h.meta();
        if (text == nullptr || d->role != ROLE_PARAM) return SET_INVALID;
        if ((d->flags & F_RDONLY) != 0) return SET_INVALID;

        const uint16_t i = find(d->name).index;
        if (i >= _t.count) return SET_INVALID;
        Value& slot = _t.val[i];

        if (d->type == T_STR) {
            if (_t.pool == nullptr || d->str_cap == 0) return SET_INVALID;
            if (!validate_str(d->kind, text, d->str_cap)) return SET_INVALID;
            copy_bounded(_t.pool + d->str_off, d->str_cap, text);
            slot.valid = true;
        } else {
            Scalar parsed;
            if (parse_for(*d, text, /*bounded=*/true, &parsed) != SET_OK) return SET_INVALID;
            slot.v     = parsed;
            slot.valid = true;
        }

        if (_store == nullptr) return SET_NOT_PERSISTED;
        if (!_store->save(*d, slot, _t.pool)) {
            // Loud: the value is live, so nothing looks wrong until the reboot
            // that quietly reverts it.
            if (_logger != nullptr)
                _logger->critical(Logs::STORAGE, "param '%s' not persisted", d->name);
            return SET_NOT_PERSISTED;
        }
        return SET_OK;
    }

    SetResult Bank::override_set(Var h, const char* text) {
        const Variable* d = h.meta();
        // A T_STR's live text lives in the pool, not the slot, so there is
        // nothing for an override to shadow - and a hostname is configuration
        // anyway, which is set()'s business.
        if (text == nullptr || d->type == T_STR) return SET_INVALID;
        // Control disabled: refuse to arm an actuator. The alternative - accept
        // it and hold it - is a command that fires later, from a console session
        // long since walked away from.
        if (d->role == ROLE_OUTPUT && !control_enabled()) return SET_INVALID;
        Scalar parsed;
        // Band deliberately NOT applied - see the header note.
        if (parse_for(*d, text, /*bounded=*/false, &parsed) != SET_OK) return SET_INVALID;
        h.override_set(parsed);
        return SET_OK;
    }

    uint16_t Bank::clear_overrides() {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _t.count; i++)
            if (_t.val[i].ovr_on) { _t.val[i].ovr_on = false; n++; }
        return n;
    }

    uint16_t Bank::override_count() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _t.count; i++) if (_t.val[i].ovr_on) n++;
        return n;
    }

}}  // namespace Botanik::Vars
