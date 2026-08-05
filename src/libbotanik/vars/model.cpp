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
        /** @brief FNV-1a over a NUL-terminated name. Small, no table, and it
         *  spreads the shared prefixes a variable table is full of ("grid_",
         *  "cli_", "alarm_") - which is exactly what the strcmp scan it replaces
         *  was worst at. */
        uint32_t name_hash(const char* s) {
            uint32_t h = 2166136261u;
            while (*s != 0) { h ^= (uint8_t)*s++; h *= 16777619u; }
            return h;
        }
        /** @brief The same over @p len bytes, for a shell word with no NUL.
         *  Must agree with the form above on the same characters. */
        uint32_t name_hash(const char* s, uint16_t len) {
            uint32_t h = 2166136261u;
            for (uint16_t i = 0; i < len; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
            return h;
        }
        /** @brief Whether row name @p n is @p name (bounded to @p len when
         *  @p len is non-zero). */
        bool name_eq(const char* n, const char* name, uint16_t len) {
            if (n == nullptr) return false;
            if (len == 0) return strcmp(n, name) == 0;
            return strncmp(n, name, len) == 0 && n[len] == 0;
        }

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
                // Out of pool. STR_NO_SLOT, not 0: offset 0 is the first text
                // row's slot, so parking an overflowed row there made the two
                // share bytes, and a pool smaller than one slot wrote past the
                // end of the array. text_at() refuses on the sentinel, so the
                // row reads empty and every write bounces.
                _t.val[i].str_off = STR_NO_SLOT;
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

            uint16_t cap = 0;
            char* text = text_at(Var(&d, &_t.val[i]), &cap);
            if (text != nullptr) copy_bounded(text, cap, d.def.s);
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

    char* Bank::text_at(Var h, uint16_t* cap_out) const {
        if (cap_out != nullptr) *cap_out = 0;
        const uint16_t cap = text_slot(*h.meta()).cap;
        const uint16_t off = h.str_off();
        if (_t.pool == nullptr || cap == 0 || off == STR_NO_SLOT) return nullptr;
        // Belt and braces: init() already bounded this, but this is the one
        // place a pool pointer is handed out, so it is the one place worth
        // proving it lands inside the array.
        if ((uint32_t)off + cap > _t.pool_bytes) return nullptr;
        if (cap_out != nullptr) *cap_out = cap;
        return _t.pool + off;
    }

    void Bank::_build_index() {
        _hashed = false;
        // Open addressing degrades badly as it fills, so keep real headroom: a
        // table past three quarters full is not worth indexing, and falling back
        // to the scan is at least honest about it (name_index_built() says so).
        if (_t.var == nullptr || _t.count == 0) return;
        if ((uint32_t)_t.count * 4u > (uint32_t)HASH_SLOTS * 3u) return;

        for (uint16_t s = 0; s < HASH_SLOTS; s++) _hash[s] = VAR_NONE;
        const uint16_t mask = (uint16_t)(HASH_SLOTS - 1);
        for (uint16_t i = 0; i < _t.count; i++) {
            const char* n = _t.var[i].name;
            if (n == nullptr) continue;
            uint16_t slot = (uint16_t)(name_hash(n) & mask);
            // The first row of a duplicated name wins, which is what the linear
            // scan did - a site with two rows of one name is a defect either
            // way, but it must not resolve differently depending on the index.
            while (_hash[slot] != VAR_NONE &&
                   !name_eq(_t.var[_hash[slot]].name, n, 0))
                slot = (uint16_t)((slot + 1) & mask);
            if (_hash[slot] == VAR_NONE) _hash[slot] = i;
        }
        _hashed = true;
    }

    uint16_t Bank::_lookup(const char* name, uint16_t len) const {
        if (!_hashed) {
            for (uint16_t i = 0; i < _t.count; i++)
                if (name_eq(_t.var[i].name, name, len)) return i;
            return VAR_NONE;
        }
        const uint16_t mask = (uint16_t)(HASH_SLOTS - 1);
        const uint32_t h = (len == 0) ? name_hash(name) : name_hash(name, len);
        for (uint16_t k = 0; k < HASH_SLOTS; k++) {
            const uint16_t i = _hash[(uint16_t)((h + k) & mask)];
            // An empty slot ends the probe: the index is never full (the load
            // factor above guarantees it), so a run of collisions is all a miss
            // ever costs.
            if (i == VAR_NONE) break;
            if (name_eq(_t.var[i].name, name, len)) return i;
        }
        return VAR_NONE;
    }

    VarRef Bank::find(const char* name) const {
        VarRef r = { VAR_NONE };
        if (name == nullptr) return r;
        r.index = _lookup(name, 0);
        if (r.index != VAR_NONE) return r;
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
        r.index = _lookup(name, len);
        return r;
    }

    void Bank::load_params() {
        if (_store == nullptr) return;
        for (uint16_t k = 0; k < _param_count; k++) {
            const uint16_t i = (uint16_t)(_param_first + k);
            // A missing key is normal: the parameter keeps the declared default
            // that init() already seeded.
            uint16_t cap = 0;
            char* text = text_at(var(i), &cap);
            (void)_store->load(_t.var[i], _t.val[i], text, cap);
        }
    }

    void Bank::enable_control(ControlInhibit source) {
        _inhibits &= (uint8_t)~(uint8_t)source;
        // Nothing to do on the way up: the outputs sit at their safe state and
        // the scan drives them from there on the next beat.
    }

    void Bank::disable_control(ControlInhibit source) {
        const uint8_t before = _inhibits;
        _inhibits |= (uint8_t)source;
        // Nothing new: this hold was already in force. Worth the early exit -
        // the loop's caller is the per-beat watchdog path, which re-asserts the
        // health hold every beat for as long as the fault lasts.
        if (before == _inhibits) return;

        // Drop to safe on the transition only. Control was live until now, so
        // this is the one moment the outputs need putting somewhere known; a
        // second inhibit arriving on top finds them already there.
        const bool to_safe = (before == 0);
        // Only the SYSTEM's withdrawal releases a hand-held output. The
        // operator's is how you take the plant by hand, and wiping the overrides
        // would leave nothing able to move an actuator at all - see the header.
        const bool release = (source == INHIBIT_HEALTH);
        if (!to_safe && !release) return;

        for (uint16_t i = 0; i < _t.count; i++) {
            if (_t.var[i].role != ROLE_OUTPUT) continue;
            if (release && _t.val[i].ovr_on) {
                // Wipe the stored copy too. An output override is not persisted
                // today, so this is belt and braces against one written by an
                // older build coming back at the next boot.
                if (_store != nullptr) (void)_store->clear_override(_t.var[i]);
                _t.val[i].ovr_on = false;
            }
            if (to_safe) _t.val[i].valid = false;
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

    uint16_t Bank::reject_implausible() {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _t.count; i++) {
            const Variable& d = _t.var[i];
            if (d.role != ROLE_INPUT || !_t.val[i].valid) continue;
            // An override is a maintainer deliberately pushing a reading past
            // its band to exercise what watches for it - the one case where an
            // implausible value is the point.
            if (_t.val[i].ovr_on) continue;

            bool bad = false;
            if (type_real(d.type)) {
                const float f = _t.val[i].v.f;
                bad = !(f == f) || f > 3.402823466e+38f || f < -3.402823466e+38f;
            }
            // SCOPE_AI/AO carry their band in parameters, not here; num_band()
            // returns unset for them, which would reject everything.
            if (!bad && (d.flags & F_BOUNDED) != 0 && payload_arm(d) == PAY_NUM) {
                const NumBand b = num_band(d);
                bad = !in_range(d.type, _t.val[i].v, b.min, b.max);
            }
            if (!bad) continue;
            _t.val[i].valid = false;
            n++;
        }
        return n;
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
            uint16_t cap = 0;
            char* dst = text_at(h, &cap);
            if (dst == nullptr) return SET_INVALID;
            if (!validate_str(text_slot(*d).kind, text, cap)) return SET_INVALID;
            copy_bounded(dst, cap, text);
            slot.valid = true;
        } else {
            Scalar parsed;
            if (parse_for(*d, text, /*bounded=*/true, &parsed) != SET_OK) return SET_INVALID;
            slot.v     = parsed;
            slot.valid = true;
        }

        if (_store == nullptr) return SET_NOT_PERSISTED;
        uint16_t save_cap = 0;
        char* save_text = text_at(h, &save_cap);
        if (!_store->save(*d, slot, save_text, save_cap)) {
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
        // An output is deliberately NOT refused while control is disabled: that
        // is the state in which the control programs have stopped deciding, so
        // an override is the only way left to command an actuator - which is
        // what `control disable` is for. It is live immediately, not staged, and
        // it is never persisted (see _persist_override), so there is no session
        // it can outlive.
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
                // Found one on an actuator: drop it rather than restore it - it
                // would be driving the plant from power-on, out of a session
                // nobody is standing at any more. Only reached when a record
                // actually exists - an unconditional remove per output row was a
                // flash transaction each boot for nothing.
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
