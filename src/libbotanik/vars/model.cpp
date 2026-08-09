/**
 * @file model.cpp
 * @brief Bank: seeding, lookup, staleness expiry and the fail-safe sweep.
 *
 * Each is one loop with a role switch. The predecessor needed ten hand-written
 * calls naming six tables, which is where the drift lived: expire_stale covered
 * two of them and the fail-safe sweep another two, and nothing said why.
 */

#include "libbotanik/vars/model.h"  // Bank / Var / Variable

#include <cstring>                  // strcmp / strncmp

#include "libbotanik/logs/logger.h" // Logs::Logger

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
        /** @brief Three-way compare of row name @p n against the key @p key,
         *  bounded to @p len characters when @p len is non-zero (a shell word
         *  has no NUL of its own).
         *
         *  The bounded arm is the same order as the unbounded one: with the
         *  first @p len characters equal, a longer row name sorts after the key,
         *  because the key ends there. That is what lets one sorted mapping
         *  serve both find() overloads.
         *
         *  A nameless row sorts after everything and matches nothing, so it
         *  simply falls off the end of the search. */
        int cmp_name(const char* n, const char* key, uint16_t len) {
            if (n == nullptr)   return (key == nullptr) ? 0 : 1;
            if (key == nullptr) return -1;
            if (len == 0) return strcmp(n, key);
            const int c = strncmp(n, key, len);
            if (c != 0) return c;
            return (n[len] == 0) ? 0 : 1;
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
        for (uint16_t i = 0; i < _terminal.count; i++) {
            const uint16_t cap = text_slot(_terminal.var[i]).cap;
            if (cap == 0) { _terminal.val[i].str_off = 0; continue; }
            if ((uint32_t)_pool_used + cap > _terminal.pool_bytes) {
                // Out of pool. STR_NO_SLOT, not 0: offset 0 is the first text
                // row's slot, so parking an overflowed row there made the two
                // share bytes, and a pool smaller than one slot wrote past the
                // end of the array. text_at() refuses on the sentinel, so the
                // row reads empty and every write bounces.
                _terminal.val[i].str_off = STR_NO_SLOT;
                _pool_overflow++;
                continue;
            }
            _terminal.val[i].str_off = _pool_used;
            _pool_used = (uint16_t)(_pool_used + cap);
        }

        for (uint16_t i = 0; i < _terminal.count; i++) {
            const Variable& d = _terminal.var[i];
            _terminal.val[i].v        = d.def;
            _terminal.val[i].stamp_ms = 0;
            // Overrides are a live-session tool; a fresh table has none.
            _terminal.val[i].ovr_on   = false;
            // A parameter always HAS a value - its factory default until the
            // store loads one - so it starts good. Everything else starts with
            // nothing written, so it reads as its default and the console says
            // so rather than presenting a default as a measurement.
            _terminal.val[i].qual     = (d.role == ROLE_PARAM) ? Q_GOOD : Q_NO_DATA;
            // Nothing has been demanded yet, so nothing is owed to the field.
            // An output starts at its safe state and the driver's first
            // unconditional rewrite is what puts it there.
            _terminal.val[i].vflags   = 0;

            uint16_t cap = 0;
            char* text = text_at(Var(&d, &_terminal.val[i]), &cap);
            if (text != nullptr) copy_bounded(text, cap, d.def.s);
        }
    }

    void Bank::_find_param_block() {
        _param_first = 0;
        _param_count = 0;
        for (uint16_t i = 0; i < _terminal.count; i++) {
            if (_terminal.var[i].role != ROLE_PARAM) continue;
            _param_first = i;
            while (i < _terminal.count && _terminal.var[i].role == ROLE_PARAM) { _param_count++; i++; }
            return;
        }
    }

    char* Bank::text_at(Var h, uint16_t* cap_out) const {
        if (cap_out != nullptr) *cap_out = 0;
        const uint16_t cap = text_slot(*h.meta()).cap;
        const uint16_t off = h.str_off();
        if (_terminal.pool == nullptr || cap == 0 || off == STR_NO_SLOT) return nullptr;
        // Belt and braces: init() already bounded this, but this is the one
        // place a pool pointer is handed out, so it is the one place worth
        // proving it lands inside the array.
        if ((uint32_t)off + cap > _terminal.pool_bytes) return nullptr;
        if (cap_out != nullptr) *cap_out = cap;
        return _terminal.pool + off;
    }

    bool Bank::_order_before(uint16_t a, uint16_t b) const {
        const int c = cmp_name(_terminal.var[a].name, _terminal.var[b].name, 0);
        return (c != 0) ? (c < 0) : (a < b);
    }

    void Bank::_build_index() {
        _indexed = false;
        if (_terminal.index == nullptr || _terminal.var == nullptr || _terminal.count == 0) return;

        for (uint16_t i = 0; i < _terminal.count; i++) _terminal.index[i] = i;
        // Binary insertion sort. The expensive operation here is the strcmp, and
        // this does n log n of them (~2000 for a 260-row site); the shuffling is
        // uint16_t moves, which are free by comparison. Once, at construction -
        // there is no insertion, no removal and nothing to invalidate later.
        for (uint16_t i = 1; i < _terminal.count; i++) {
            const uint16_t row = _terminal.index[i];
            uint16_t lo = 0, hi = i;
            while (lo < hi) {
                const uint16_t mid = (uint16_t)(lo + (hi - lo) / 2);
                if (_order_before(row, _terminal.index[mid])) hi = mid;
                else                                   lo = (uint16_t)(mid + 1);
            }
            for (uint16_t k = i; k > lo; k--) _terminal.index[k] = _terminal.index[k - 1];
            _terminal.index[lo] = row;
        }
        _indexed = true;
    }

    uint16_t Bank::_lookup(const char* name, uint16_t len) const {
        if (!_indexed) {
            for (uint16_t i = 0; i < _terminal.count; i++)
                if (cmp_name(_terminal.var[i].name, name, len) == 0) return i;
            return VAR_NONE;
        }
        // Lower bound, then one equality check: with ties ordered by row number,
        // the first match is the lowest row carrying that name.
        uint16_t lo = 0, hi = _terminal.count;
        while (lo < hi) {
            const uint16_t mid = (uint16_t)(lo + (hi - lo) / 2);
            if (cmp_name(_terminal.var[_terminal.index[mid]].name, name, len) < 0)
                lo = (uint16_t)(mid + 1);
            else
                hi = mid;
        }
        if (lo >= _terminal.count) return VAR_NONE;
        const uint16_t row = _terminal.index[lo];
        return (cmp_name(_terminal.var[row].name, name, len) == 0) ? row : VAR_NONE;
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
            (void)_store->load(_terminal.var[i], _terminal.val[i], text, cap);
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

        for (uint16_t i = 0; i < _terminal.count; i++) {
            if (_terminal.var[i].role != ROLE_OUTPUT) continue;
            if (release && _terminal.val[i].ovr_on) {
                // Wipe the stored copy in the same breath: overrides persist, so
                // a release that only cleared RAM would hand the actuator back
                // to whoever forced it at the next boot.
                if (_store != nullptr) (void)_store->clear_override(_terminal.var[i]);
                _terminal.val[i].ovr_on = false;
                _terminal.val[i].vflags |= VF_FIELD_DIRTY;
            }
            if (to_safe) {
                _terminal.val[i].qual = Q_SAFE;
                // The safe state is only in the image until something carries it
                // out. With local I/O that is instant; over a field bus it is a
                // transaction, and an unmarked row would leave the actuator
                // energised at exactly the moment control was withdrawn.
                _terminal.val[i].vflags |= VF_FIELD_DIRTY;
            }
        }
    }

    void Bank::expire_stale(uint64_t now_ms) {
        for (uint16_t i = 0; i < _terminal.count; i++) {
            const Variable& d = _terminal.var[i];
            if (d.role != ROLE_INPUT || d.timeout_ms == 0) continue;
            if (_terminal.val[i].qual == Q_GOOD &&
                (now_ms - _terminal.val[i].stamp_ms) > d.timeout_ms)
                _terminal.val[i].qual = Q_STALE;
        }
    }

    bool value_implausible(const Variable& d, const Scalar& v) {
        if (type_real(d.type)) {
            const float f = v.f;
            if (!(f == f) || f > 3.402823466e+38f || f < -3.402823466e+38f)
                return true;
        }
        // SCOPE_AI/AO carry their band in parameters, not here; num_band()
        // returns unset for them, which would reject everything.
        if ((d.flags & F_BOUNDED) == 0 || payload_arm(d) != PAY_NUM) return false;
        const NumBand b = num_band(d);
        return !in_range(d.type, v, b.min, b.max);
    }

    uint16_t Bank::reject_implausible() {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _terminal.count; i++) {
            const Variable& d = _terminal.var[i];
            if (d.role != ROLE_INPUT || _terminal.val[i].qual != Q_GOOD) continue;
            // An override is a maintainer deliberately pushing a reading past
            // its band to exercise what watches for it - the one case where an
            // implausible value is the point.
            if (_terminal.val[i].ovr_on) continue;

            if (!value_implausible(d, _terminal.val[i].v)) continue;
            _terminal.val[i].qual = Q_IMPLAUSIBLE;
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

    namespace {
        /** @brief Whether @p d may be edited at all, and where its slot is.
         *
         *  The handle already points INTO the table, so its row is the offset -
         *  no name scan. (It may instead point at the sink, which is what the
         *  range check rejects; a Var is only ever one or the other.) */
        Value* editable_slot(const VarTables& t, const Variable* d) {
            if (d->role != ROLE_PARAM || (d->flags & F_RDONLY) != 0) return nullptr;
            if (t.var == nullptr || d < t.var || d >= t.var + t.count) return nullptr;
            return &t.val[d - t.var];
        }
    }  // namespace

    SetResult Bank::check(Var h, Scalar v) const {
        const Variable* d = h.meta();
        // A T_STR's live text is in the pool, so there is no typed form of it to
        // write; the text overload is the only way in.
        if (d->type == T_STR) return SET_INVALID;
        if (editable_slot(_terminal, d) == nullptr) return SET_INVALID;
        const NumBand band = num_band(*d);
        if ((d->flags & F_BOUNDED) != 0 && !in_range(d->type, v, band.min, band.max))
            return SET_INVALID;
        return SET_OK;
    }

    SetResult Bank::set(Var h, Scalar v) {
        const SetResult r = set_deferred(h, v);
        if (r != SET_OK) return r;
        return persist(h);
    }

    SetResult Bank::set_deferred(Var h, Scalar v) {
        if (check(h, v) != SET_OK) return SET_INVALID;
        const Variable* d = h.meta();
        Value* slot = editable_slot(_terminal, d);
        if (slot == nullptr) return SET_INVALID;
        slot->v    = v;
        slot->qual = Q_GOOD;
        return SET_OK;
    }

    SetResult Bank::persist(Var h) {
        const Variable* d = h.meta();
        Value* slot = editable_slot(_terminal, d);
        if (slot == nullptr) return SET_INVALID;
        return _persist_param(h, *d, *slot);
    }

    SetResult Bank::set(Var h, const char* text) {
        const Variable* d = h.meta();
        if (text == nullptr) return SET_INVALID;
        Value* slotp = editable_slot(_terminal, d);
        if (slotp == nullptr) return SET_INVALID;
        Value& slot = *slotp;

        if (d->type == T_STR) {
            uint16_t cap = 0;
            char* dst = text_at(h, &cap);
            if (dst == nullptr) return SET_INVALID;
            if (!validate_str(text_slot(*d).kind, text, cap)) return SET_INVALID;
            copy_bounded(dst, cap, text);
            slot.qual = Q_GOOD;
        } else {
            Scalar parsed;
            if (parse_for(*d, text, /*bounded=*/true, &parsed) != SET_OK) return SET_INVALID;
            slot.v    = parsed;
            slot.qual = Q_GOOD;
        }

        return _persist_param(h, *d, slot);
    }

    SetResult Bank::_persist_param(Var h, const Variable& d, Value& slot) {
        if (_store == nullptr) return SET_NOT_PERSISTED;
        uint16_t save_cap = 0;
        char* save_text = text_at(h, &save_cap);
        if (!_store->save(d, slot, save_text, save_cap)) {
            // Loud: the value is live, so nothing looks wrong until the reboot
            // that quietly reverts it.
            if (_logger != nullptr)
                _logger->critical(Logs::STORAGE, "param '%s' not persisted", d.name);
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
        // what `control disable` is for.
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
        if (_terminal.var == nullptr || d < _terminal.var || d >= _terminal.var + _terminal.count) return VAR_NONE;
        return (uint16_t)(d - _terminal.var);
    }

    void Bank::_persist_override(Var h) {
        const uint16_t row = _row_of(h);
        if (_store == nullptr || row == VAR_NONE) return;
        const Variable* d = h.meta();
        if (!_store->save_override(*d, _terminal.val[row]) && _logger != nullptr)
            _logger->critical(Logs::STORAGE, "override on '%s' not persisted", d->name);
    }

    uint16_t Bank::load_overrides() {
        if (_store == nullptr) return 0;
        uint16_t n = 0;
        for (uint16_t i = 0; i < _terminal.count; i++) {
            const Variable& d = _terminal.var[i];
            if (!_store->load_override(d, _terminal.val[i])) continue;
            _terminal.val[i].ovr_on = true;
            if (d.role == ROLE_OUTPUT) _terminal.val[i].vflags |= VF_FIELD_DIRTY;
            n++;
            // CRITICAL, not a warning: an override is the one thing that
            // explains behaviour nothing else accounts for, and after a reboot
            // nobody remembers setting it. The row's own scope says whether it
            // is an actuator, so the entry does not shout about it.
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT, "override restored on '%s'", d.name);
        }
        return n;
    }

    uint16_t Bank::clear_overrides() {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _terminal.count; i++) {
            if (!_terminal.val[i].ovr_on) continue;
            _terminal.val[i].ovr_on = false;
            if (_store != nullptr) (void)_store->clear_override(_terminal.var[i]);
            // What the field should be showing has just changed back to whatever
            // the programs demand underneath; a released override that was never
            // written out would leave the actuator where the maintainer left it.
            if (_terminal.var[i].role == ROLE_OUTPUT)
                _terminal.val[i].vflags |= VF_FIELD_DIRTY;
            n++;
        }
        return n;
    }

    uint16_t Bank::override_count() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _terminal.count; i++) if (_terminal.val[i].ovr_on) n++;
        return n;
    }

}}  // namespace Botanik::Vars
