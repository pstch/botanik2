/**
 * @file model.cpp
 * @brief Bank: seeding, lookup, staleness expiry and the fail-safe sweep.
 *
 * Each is ONE loop over the one table, with a role switch; no sweep names a
 * subset of rows by hand.
 */

#include "libbotanik/vars/model.h"

                            // Bank / Var / Variable

#include "libbotanik/tasks/tasks.h"


#include <cstring>
#include "libbotanik/logs/logger.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/utils/text.h"

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
        /** @brief Copy @p src into @p dst, always NUL-terminating. */
        void copy_bounded(char* dst, uint16_t cap, const char* src) {
            if (dst == nullptr || cap == 0) return;
            if (src == nullptr) { dst[0] = 0; return; }
            uint16_t n = 0;
            while (src[n] != 0 && n + 1u < cap) { dst[n] = src[n]; n++; }
            dst[n] = 0;
        }
    }  // namespace

    // Registration

    bool Bank::register_vars(const Variable* rows, uint16_t count) {
        if (rows == nullptr || count == 0) return true;   // nothing to add
        // All or nothing: a half-added segment would leave the rows past the
        // ceiling silently absent while the ones before them worked, which is
        // the shape of bug that takes a week to find.
        if (_segments >= Config::VAR_SEGMENTS ||
            (uint32_t)_count + count > Config::VAR_MAX) {
            // Loud here, counted for POST there: every name in the lost segment
            // resolves to the sink, so the subsystem that owns them looks
            // present and does nothing.
            BOTANIK_ASSERT(!"variable table full: raise VAR_MAX / VAR_SEGMENTS");
            _refused = (uint16_t)(_refused + count);
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT, "%u vars refused ('%s'...): raise VAR_MAX",
                                  (unsigned)count, rows[0].name);
            return false;
        }
        _seg[_segments]      = rows;
        _seg_base[_segments] = _count;
        _seg_len[_segments]  = count;
        _segments++;
        // Identity HERE, not at freeze(): desc() goes through _row from the
        // moment a row exists, and a bank that is read before it is frozen (or
        // never frozen at all, as several tests do) would otherwise index the
        // segments through uninitialised memory.
        for (uint16_t i = _count; i < _count + count; i++) _row[i] = i;
        _count = (uint16_t)(_count + count);
        return true;
    }

    void Bank::_resolve_shadows() {
        // _row is still identity from register_vars(), so desc() reads
        // physically here and the sort below can use the ordinary accessors.
        _build_index();       // by name, so a redeclared pair lands adjacent

        uint16_t i = 0;
        while (i < _count) {
            uint16_t j = (uint16_t)(i + 1);
            while (j < _count &&
                   std::strcmp(desc(_index[i]).name, desc(_index[j]).name) == 0) j++;
            if (j > i + 1) {
                // LAST wins: segments register library-then-site, so the site's
                // restatement is the one that survives.
                uint16_t win = _index[i];
                for (uint16_t k = (uint16_t)(i + 1); k < j; k++)
                    if (_index[k] > win) win = _index[k];
                for (uint16_t k = i; k < j; k++) {
                    if (_index[k] == win) continue;
                    _row[_index[k]] = VAR_NONE;         // tombstone
                    _shadowed++;
                    if (_logger != nullptr)
                        _logger->notice(Logs::BOOT, "var '%s' redeclared",
                                        desc(win).name);
                }
            }
            i = j;
        }
        if (_shadowed == 0) return;

        // Compact in place: k never outruns p, so this cannot clobber.
        uint16_t k = 0;
        for (uint16_t p = 0; p < _count; p++)
            if (_row[p] != VAR_NONE) _row[k++] = p;
        _count = k;
    }

    void Bank::freeze() {
        // BEFORE init(): a shadowed row must not be given a pool slot or a value
        // slot it will never be read through.
        _resolve_shadows();
        init();               // pool layout, then every slot from its default
        _find_param_block();
        _build_index();
        // The ROLE_PARAM rows must be ONE run - that is what lets the PARAMS
        // page and the shell address them 0-based. Registration order decides
        // it, so this is where it can be checked; it used to be a convention
        // with a test to catch it afterwards. Naming the row is the whole point:
        // "parameters are not contiguous" sends nobody anywhere.
        for (uint16_t i = (uint16_t)(_param_first + _param_count); i < _count; i++) {
            if (desc(i).role != ROLE_PARAM) continue;
            BOTANIK_ASSERT(!"a parameter outside the parameter block");
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT,
                                  "param '%s' is outside the parameter block\n"
                                  "register the parameter segment first",
                                  desc(i).name);
            _param_stray++;
        }
    }

    void Bank::init() {
        // Text rows first, in table order, so a slot's position is derived and
        // never declared.
        _pool_used = 0;
        for (uint16_t i = 0; i < _count; i++) {
            const uint16_t cap = text_slot(desc(i)).cap;
            if (cap == 0) { _values[i].str_off = 0; continue; }
            if ((uint32_t)_pool_used + cap > Config::STR_POOL_BYTES) {
                // Out of pool. STR_NO_SLOT, not 0: offset 0 is the first text
                // row's slot, so an overflowed row parked there would share its
                // bytes. text_at() refuses on the sentinel, so the row reads
                // empty and every write bounces.
                _values[i].str_off = STR_NO_SLOT;
                _pool_overflow++;
                continue;
            }
            _values[i].str_off = _pool_used;
            _pool_used = (uint16_t)(_pool_used + cap);
        }

        for (uint16_t i = 0; i < _count; i++) {
            const Variable& d = desc(i);
            _values[i].v        = d.def;
            _values[i].stamp_ms = 0;
            // Overrides are a live-session tool; a fresh table has none.
            _values[i].ovr_on   = false;
            // A parameter always HAS a value (its factory default until the
            // store loads one), so it starts good. Everything else starts with
            // nothing written, so a default is never shown as a measurement.
            _values[i].qual     = (d.role == ROLE_PARAM) ? Q_GOOD : Q_NO_DATA;
            // Nothing demanded yet, so nothing is owed to the field: an output
            // starts at its safe state and the driver's first unconditional
            // rewrite is what puts it there.
            _values[i].vflags   = 0;
            // Explicit, not left to zero fill: zero is program 0, not "none".
            _values[i].writer   = WRITER_NONE;

            uint16_t cap = 0;
            char* text = text_at(Var(&d, &_values[i]), &cap);
            if (text != nullptr) copy_bounded(text, cap, d.def.s);
        }
    }

    void Bank::_find_param_block() {
        _param_first = 0;
        _param_count = 0;
        for (uint16_t i = 0; i < _count; i++) {
            if (desc(i).role != ROLE_PARAM) continue;
            _param_first = i;
            while (i < _count && desc(i).role == ROLE_PARAM) { _param_count++; i++; }
            return;
        }
    }

    char* Bank::text_at(Var h, uint16_t* cap_out) const {
        if (cap_out != nullptr) *cap_out = 0;
        const uint16_t cap = text_slot(*h.meta()).cap;
        const uint16_t off = h.str_off();
        if (cap == 0 || off == STR_NO_SLOT) return nullptr;
        // Belt and braces: init() already bounded this, but this is the only
        // place a pool pointer is handed out.
        if ((uint32_t)off + cap > Config::STR_POOL_BYTES) return nullptr;
        if (cap_out != nullptr) *cap_out = cap;
        return _pool + off;
    }

    bool Bank::_order_before(uint16_t a, uint16_t b) const {
        const int c = Utils::compare_bounded(desc(a).name, desc(b).name, 0);
        return (c != 0) ? (c < 0) : (a < b);
    }

    void Bank::_build_index() {
        _indexed = false;
        if (_segments == 0 || _count == 0) return;

        for (uint16_t i = 0; i < _count; i++) _index[i] = i;
        // Binary insertion sort: the strcmp is the expensive part and this does
        // n log n of them (~2000 for a 260-row site), the shuffling being
        // uint16_t moves. Once, at construction; nothing invalidates it.
        for (uint16_t i = 1; i < _count; i++) {
            const uint16_t row = _index[i];
            uint16_t lo = 0, hi = i;
            while (lo < hi) {
                const uint16_t mid = (uint16_t)(lo + (hi - lo) / 2);
                if (_order_before(row, _index[mid])) hi = mid;
                else                                   lo = (uint16_t)(mid + 1);
            }
            for (uint16_t k = i; k > lo; k--) _index[k] = _index[k - 1];
            _index[lo] = row;
        }
        _indexed = true;
    }

    uint16_t Bank::_lookup(const char* name, uint16_t len) const {
        if (!_indexed) {
            for (uint16_t i = 0; i < _count; i++)
                if (Utils::compare_bounded(desc(i).name, name, len) == 0) return i;
            return VAR_NONE;
        }
        // Lower bound, then one equality check: with ties ordered by row number,
        // the first match is the lowest row carrying that name.
        uint16_t lo = 0, hi = _count;
        while (lo < hi) {
            const uint16_t mid = (uint16_t)(lo + (hi - lo) / 2);
            if (Utils::compare_bounded(desc(_index[mid]).name, name, len) < 0)
                lo = (uint16_t)(mid + 1);
            else
                hi = mid;
        }
        if (lo >= _count) return VAR_NONE;
        const uint16_t row = _index[lo];
        return (Utils::compare_bounded(desc(row).name, name, len) == 0) ? row : VAR_NONE;
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
            (void)_store->load(desc(i), _values[i], text, cap);
            // Whatever the row now holds came from the store (or is the seeded
            // default), so nothing is owed a write.
            _values[i].vflags = (uint8_t)(_values[i].vflags & ~VF_PARAM_UNSAVED);
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

        for (uint16_t i = 0; i < _count; i++) {
            if (desc(i).role != ROLE_OUTPUT) continue;
            Value& v = _values[i];

            if (erase_overrides && v.ovr_on) {
                // Wipe the stored copy too: overrides persist, so clearing only
                // RAM hands the actuator back to whoever forced it at the next
                // boot.
                if (_store != nullptr) (void)_store->clear_override(desc(i));
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
        for (uint16_t i = 0; i < _count; i++) {
            const Variable& d = desc(i);
            if (d.role != ROLE_INPUT || d.timeout_ms == 0) continue;
            if (_values[i].qual == Q_GOOD &&
                (now_ms - _values[i].stamp_ms) > d.timeout_ms)
                _values[i].qual = Q_STALE;
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
        if (r.index >= _count || who == WRITER_NONE) return true;
        uint8_t& held = _values[r.index].writer;
        if (held == who) return true;               // the usual case, every beat
        if (held == WRITER_NONE) { held = who; return true; }

        // A second author. Counted, not logged: this runs in the control phase,
        // from a class with no logger. The driver reads the count on the edge
        // and names the row.
        //
        // Counted ONCE PER ROW, not per collision: both programs keep writing
        // every beat, so a per-collision count would rise forever.
        uint8_t& vf = _values[r.index].vflags;
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
        return (r.index < _count) ? _values[r.index].writer
                                           : WRITER_NONE;
    }

    uint16_t Bank::reject_implausible() {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++) {
            const Variable& d = desc(i);
            if (d.role != ROLE_INPUT || _values[i].qual != Q_GOOD) continue;
            // An override pushes a reading past its band on purpose, to
            // exercise what watches for it.
            if (_values[i].ovr_on) continue;

            if (!value_implausible(d, _values[i].v)) continue;
            _values[i].qual = Q_IMPLAUSIBLE;
            n++;
        }
        return n;
    }

    // Writing

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
    }  // namespace

    SetResult Bank::check(Var h, Scalar v) const {
        const Variable* d = h.meta();
        // A T_STR's live text is in the pool, so there is no typed form of it to
        // write; the text overload is the only way in.
        if (d->type == T_STR) return SET_INVALID;
        if (_editable_slot(d) == nullptr) return SET_INVALID;
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
        const SetResult r = set_deferred(h, v);
        if (r != SET_OK) return r;
        // Live now, durable when the operator says so.
        Value* slot = _editable_slot(h.meta());
        slot->vflags = (uint8_t)(slot->vflags | VF_PARAM_UNSAVED);
        return SET_OK;
    }

    SetResult Bank::set_deferred(Var h, Scalar v) {
        if (check(h, v) != SET_OK) return SET_INVALID;
        const Variable* d = h.meta();
        Value* slot = _editable_slot(d);
        if (slot == nullptr) return SET_INVALID;
        slot->v     = v;
        slot->qual  = Q_GOOD;
        // Live now, durable when somebody saves. See the header: it is the one
        // rule that makes a setting able to break its own console recoverable.
        slot->vflags = (uint8_t)(slot->vflags | VF_PARAM_UNSAVED);
        return SET_OK;
    }

    SetResult Bank::param_clear(Var h) {
        const Variable* d = h.meta();
        Value* slot = _editable_slot(d);
        if (slot == nullptr) return SET_INVALID;
        // Nothing owed, nothing to forget. Not an error: `clear` on a settled
        // row is the no-op an operator expects, not a complaint.
        if ((slot->vflags & VF_PARAM_UNSAVED) == 0) return SET_OK;

        uint16_t cap = 0;
        char* text = text_at(h, &cap);
        // Exactly what a reboot would give it: the stored copy, or the factory
        // default when nothing was ever saved. The stored copy is NOT touched -
        // that is the whole difference from param_reset().
        if (_store == nullptr || !_store->load(*d, *slot, text, cap)) {
            slot->v = d->def;
            if (text != nullptr) copy_bounded(text, cap, d->def.s);
        }
        slot->qual   = Q_GOOD;                // a parameter always has a value
        slot->vflags = (uint8_t)(slot->vflags & ~VF_PARAM_UNSAVED);
        return SET_OK;
    }

    SetResult Bank::param_reset(Var h) {
        const Variable* d = h.meta();
        Value* slot = _editable_slot(d);
        if (slot == nullptr) return SET_INVALID;

        slot->v    = d->def;
        slot->qual = Q_GOOD;                  // a parameter always has a value
        uint16_t cap = 0;
        char* text = text_at(h, &cap);
        if (text != nullptr) copy_bounded(text, cap, d->def.s);
        slot->vflags = (uint8_t)(slot->vflags & ~VF_PARAM_UNSAVED);

        // The stored copy goes with the live one: left behind, it would put the
        // old value back at the next boot, which is not "back to default".
        if (_store == nullptr) return SET_NOT_PERSISTED;
        if (!_store->clear(*d)) {
            if (_logger != nullptr)
                _logger->critical(Logs::STORAGE, "param not cleared\n'%s'", d->name);
            return SET_NOT_PERSISTED;
        }
        return SET_OK;
    }

    SetResult Bank::save(Var h) {
        const Variable* d = h.meta();
        Value* slot = _editable_slot(d);
        if (slot == nullptr) return SET_INVALID;
        const SetResult r = _persist_param(h, *d, *slot);
        // Only a write that landed clears the mark: a refused row stays owed,
        // so the next `params save` tries it again.
        if (r == SET_OK) slot->vflags = (uint8_t)(slot->vflags & ~VF_PARAM_UNSAVED);
        return r;
    }

    uint16_t Bank::save_params(uint16_t* refused) {
        uint16_t saved = 0, bad = 0;
        for (uint16_t i = 0; i < _count; i++) {
            if ((_values[i].vflags & VF_PARAM_UNSAVED) == 0) continue;
            if (save(var(i)) == SET_OK) saved++; else bad++;
        }
        if (refused != nullptr) *refused = bad;
        return saved;
    }

    uint16_t Bank::unsaved_override_count() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++)
            if ((_values[i].vflags & VF_OVR_UNSAVED) != 0) n++;
        return n;
    }

    uint16_t Bank::unsaved_count() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++)
            if ((_values[i].vflags & VF_PARAM_UNSAVED) != 0) n++;
        return n;
    }

    SetResult Bank::set(Var h, const char* text) {
        const Variable* d = h.meta();
        if (text == nullptr) return SET_INVALID;
        Value* slotp = _editable_slot(d);
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

        slot.vflags = (uint8_t)(slot.vflags | VF_PARAM_UNSAVED);
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

    SetResult Bank::save_override(Var h) {
        const uint16_t row = _row_of(h);
        if (row == VAR_NONE || !_values[row].ovr_on) return SET_INVALID;
        if (!_persist_override(h)) return SET_NOT_PERSISTED;
        _values[row].vflags = (uint8_t)(_values[row].vflags & ~VF_OVR_UNSAVED);
        return SET_OK;
    }

    uint16_t Bank::save_overrides(uint16_t* refused) {
        uint16_t saved = 0, bad = 0;
        for (uint16_t i = 0; i < _count; i++) {
            if ((_values[i].vflags & VF_OVR_UNSAVED) == 0) continue;
            if (save_override(var(i)) == SET_OK) saved++; else bad++;
        }
        if (refused != nullptr) *refused = bad;
        return saved;
    }

    void Bank::_mark_override_unsaved(Var h) {
        const uint16_t row = _row_of(h);
        if (row == VAR_NONE) return;
        _values[row].vflags = (uint8_t)(_values[row].vflags | VF_OVR_UNSAVED);
    }

    bool Bank::_sync_override_mute(uint16_t i) {
        // VAR_NONE lands here as an out-of-range index, so a sink handle is a
        // no-op rather than a special case at each caller.
        if (i >= _count || desc(i).role != ROLE_OUTPUT) return false;
        Value& v = _values[i];
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
        _values[row].vflags = (uint8_t)(_values[row].vflags & ~VF_OVR_UNSAVED);
        // Forget the stored copy too, or the override comes back at the next
        // boot. Unconditional: an override cleared before it was ever saved may
        // still have an older durable copy underneath it.
        if (_store != nullptr) (void)_store->clear_override(*h.meta());
    }

    // Which row a descriptor IS, recovered from its address. The segments are
    // disjoint arrays, so this asks each in turn; there are a handful, and every
    // caller is on a cold path (a persist, a parameter edit).
    uint16_t Bank::_row_of(const Variable* d) const {
        for (uint8_t s = 0; s < _segments; ++s) {
            if (d < _seg[s] || d >= _seg[s] + _seg_len[s]) continue;
            const uint16_t p = (uint16_t)(_seg_base[s] + (uint16_t)(d - _seg[s]));
            // The segments give a PHYSICAL row; _values is indexed logically, so
            // a redeclaration earlier in the table shifts the two apart. _row
            // ascends, so the logical row is a binary search away - and a miss
            // means this descriptor IS the shadowed one, which is no longer a
            // row of this bank.
            if (_shadowed == 0) return p;
            uint16_t lo = 0, hi = _count;
            while (lo < hi) {
                const uint16_t mid = (uint16_t)(lo + (hi - lo) / 2);
                if (_row[mid] < p) lo = (uint16_t)(mid + 1);
                else               hi = mid;
            }
            return (lo < _count && _row[lo] == p) ? lo : VAR_NONE;
        }
        return VAR_NONE;   // the sink, or a descriptor from another bank
    }

    uint16_t Bank::_row_of(Var h) const { return _row_of(h.meta()); }

    // The slot behind an EDITABLE row: a parameter of this bank's, not read-only.
    // Anything else is not a thing set() may write.
    Value* Bank::_editable_slot(const Variable* d) const {
        if (d->role != ROLE_PARAM || (d->flags & F_RDONLY) != 0) return nullptr;
        const uint16_t row = _row_of(d);
        return (row == VAR_NONE) ? nullptr : &_values[row];
    }

    bool Bank::_persist_override(Var h) {
        const uint16_t row = _row_of(h);
        if (_store == nullptr || row == VAR_NONE) return false;
        const Variable* d = h.meta();
        if (!_store->save_override(*d, _values[row])) {
            if (_logger != nullptr)
                _logger->critical(Logs::STORAGE, "override not persisted\n'%s'", d->name);
            return false;
        }
        return true;
    }

    uint16_t Bank::load_overrides() {
        if (_store == nullptr) return 0;
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++) {
            const Variable& d = desc(i);
            if (!_store->load_override(d, _values[i])) continue;
            _values[i].ovr_on = true;
            // It came OUT of the store, so it is durable by definition.
            _values[i].vflags = (uint8_t)(_values[i].vflags & ~VF_OVR_UNSAVED);
            // Restored while the system still holds control, i.e. with the mode
            // at CONTROL_DISABLED, so this is exactly the window the mute covers:
            // an output override must not reach the field until control does.
            (void)_sync_override_mute(i);
            if (d.role == ROLE_OUTPUT) _values[i].vflags |= VF_FIELD_DIRTY;
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
        for (uint16_t i = 0; i < _count; i++) {
            const Variable& d = desc(i);
            if (!retainable(d)) continue;
            if (!_store->load_retained(d, _values[i])) continue;
            // Q_GOOD, because it is: the value a totalizer carried when the unit
            // went down is what to carry on from. Q_NO_DATA would send every
            // reader to the default this restore exists to avoid.
            _values[i].qual = Q_GOOD;
            n++;
        }
        return n;
    }

    uint16_t Bank::save_retained() {
        if (_store == nullptr) return 0;
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++) {
            const Variable& d = desc(i);
            if (!retainable(d)) continue;
            // The LIVE value, never the override: a totalizer forced to 1000 to
            // test an alarm must not enter the plant's history.
            // A refused write is already counted by HAL::Storage, which is what
            // alarm_flash_error watches.
            if (_store->save_retained(d, _values[i])) n++;
        }
        return n;
    }

    uint16_t Bank::clear_overrides() {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++) {
            if (!_values[i].ovr_on) continue;
            _values[i].ovr_on = false;
            _values[i].vflags = (uint8_t)(_values[i].vflags & ~VF_OVR_UNSAVED);
            if (_store != nullptr) (void)_store->clear_override(desc(i));
            // The field owes the programs' demand again; a release never written
            // out would leave the actuator where the maintainer left it.
            if (desc(i).role == ROLE_OUTPUT)
                _values[i].vflags |= VF_FIELD_DIRTY;
            n++;
        }
        return n;
    }

    uint16_t Bank::override_count() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++) if (_values[i].ovr_on) n++;
        return n;
    }

}}  // namespace Botanik::Vars

