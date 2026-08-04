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
        Scalar::U(0), "<none>", "", "", "",
        Payload(),
        { XP_NONE, T_U8, 0, 0, 0, 0, 1.0f, 0.0f },
        0, nullptr, T_U8, ROLE_COMPUTED, SCOPE_G, 0, 0, {}
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
        // Text rows are laid out first, in table order, so a slot's position is
        // derived rather than declared. Inserting a text parameter used to mean
        // re-typing every offset after it, and getting one wrong overlapped two
        // settings silently.
        _pool_used = 0;
        for (uint16_t i = 0; i < _t.count; i++) {
            const uint16_t cap = text_slot(_t.var[i]).cap;
            if (cap == 0) { _t.val[i].str_off = 0; continue; }
            if ((uint32_t)_pool_used + cap > _t.pool_bytes) {
                // Out of pool: leave the row with no slot rather than one that
                // runs off the end. cap is in the descriptor, so the row still
                // claims a slot - which is why the overflow is counted and the
                // site test asserts it is zero.
                _t.val[i].str_off = 0;
                _pool_overflow++;
                continue;
            }
            _t.val[i].str_off = _pool_used;
            _pool_used = (uint16_t)(_pool_used + cap);
        }

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

            const TextSlot slot = text_slot(d);
            if (_t.pool != nullptr && slot.cap != 0)
                copy_bounded(_t.pool + _t.val[i].str_off, slot.cap, d.def.s);
        }
    }

    void Bank::_find_param_block() {
        _param_first = 0;
        _param_count = 0;
        for (uint16_t i = 0; i < _t.count; i++) {
            if (_t.var[i].role != ROLE_PARAM) continue;
            _param_first = i;
            while (i < _t.count && _t.var[i].role == ROLE_PARAM) { _param_count++; i++; }
            return;
        }
    }

    VarRef Bank::find(const char* name) const {
        VarRef r = { VAR_NONE };
        if (name == nullptr) return r;
        for (uint16_t i = 0; i < _t.count; i++) {
            const char* n = _t.var[i].name;
            if (n != nullptr && strcmp(n, name) == 0) { r.index = i; return r; }
        }
        // Nothing addresses a row by number any more, so a miss here is a name
        // that exists in a table or a program but not in vars.h. Count it (and
        // say which) rather than letting it read as a dead variable forever.
        if (_unresolved < 0xFFFFu) _unresolved++;
        if (_logger != nullptr) _logger->error(Logs::BOOT, "unknown variable '%s'", name);
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
        for (uint16_t k = 0; k < _param_count; k++) {
            const uint16_t i = (uint16_t)(_param_first + k);
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
            // wants after whatever took control away in the first place. Wipe
            // the stored copy too, so a reboot cannot reopen the same hole.
            if (_t.val[i].ovr_on && _store != nullptr)
                (void)_store->clear_override(_t.var[i]);
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
            const NumBand band = num_band(d);
            if (parse_scalar(d.type, text, out) != PARSE_OK) return SET_INVALID;
            if (bounded && (d.flags & F_BOUNDED) != 0 &&
                !in_range(d.type, *out, band.min, band.max)) return SET_INVALID;
            return SET_OK;
        }
    }  // namespace

    SetResult Bank::set(Var h, const char* text) {
        const Variable* d = h.meta();
        if (text == nullptr || d->role != ROLE_PARAM) return SET_INVALID;
        if ((d->flags & F_RDONLY) != 0) return SET_INVALID;

        // The handle already points INTO the table, so its row is the offset -
        // no name scan. (It may instead point at the sink, which is what the
        // range check rejects; a Var is only ever one or the other.)
        if (_t.var == nullptr || d < _t.var || d >= _t.var + _t.count)
            return SET_INVALID;
        Value& slot = _t.val[d - _t.var];

        if (d->type == T_STR) {
            const TextSlot ts = text_slot(*d);
            if (_t.pool == nullptr || ts.cap == 0) return SET_INVALID;
            if (!validate_str(ts.kind, text, ts.cap)) return SET_INVALID;
            copy_bounded(_t.pool + slot.str_off, ts.cap, text);
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

    SetResult Bank::override_set(Var h, const char* text, bool valid) {
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
        override_set(h, parsed, valid);
        return SET_OK;
    }

    void Bank::override_set(Var h, Scalar v, bool valid) {
        h.override_set(v, valid);
        _persist_override(h);
    }

    void Bank::override_freeze(Var h) {
        h.override_freeze();
        _persist_override(h);
    }

    void Bank::override_clear(Var h) {
        h.override_clear();
        // Forget the stored copy in the same breath. A release that only cleared
        // RAM would come back at the next boot, which is the opposite of what
        // "override off" means.
        if (_store != nullptr && _row_of(h) != VAR_NONE)
            (void)_store->clear_override(*h.meta());
    }

    uint16_t Bank::_row_of(Var h) const {
        const Variable* d = h.meta();
        if (_t.var == nullptr || d < _t.var || d >= _t.var + _t.count) return VAR_NONE;
        return (uint16_t)(d - _t.var);
    }

    void Bank::_persist_override(Var h) {
        const uint16_t row = _row_of(h);
        if (_store == nullptr || row == VAR_NONE) return;
        const Variable* d = h.meta();
        // An actuator override is deliberately never written: see
        // load_overrides(). Nothing would restore it, so storing it would only
        // leave a stale record to puzzle over.
        if (d->role == ROLE_OUTPUT) return;
        if (!_store->save_override(*d, _t.val[row]) && _logger != nullptr)
            _logger->critical(Logs::STORAGE, "override on '%s' not persisted", d->name);
    }

    uint16_t Bank::load_overrides() {
        if (_store == nullptr) return 0;
        uint16_t n = 0;
        for (uint16_t i = 0; i < _t.count; i++) {
            const Variable& d = _t.var[i];
            if (!_store->load_override(d, _t.val[i])) continue;
            if (d.role == ROLE_OUTPUT) {
                // Found one on an actuator: drop it rather than restore it, so
                // it cannot fire when bring-up hands control back. Only reached
                // when a record actually exists - an unconditional remove per
                // output row was a flash transaction each boot for nothing.
                (void)_store->clear_override(d);
                _t.val[i].ovr_on = false;
                continue;
            }
            _t.val[i].ovr_on = true;
            n++;
            if (_logger != nullptr)
                _logger->warning(Logs::BOOT, "override restored on '%s'", d.name);
        }
        return n;
    }

    uint16_t Bank::clear_overrides() {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _t.count; i++) {
            if (!_t.val[i].ovr_on) continue;
            _t.val[i].ovr_on = false;
            if (_store != nullptr) (void)_store->clear_override(_t.var[i]);
            n++;
        }
        return n;
    }

    uint16_t Bank::override_count() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _t.count; i++) if (_t.val[i].ovr_on) n++;
        return n;
    }

}}  // namespace Botanik::Vars
