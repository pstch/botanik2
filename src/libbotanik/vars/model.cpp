/**
 * @file model.cpp
 * @brief Bank: seeding, lookup, staleness expiry and the fail-safe sweep.
 *
 * Each is ONE loop over the one table, with a role switch; no sweep names a
 * subset of rows by hand.
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
        0, 0, nullptr, T_U8, ROLE_COMPUTED, SCOPE_G, 0, 0, {}
    };
    Value Bank::_sink_v = {};

    namespace {
        /** @brief Three-way compare of row name @p n against the key @p key,
         *  bounded to @p len characters when @p len is non-zero (a shell word
         *  has no NUL of its own).
         *
         *  Both arms give the same order (with the first @p len characters
         *  equal, a longer row name sorts after the key), so one sorted mapping
         *  serves both find() overloads. A nameless row sorts after everything
         *  and matches nothing. */
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
        // Text rows first, in table order, so a slot's position is derived and
        // never declared.
        _pool_used = 0;
        for (uint16_t i = 0; i < _tables.count; i++) {
            const uint16_t cap = text_slot(_tables.var[i]).cap;
            if (cap == 0) { _tables.val[i].str_off = 0; continue; }
            if ((uint32_t)_pool_used + cap > _tables.pool_bytes) {
                // Out of pool. STR_NO_SLOT, not 0: offset 0 is the first text
                // row's slot, so an overflowed row parked there would share its
                // bytes. text_at() refuses on the sentinel, so the row reads
                // empty and every write bounces.
                _tables.val[i].str_off = STR_NO_SLOT;
                _pool_overflow++;
                continue;
            }
            _tables.val[i].str_off = _pool_used;
            _pool_used = (uint16_t)(_pool_used + cap);
        }

        for (uint16_t i = 0; i < _tables.count; i++) {
            const Variable& d = _tables.var[i];
            _tables.val[i].v        = d.def;
            _tables.val[i].stamp_ms = 0;
            // Overrides are a live-session tool; a fresh table has none.
            _tables.val[i].ovr_on   = false;
            // A parameter always HAS a value (its factory default until the
            // store loads one), so it starts good. Everything else starts with
            // nothing written, so a default is never shown as a measurement.
            _tables.val[i].qual     = (d.role == ROLE_PARAM) ? Q_GOOD : Q_NO_DATA;
            // Nothing demanded yet, so nothing is owed to the field: an output
            // starts at its safe state and the driver's first unconditional
            // rewrite is what puts it there.
            _tables.val[i].vflags   = 0;
            // Explicit, not left to zero fill: zero is program 0, not "none".
            _tables.val[i].writer   = WRITER_NONE;

            uint16_t cap = 0;
            char* text = text_at(Var(&d, &_tables.val[i]), &cap);
            if (text != nullptr) copy_bounded(text, cap, d.def.s);
        }
    }

    void Bank::_find_param_block() {
        _param_first = 0;
        _param_count = 0;
        for (uint16_t i = 0; i < _tables.count; i++) {
            if (_tables.var[i].role != ROLE_PARAM) continue;
            _param_first = i;
            while (i < _tables.count && _tables.var[i].role == ROLE_PARAM) { _param_count++; i++; }
            return;
        }
    }

    char* Bank::text_at(Var h, uint16_t* cap_out) const {
        if (cap_out != nullptr) *cap_out = 0;
        const uint16_t cap = text_slot(*h.meta()).cap;
        const uint16_t off = h.str_off();
        if (_tables.pool == nullptr || cap == 0 || off == STR_NO_SLOT) return nullptr;
        // Belt and braces: init() already bounded this, but this is the only
        // place a pool pointer is handed out.
        if ((uint32_t)off + cap > _tables.pool_bytes) return nullptr;
        if (cap_out != nullptr) *cap_out = cap;
        return _tables.pool + off;
    }

    bool Bank::_order_before(uint16_t a, uint16_t b) const {
        const int c = cmp_name(_tables.var[a].name, _tables.var[b].name, 0);
        return (c != 0) ? (c < 0) : (a < b);
    }

    void Bank::_build_index() {
        _indexed = false;
        if (_tables.index == nullptr || _tables.var == nullptr || _tables.count == 0) return;

        for (uint16_t i = 0; i < _tables.count; i++) _tables.index[i] = i;
        // Binary insertion sort: the strcmp is the expensive part and this does
        // n log n of them (~2000 for a 260-row site), the shuffling being
        // uint16_t moves. Once, at construction; nothing invalidates it.
        for (uint16_t i = 1; i < _tables.count; i++) {
            const uint16_t row = _tables.index[i];
            uint16_t lo = 0, hi = i;
            while (lo < hi) {
                const uint16_t mid = (uint16_t)(lo + (hi - lo) / 2);
                if (_order_before(row, _tables.index[mid])) hi = mid;
                else                                   lo = (uint16_t)(mid + 1);
            }
            for (uint16_t k = i; k > lo; k--) _tables.index[k] = _tables.index[k - 1];
            _tables.index[lo] = row;
        }
        _indexed = true;
    }

    uint16_t Bank::_lookup(const char* name, uint16_t len) const {
        if (!_indexed) {
            for (uint16_t i = 0; i < _tables.count; i++)
                if (cmp_name(_tables.var[i].name, name, len) == 0) return i;
            return VAR_NONE;
        }
        // Lower bound, then one equality check: with ties ordered by row number,
        // the first match is the lowest row carrying that name.
        uint16_t lo = 0, hi = _tables.count;
        while (lo < hi) {
            const uint16_t mid = (uint16_t)(lo + (hi - lo) / 2);
            if (cmp_name(_tables.var[_tables.index[mid]].name, name, len) < 0)
                lo = (uint16_t)(mid + 1);
            else
                hi = mid;
        }
        if (lo >= _tables.count) return VAR_NONE;
        const uint16_t row = _tables.index[lo];
        return (cmp_name(_tables.var[row].name, name, len) == 0) ? row : VAR_NONE;
    }

    VarRef Bank::find(const char* name) const {
        VarRef r = { VAR_NONE };
        if (name == nullptr) return r;
        r.index = _lookup(name, 0);
        if (r.index != VAR_NONE) return r;
        // A miss is a name a table or a program uses but vars.h does not
        // declare. Count it and say which, or it reads as a dead variable.
        if (_unresolved < 0xFFFFu) _unresolved++;
        if (_logger != nullptr) _logger->error(Logs::BOOT, "unknown variable\n'%s'", name);
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
            (void)_store->load(_tables.var[i], _tables.val[i], text, cap);
        }
    }

    void Bank::set_control_mode(ControlMode mode) {
        const ControlMode before = control_mode();
        _op_mode = mode;
        _apply_control(before, false);
    }

    void Bank::set_health_hold(bool held) {
        const ControlMode before = control_mode();
        const bool        was    = _health_hold;
        _health_hold = held;
        // The system RAISING its hold is the destructive edge, and only on the
        // transition: the per-beat watchdog path re-asserts it for as long as
        // the fault lasts, so re-erasing every beat would be wasted work.
        _apply_control(before, held && !was);
    }

    void Bank::_apply_control(ControlMode before, bool erase_overrides) {
        const ControlMode now = control_mode();
        if (now == before && !erase_overrides) return;

        // Drop to safe on the way out of ENABLED only; a mode change between the
        // two withheld modes finds the outputs already there.
        const bool to_safe = (before == CONTROL_ENABLED && now != CONTROL_ENABLED);

        for (uint16_t i = 0; i < _tables.count; i++) {
            if (_tables.var[i].role != ROLE_OUTPUT) continue;
            Value& v = _tables.val[i];

            if (erase_overrides && v.ovr_on) {
                // Wipe the stored copy too: overrides persist, so clearing only
                // RAM hands the actuator back to whoever forced it at the next
                // boot.
                if (_store != nullptr) (void)_store->clear_override(_tables.var[i]);
                v.ovr_on  = false;
                v.vflags |= VF_FIELD_DIRTY;
            }

            // Muting and unmuting both CHANGE what the field is owed, so both
            // mark the row: unmuting hands the actuator back to the forced
            // value, and nothing else would carry it out there.
            if (_sync_override_mute(i) && v.ovr_on) v.vflags |= VF_FIELD_DIRTY;

            if (to_safe) {
                v.qual = Q_SAFE;
                // The safe state is only in the image until something carries it
                // out: over a field bus that is a transaction, and an unmarked
                // row leaves the actuator energised.
                v.vflags |= VF_FIELD_DIRTY;
            }
        }
    }

    void Bank::expire_stale(uint64_t now_ms) {
        for (uint16_t i = 0; i < _tables.count; i++) {
            const Variable& d = _tables.var[i];
            if (d.role != ROLE_INPUT || d.timeout_ms == 0) continue;
            if (_tables.val[i].qual == Q_GOOD &&
                (now_ms - _tables.val[i].stamp_ms) > d.timeout_ms)
                _tables.val[i].qual = Q_STALE;
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

    bool Bank::claim_writer(VarRef r, uint8_t who) {
        if (r.index >= _tables.count || who == WRITER_NONE) return true;
        uint8_t& held = _tables.val[r.index].writer;
        if (held == who) return true;               // the usual case, every beat
        if (held == WRITER_NONE) { held = who; return true; }

        // A second author. Counted, not logged: this runs in the control phase,
        // from a class with no logger. The driver reads the count on the edge
        // and names the row.
        //
        // Counted ONCE PER ROW, not per collision: both programs keep writing
        // every beat, so a per-collision count would rise forever.
        uint8_t& vf = _tables.val[r.index].vflags;
        if ((vf & VF_TWO_WRITERS) == 0) {
            vf = (uint8_t)(vf | VF_TWO_WRITERS);
            _conflicts++;
            if (_conflict_row.index == VAR_NONE) _conflict_row = r;
        }
        // The newest writer becomes the holder, so the row reports whoever
        // actually wrote it last.
        held = who;
        return false;
    }

    uint8_t Bank::writer_of(VarRef r) const {
        return (r.index < _tables.count) ? _tables.val[r.index].writer
                                           : WRITER_NONE;
    }

    uint16_t Bank::reject_implausible() {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _tables.count; i++) {
            const Variable& d = _tables.var[i];
            if (d.role != ROLE_INPUT || _tables.val[i].qual != Q_GOOD) continue;
            // An override pushes a reading past its band on purpose, to
            // exercise what watches for it.
            if (_tables.val[i].ovr_on) continue;

            if (!value_implausible(d, _tables.val[i].v)) continue;
            _tables.val[i].qual = Q_IMPLAUSIBLE;
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
            // d.str goes in so a labelled enum answers to its own words, and so
            // a code it cannot name is refused. That refusal is NOT the band and
            // so is not gated on `bounded`: an override may push a reading past
            // what is plausible, but there is no such thing as an enum value
            // outside its labels - it would render as a bare number and mean
            // nothing to anything reading it.
            if (parse_scalar(d.type, text, out, d.str) != PARSE_OK) return SET_INVALID;
            if (bounded && (d.flags & F_BOUNDED) != 0 &&
                !in_range(d.type, *out, band.min, band.max)) return SET_INVALID;
            return SET_OK;
        }
    }  // namespace

    namespace {
        /** @brief Whether @p d may be edited at all, and where its slot is.
         *
         *  The handle points INTO the table, so its row is the offset. The range
         *  check rejects a handle on the sink, which is the only alternative. */
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
        if (editable_slot(_tables, d) == nullptr) return SET_INVALID;
        // Same rule the text path gets through parse_for(): a labelled enum's
        // codes are its labels, and this overload never goes near a parser, so
        // the check has to be here too or a typed write is the way around it.
        if (d->type == T_ENUM && d->str != nullptr && d->str(v.u) == nullptr)
            return SET_INVALID;
        const NumBand band = num_band(*d);
        if ((d->flags & F_BOUNDED) != 0 && !in_range(d->type, v, band.min, band.max))
            return SET_INVALID;
        return SET_OK;
    }

    SetResult Bank::set(Var h, Scalar v) {
        if (check(h, v) != SET_OK) return SET_INVALID;
        const Variable* d = h.meta();
        Value* slot = editable_slot(_tables, d);
        if (slot == nullptr) return SET_INVALID;
        slot->v     = v;
        slot->qual  = Q_GOOD;
        // Live now, durable when somebody saves. See the header: it is the one
        // rule that makes a setting able to break its own console recoverable.
        slot->vflags = (uint8_t)(slot->vflags | VF_UNSAVED);
        return SET_OK;
    }

    SetResult Bank::persist(Var h) {
        const Variable* d = h.meta();
        Value* slot = editable_slot(_tables, d);
        if (slot == nullptr) return SET_INVALID;
        const SetResult r = _persist_param(h, *d, *slot);
        // Only a real write clears the mark: a store that refused still owes
        // this row, and `params` must go on saying so.
        if (r == SET_OK) slot->vflags = (uint8_t)(slot->vflags & ~VF_UNSAVED);
        return r;
    }

    uint16_t Bank::save_params() {
        uint16_t written = 0;
        for (uint16_t i = 0; i < _tables.count; i++) {
            if ((_tables.val[i].vflags & VF_UNSAVED) == 0) continue;
            if (persist(var(i)) == SET_OK) written++;
        }
        return written;
    }

    uint16_t Bank::unsaved_params() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _tables.count; i++)
            if ((_tables.val[i].vflags & VF_UNSAVED) != 0) n++;
        return n;
    }

    uint16_t Bank::save_overrides() {
        uint16_t written = 0;
        for (uint16_t i = 0; i < _tables.count; i++) {
            if ((_tables.val[i].vflags & VF_OVR_UNSAVED) == 0) continue;
            _persist_override(var(i));
            _tables.val[i].vflags = (uint8_t)(_tables.val[i].vflags & ~VF_OVR_UNSAVED);
            written++;
        }
        return written;
    }

    uint16_t Bank::unsaved_overrides() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _tables.count; i++)
            if ((_tables.val[i].vflags & VF_OVR_UNSAVED) != 0) n++;
        return n;
    }

    SetResult Bank::set(Var h, const char* text) {
        const Variable* d = h.meta();
        if (text == nullptr) return SET_INVALID;
        Value* slotp = editable_slot(_tables, d);
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

        // Live now, durable when saved - see the header. SET_OK because the EDIT
        // took: SET_NOT_PERSISTED means a store REFUSED a write, which is a
        // fault, and no write was attempted here.
        slot.vflags = (uint8_t)(slot.vflags | VF_UNSAVED);
        return SET_OK;
    }

    SetResult Bank::_persist_param(Var h, const Variable& d, Value& slot) {
        if (_store == nullptr) return SET_NOT_PERSISTED;
        uint16_t save_cap = 0;
        char* save_text = text_at(h, &save_cap);
        if (!_store->save(d, slot, save_text, save_cap)) {
            // Loud: the value is live, so nothing looks wrong until the reboot
            // that quietly reverts it.
            if (_logger != nullptr)
                _logger->critical(Logs::STORAGE, "param not persisted\n'%s'", d.name);
            return SET_NOT_PERSISTED;
        }
        return SET_OK;
    }

    SetResult Bank::override_set(Var h, const char* text, bool valid) {
        const Variable* d = h.meta();
        // A T_STR's live text lives in the pool, not the slot, so there is
        // nothing for an override to shadow.
        if (text == nullptr || d->type == T_STR) return SET_INVALID;
        // An output is deliberately NOT refused while control is disabled: with
        // the programs stopped, an override is the only way left to command an
        // actuator.
        Scalar parsed;
        // Band deliberately NOT applied - see the header note.
        if (parse_for(*d, text, /*bounded=*/false, &parsed) != SET_OK) return SET_INVALID;
        override_set(h, parsed, valid);
        return SET_OK;
    }

    void Bank::override_set(Var h, Scalar v, bool valid) {
        h.override_set(v, valid);
        (void)_sync_override_mute(_row_of(h));
        _mark_override_unsaved(h);
    }

    void Bank::override_freeze(Var h) {
        h.override_freeze();
        (void)_sync_override_mute(_row_of(h));
        _mark_override_unsaved(h);
    }

    void Bank::_mark_override_unsaved(Var h) {
        // A forced value does NOT reach flash on its own. An override is a
        // maintenance tool - a sensor held, an actuator driven by hand - and one
        // that resurrects itself at the next boot is the thing a safe state
        // exists to prevent; the system's health hold already erases them for
        // that reason. So it lives for as long as the unit does, and outlives it
        // only if somebody says `overrides save`.
        const uint16_t row = _row_of(h);
        if (row == VAR_NONE) return;
        _tables.val[row].vflags = (uint8_t)(_tables.val[row].vflags | VF_OVR_UNSAVED);
    }

    bool Bank::_sync_override_mute(uint16_t i) {
        // VAR_NONE lands here as an out-of-range index, so a sink handle is a
        // no-op rather than a special case at each caller.
        if (i >= _tables.count || _tables.var[i].role != ROLE_OUTPUT) return false;
        Value& v = _tables.val[i];
        const uint8_t before = v.vflags;
        v.vflags = (control_mode() == CONTROL_DISABLED)
                     ? (uint8_t)(before | VF_OVR_MUTED)
                     : (uint8_t)(before & ~VF_OVR_MUTED);
        return v.vflags != before;
    }

    void Bank::override_clear(Var h) {
        h.override_clear();
        const uint16_t row = _row_of(h);
        if (row == VAR_NONE) return;
        _tables.val[row].vflags = (uint8_t)(_tables.val[row].vflags & ~VF_OVR_UNSAVED);
        // Erased NOW, not on the next `overrides save`, and that asymmetry with
        // setting one is deliberate: releasing is unambiguous, and an override
        // that survived its own release to come back at the next boot is the
        // hazard the whole arrangement exists to avoid.
        if (_store != nullptr) (void)_store->clear_override(*h.meta());
    }

    uint16_t Bank::_row_of(Var h) const {
        const Variable* d = h.meta();
        if (_tables.var == nullptr || d < _tables.var || d >= _tables.var + _tables.count) return VAR_NONE;
        return (uint16_t)(d - _tables.var);
    }

    void Bank::_persist_override(Var h) {
        const uint16_t row = _row_of(h);
        if (_store == nullptr || row == VAR_NONE) return;
        const Variable* d = h.meta();
        if (!_store->save_override(*d, _tables.val[row]) && _logger != nullptr)
            _logger->critical(Logs::STORAGE, "override not persisted\n'%s'", d->name);
    }

    uint16_t Bank::load_overrides() {
        if (_store == nullptr) return 0;
        uint16_t n = 0;
        for (uint16_t i = 0; i < _tables.count; i++) {
            const Variable& d = _tables.var[i];
            if (!_store->load_override(d, _tables.val[i])) continue;
            _tables.val[i].ovr_on = true;
            // Restored while the system still holds control, i.e. with the mode
            // at CONTROL_DISABLED, so this is exactly the window the mute covers:
            // an output override must not reach the field until control does.
            (void)_sync_override_mute(i);
            if (d.role == ROLE_OUTPUT) _tables.val[i].vflags |= VF_FIELD_DIRTY;
            n++;
            // CRITICAL, not a warning: an override explains behaviour nothing
            // else accounts for, and after a reboot nobody remembers setting it.
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT, "override restored\non '%s'", d.name);
        }
        return n;
    }

    uint16_t Bank::load_retained() {
        if (_store == nullptr) return 0;
        uint16_t n = 0;
        for (uint16_t i = 0; i < _tables.count; i++) {
            const Variable& d = _tables.var[i];
            if (!retainable(d)) continue;
            if (!_store->load_retained(d, _tables.val[i])) continue;
            // Q_GOOD, because it is: the value a totalizer carried when the unit
            // went down is what to carry on from. Q_NO_DATA would send every
            // reader to the default this restore exists to avoid.
            _tables.val[i].qual = Q_GOOD;
            n++;
        }
        return n;
    }

    uint16_t Bank::save_retained() {
        if (_store == nullptr) return 0;
        uint16_t n = 0;
        for (uint16_t i = 0; i < _tables.count; i++) {
            const Variable& d = _tables.var[i];
            if (!retainable(d)) continue;
            // The LIVE value, never the override: a totalizer forced to 1000 to
            // test an alarm must not enter the plant's history.
            // A refused write is already counted by HAL::Storage, which is what
            // alarm_flash_error watches.
            if (_store->save_retained(d, _tables.val[i])) n++;
        }
        return n;
    }

    uint16_t Bank::clear_overrides() {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _tables.count; i++) {
            if (!_tables.val[i].ovr_on) continue;
            _tables.val[i].ovr_on = false;
            _tables.val[i].vflags = (uint8_t)(_tables.val[i].vflags & ~VF_OVR_UNSAVED);
            if (_store != nullptr) (void)_store->clear_override(_tables.var[i]);
            // The field owes the programs' demand again; a release never written
            // out would leave the actuator where the maintainer left it.
            if (_tables.var[i].role == ROLE_OUTPUT)
                _tables.val[i].vflags |= VF_FIELD_DIRTY;
            n++;
        }
        return n;
    }

    uint16_t Bank::override_count() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _tables.count; i++) if (_tables.val[i].ovr_on) n++;
        return n;
    }

}}  // namespace Botanik::Vars