namespace Botanik { namespace Vars {

    void Bank::_expire_task(Component* c, const Tasks::Tick& t) {
        static_cast<Bank*>(c)->expire_stale(t.now_ms);
    }

    void Bank::_reject_task(Component* c, const Tasks::Tick&) {
        (void)static_cast<Bank*>(c)->reject_implausible();
    }

    void Bank::on_setup(uint64_t /*now_ms*/) {
        load_params();
        // After the parameters, so a restored override shadows the setting
        // rather than being loaded over.
        const uint16_t n = load_overrides();
        if (n != 0 && _logger != nullptr)
            _logger->critical(Logs::BOOT,
                              "%u override(s) restored from storage\n"
                              "the field is not being read or driven normally",
                              (unsigned)n);
        // Retained totals last: untouched by either step above.
        const uint16_t r = load_retained();
        if (r != 0 && _logger != nullptr)
            _logger->notice(Logs::BOOT, "%u retained value(s) restored",
                            (unsigned)r);
    }

    uint32_t Bank::_retain_ms() const {
        const VarRef r = find("sys_retain_sec");
        const int32_t sec = (r.index != VAR_NONE)
                                ? (int32_t)var(r).i()
                                : (int32_t)(Config::RETAIN_SAVE_MS / 1000u);
        return sec > 0 ? (uint32_t)sec * 1000u : 0u;
    }

    void Bank::_apply_log_level() {
        if (_logger == nullptr) return;
        const VarRef r = find("sys_log_level");
        const int32_t want = (r.index != VAR_NONE) ? (int32_t)var(r).i()
                                                   : (int32_t)Config::LOG_MIN_LEVEL;
        // The row is a T_ENUM over log_level_label(), so only the Config
        // fallback arrives unvetted - hence the guard rather than a bare cast.
        if (Logs::log_level_label((uint64_t)want) != nullptr) {
            const Logs::LogLevel lvl = (Logs::LogLevel)want;
            if (lvl != _logger->min_level()) _logger->set_min_level(lvl);
        }
        // Then each sink's floor, from the row that sink names: a page cannot
        // reach the bank and should not learn how. Live, no commit serial - a
        // level takes effect on the next entry.
        for (uint8_t i = 0; i < _logger->writer_count(); i++) {
            Logs::LogWriter* w = _logger->writer(i);
            if (w == nullptr || w->level_param() == nullptr) continue;
            const VarRef fr = find(w->level_param());
            if (fr.index == VAR_NONE) continue;
            const int32_t f = (int32_t)var(fr).i();
            if (f < 0 || Logs::log_level_label((uint64_t)f) == nullptr) continue;
            const Logs::LogLevel want_lvl = (Logs::LogLevel)f;
            if (want_lvl != w->min_level()) w->set_min_level(want_lvl);
        }
    }

    void Bank::on_config(uint64_t now_ms) {
        _apply_log_level();
        const uint32_t every = _retain_ms();
        // 0 turns the cadence off: the way down and `save` are then the whole
        // mechanism.
        if (every == 0) return;
        // A cadence, not a write-through: a flash erase per beat wears the part
        // out in days.
        if (_last_retain_ms != 0 && (now_ms - _last_retain_ms) < every) return;
        _last_retain_ms = now_ms;
        (void)save_retained();
    }

    void Bank::_config_task(Component* c, const Tasks::Tick& t) {
        static_cast<Bank*>(c)->on_config(t.now_ms);
    }

    void Bank::register_tasks() {
        // The parameter image everything below reads its config from.
        _tasks->add(*this, &Bank::_config_task, "params",
              Tasks::GROUP_SLOW, Tasks::PHASE_CONFIG);
        // A full linear scan of the variable table, resolving timeouts measured
        // in seconds - which is why it is GROUP_BACKGROUND and not the beat.
        _tasks->add(*this, &Bank::_expire_task, "expire_stale",
              Tasks::GROUP_BACKGROUND, Tasks::PHASE_INPUT);
        // Beside it: a reading can be fresh and still be nonsense. A dead
        // 4-20 mA loop reads full scale, which no timeout catches - the
        // declared plausibility band does.
        _tasks->add(*this, &Bank::_reject_task, "reject_implausible",
              Tasks::GROUP_BACKGROUND, Tasks::PHASE_INPUT);
    }

}}  // namespace Botanik::Vars
