/**
 * @file model.cpp
 * @brief Bank: seeding, lookup, staleness expiry and the fail-safe sweep.
 *
 * Each is ONE loop over the one table, with a role switch; no sweep names a
 * subset of rows by hand.
 */

#include "libbotanik/vars/model.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_VARS

                            // Bank / Var / Descriptor

#include "libbotanik/tasks/tasks.h"


#include <cstring>
#include "libbotanik/logs/logger.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/utils/text.h"
#include "libbotanik/vars/params.h"   // analog_values - the calibration is parameters

namespace Botanik { namespace Vars {

    // Named so a bad index is obvious in a log or a debugger.
    const Descriptor Bank::_sink_d = {
        Scalar::U(0), "<none>", "", "", "",
        Payload(),
        { XP_NONE, T_U8, 0, 0, 0, 0, 1.0f, 0.0f },
        0, 0, nullptr, T_U8, ROLE_COMPUTED, SCOPE_G, 0, 0, {}
    };
    Slot Bank::_sink_v = {};

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

    bool Bank::register_vars(const Descriptor* rows, uint16_t count) {
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
        // Identity HERE, not at freeze(): descriptor() goes through _row from the
        // moment a row exists, and a bank that is read before it is frozen (or
        // never frozen at all, as several tests do) would otherwise index the
        // segments through uninitialised memory.
        for (uint16_t i = _count; i < _count + count; i++) _row[i] = i;
        _count = (uint16_t)(_count + count);
        return true;
    }

    void Bank::_resolve_shadows() {
        // _row is still identity from register_vars(), so descriptor() reads
        // physically here and the sort below can use the ordinary accessors.
        _build_index();       // by name, so a redeclared pair lands adjacent

        uint16_t i = 0;
        while (i < _count) {
            uint16_t j = (uint16_t)(i + 1);
            while (j < _count &&
                   std::strcmp(descriptor(_index[i]).name, descriptor(_index[j]).name) == 0) j++;
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
                                        descriptor(win).name);
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

    Value Bank::wire(VarRef r) const {
        if (r.index >= _count) return Value{ Scalar(), &_sink_d, T_U8, Q_NONE };
        const Descriptor& d = descriptor(r.index);
        Slot& s = _slots[r.index];
        // No binding, no wire: a parameter or a computed row has none.
        if (d.bind.transport == XP_NONE) return Value{ Scalar(), &d, d.type, Q_NONE };
        // 1:1, so the wire IS the value - nothing is stored twice.
        if (s.wire_off == WIRE_NONE) return Value{ s.v, &d, d.type, s.qual };
        // An OUTPUT is scaled on the way out, and nothing ever publishes a
        // count inward for it: its wire value is what WOULD be sent.
        if (d.role == ROLE_OUTPUT) {
            const AnalogValues a = analog_values(this, analog_cal(d));
            return Value{ from_f32(d.bind.wire, a.to_raw(var(r).as_f32())),
                          &d, d.bind.wire, s.qual };
        }
        return Value{ _wire[s.wire_off], &d, d.bind.wire, s.qual };
    }

    void Bank::publish_wire(VarRef r, Scalar raw, uint64_t now_ms) {
        if (r.index >= _count) return;
        const Descriptor& d = descriptor(r.index);
        Slot& s = _slots[r.index];
        const AnalogCal* c = analog_cal(d);
        if (c == nullptr) {                       // 1:1: the count IS the value
            var(r).publish(raw, now_ms);
            return;
        }
        if (s.wire_off != WIRE_NONE) _wire[s.wire_off] = raw;

        const AnalogValues a = analog_values(this, c);
        const float eng = a.to_eng(as_f32(d.bind.wire, raw));
        s.v        = from_f32(d.type, eng);
        s.stamp_ms = now_ms;
        // The band lives in the calibration's parameters, which is why this is
        // the model's job and not a sweep's: a dead 4-20 mA loop reads full
        // scale, and no timeout catches that.
        s.qual = a.plausible(eng) ? Q_GOOD : Q_IMPLAUSIBLE;
    }

    void Bank::_resolve_cals() {
        for (uint16_t i = 0; i < _count; i++) {
            const AnalogCal* c = analog_cal(descriptor(i));
            if (c == nullptr || analog_refs(c) != nullptr) continue;   // none, or seen
            if (_cal_count >= Config::ANALOG_BLOCKS_MAX) {
                // The tail would fall back to a name lookup per read, which is
                // slow rather than wrong - but silently so.
                BOTANIK_ASSERT(!"more AnalogCal blocks than ANALOG_BLOCKS_MAX");
                if (_logger != nullptr)
                    _logger->critical(Logs::BOOT,
                                      "analog blocks past %u: raise ANALOG_BLOCKS_MAX",
                                      (unsigned)Config::ANALOG_BLOCKS_MAX);
                return;
            }
            CalRefs& e = _cal[_cal_count++];
            e.block = c;
            const Tunable* t[6] = { &c->raw_min, &c->raw_max, &c->eng_min,
                                    &c->eng_max, &c->min,     &c->max };
            for (uint8_t k = 0; k < 6; k++)
                e.ref[k] = (t[k]->param != nullptr) ? find(t[k]->param)
                                                    : VarRef{ VAR_NONE };
        }
    }

    void Bank::freeze() {
        // BEFORE init(): a shadowed row must not be given a pool slot or a value
        // slot it will never be read through.
        _resolve_shadows();
        init();               // pool layout, then every slot from its default
        _find_param_block();
        _build_index();
        _resolve_cals();      // needs the index: the tunables are found by name
        // The ROLE_PARAM rows must be ONE run - that is what lets the PARAMS
        // page and the shell address them 0-based. Registration order decides
        // it, so this is where it can be checked; it used to be a convention
        // with a test to catch it afterwards. Naming the row is the whole point:
        // "parameters are not contiguous" sends nobody anywhere.
        for (uint16_t i = (uint16_t)(_param_first + _param_count); i < _count; i++) {
            if (descriptor(i).role != ROLE_PARAM) continue;
            BOTANIK_ASSERT(!"a parameter outside the parameter block");
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT,
                                  "param '%s' is outside the parameter block\n"
                                  "register the parameter segment first",
                                  descriptor(i).name);
            _param_stray++;
        }
    }

    void Bank::init() {
        // Text rows first, in table order, so a slot's position is derived and
        // never declared.
        _pool_used = 0;
        for (uint16_t i = 0; i < _count; i++) {
            const uint16_t cap = text_slot(descriptor(i)).cap;
            if (cap == 0) { _slots[i].str_off = 0; continue; }
            if ((uint32_t)_pool_used + cap > Config::STR_POOL_BYTES) {
                // Out of pool. STR_NO_SLOT, not 0: offset 0 is the first text
                // row's slot, so an overflowed row parked there would share its
                // bytes. text() refuses on the sentinel, so the row reads
                // empty and every write bounces.
                _slots[i].str_off = STR_NO_SLOT;
                _pool_overflow++;
                continue;
            }
            _slots[i].str_off = _pool_used;
            _pool_used = (uint16_t)(_pool_used + cap);
        }

        // A wire slot for every row that scales: the rest read their own value
        // as the wire, so they need no storage.
        _wire_count = 0;
        for (uint16_t i = 0; i < _count; i++) {
            if (analog_cal(descriptor(i)) == nullptr) {
                _slots[i].wire_off = WIRE_NONE;
                continue;
            }
            if (_wire_count >= Config::ANALOG_ROWS_MAX) {
                BOTANIK_ASSERT(!"more analog rows than ANALOG_ROWS_MAX");
                if (_logger != nullptr)
                    _logger->critical(Logs::BOOT,
                                      "analog rows past %u: raise ANALOG_ROWS_MAX",
                                      (unsigned)Config::ANALOG_ROWS_MAX);
                _slots[i].wire_off = WIRE_NONE;
                continue;
            }
            _slots[i].wire_off = _wire_count;
            _wire[_wire_count++] = Scalar();
        }

        for (uint16_t i = 0; i < _count; i++) {
            const Descriptor& d = descriptor(i);
            _slots[i].v        = d.def;
            _slots[i].stamp_ms = 0;
            // Overrides are a live-session tool; a fresh table has none.
            _slots[i].ovr_on   = false;
            // A parameter always HAS a value (its factory default until the
            // store loads one), so it starts good. Everything else starts with
            // nothing written, so a default is never shown as a measurement.
            _slots[i].qual     = (d.role == ROLE_PARAM) ? Q_GOOD : Q_NO_DATA;
            // Nothing demanded yet, so nothing is owed to the field: an output
            // starts at its safe state and the driver's first unconditional
            // rewrite is what puts it there.
            _slots[i].vflags   = 0;
            // Explicit, not left to zero fill: zero is program 0, not "none".
            _slots[i].writer   = WRITER_NONE;

            uint16_t cap = 0;
            char* txt = text(Var(&d, &_slots[i]), &cap);
            if (txt != nullptr) copy_bounded(txt, cap, d.def.s);
        }
    }

    void Bank::_find_param_block() {
        _param_first = 0;
        _param_count = 0;
        for (uint16_t i = 0; i < _count; i++) {
            if (descriptor(i).role != ROLE_PARAM) continue;
            _param_first = i;
            while (i < _count && descriptor(i).role == ROLE_PARAM) { _param_count++; i++; }
            return;
        }
    }

    char* Bank::text(Var h, uint16_t* cap_out) const {
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
        const int c = Utils::compare_bounded(descriptor(a).name, descriptor(b).name, 0);
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
                if (Utils::compare_bounded(descriptor(i).name, name, len) == 0) return i;
            return VAR_NONE;
        }
        // Lower bound, then one equality check: with ties ordered by row number,
        // the first match is the lowest row carrying that name.
        uint16_t lo = 0, hi = _count;
        while (lo < hi) {
            const uint16_t mid = (uint16_t)(lo + (hi - lo) / 2);
            if (Utils::compare_bounded(descriptor(_index[mid]).name, name, len) < 0)
                lo = (uint16_t)(mid + 1);
            else
                hi = mid;
        }
        if (lo >= _count) return VAR_NONE;
        const uint16_t row = _index[lo];
        return (Utils::compare_bounded(descriptor(row).name, name, len) == 0) ? row : VAR_NONE;
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
            char* txt = text(var(i), &cap);
            (void)_store->load(descriptor(i), _slots[i], txt, cap);
            // Whatever the row now holds came from the store (or is the seeded
            // default), so nothing is owed a write.
            _slots[i].vflags = (uint8_t)(_slots[i].vflags & ~VF_PARAM_UNSAVED);
        }
    }

    void Bank::set_effective_control(ControlMode now, bool erase_overrides) {
        const ControlMode before = _mode;
        _mode = now;
        _apply_control(before, erase_overrides);
    }

    void Bank::_apply_control(ControlMode before, bool erase_overrides) {
        const ControlMode now = _mode;
        if (now == before && !erase_overrides) return;

        // Drop to safe on the way out of ENABLED only; a mode change between the
        // two withheld modes finds the outputs already there.
        const bool to_safe = (before == CONTROL_ENABLED && now != CONTROL_ENABLED);

        for (uint16_t i = 0; i < _count; i++) {
            if (descriptor(i).role != ROLE_OUTPUT) continue;
            Slot& v = _slots[i];

            if (erase_overrides && v.ovr_on) {
                // Wipe the stored copy too: overrides persist, so clearing only
                // RAM hands the actuator back to whoever forced it at the next
                // boot.
                if (_store != nullptr) (void)_store->clear_override(descriptor(i));
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
            const Descriptor& d = descriptor(i);
            if (d.role != ROLE_INPUT || d.timeout_ms == 0) continue;
            if (_slots[i].qual == Q_GOOD &&
                (now_ms - _slots[i].stamp_ms) > d.timeout_ms)
                _slots[i].qual = Q_STALE;
        }
    }

    namespace {
        /** @brief Clamp to a signed range, NaN to 0. */
        int64_t clamp_i(double x, int64_t lo, int64_t hi) {
            if (!(x == x)) return 0;
            if (x <= (double)lo) return lo;
            if (x >= (double)hi) return hi;
            return (int64_t)x;
        }
        uint64_t clamp_u(double x, uint64_t hi) {
            if (!(x == x) || x <= 0.0) return 0;
            if (x >= (double)hi) return hi;
            return (uint64_t)x;
        }
        bool is_str(const Descriptor* d) {
            // A T_STR keeps its text in the pool, so there is no number here.
            const bool str = (d != nullptr && d->type == T_STR);
            BOTANIK_ASSERT(!str || !"a T_STR has no scalar value; use Bank::text()");
            return str;
        }
    }  // namespace

    int64_t Value::as_i64() const {
        if (d == nullptr || is_str(d)) return 0;
        if (type_real(t))   return clamp_i((double)v.f, INT64_MIN, INT64_MAX);
        if (type_signed(t)) return v.i;
        return (v.u > (uint64_t)INT64_MAX) ? INT64_MAX : (int64_t)v.u;
    }

    uint64_t Value::as_u64() const {
        if (d == nullptr || is_str(d)) return 0;
        if (type_real(t))   return clamp_u((double)v.f, UINT64_MAX);
        if (type_signed(t)) return (v.i < 0) ? 0u : (uint64_t)v.i;
        return v.u;
    }

    float Value::as_f32() const {
        if (d == nullptr || is_str(d)) return 0.0f;
        return Vars::as_f32(t, v);
    }

    bool Value::as_bool() const { return as_i64() != 0; }

    int8_t  Value::as_i8()  const { return (int8_t) clamp_i((double)as_i64(), INT8_MIN,  INT8_MAX); }
    int16_t Value::as_i16() const { return (int16_t)clamp_i((double)as_i64(), INT16_MIN, INT16_MAX); }
    int32_t Value::as_i32() const { return (int32_t)clamp_i((double)as_i64(), INT32_MIN, INT32_MAX); }
    uint8_t  Value::as_u8()  const { return (uint8_t) clamp_u((double)as_u64(), UINT8_MAX); }
    uint16_t Value::as_u16() const { return (uint16_t)clamp_u((double)as_u64(), UINT16_MAX); }
    uint32_t Value::as_u32() const { return (uint32_t)clamp_u((double)as_u64(), UINT32_MAX); }

    const char* Value::label() const {
        if (d == nullptr || t != T_ENUM || d->str == nullptr) return nullptr;
        return d->str(as_u64());
    }

    bool value_implausible(const Descriptor& d, const Scalar& v) {
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
        // A row declared VAR_IN_OUT is read-modify-written by several programs
        // on purpose, so it records no owner at all: naming one would make the
        // MONITOR page report whichever program happened to write last as THE
        // writer of a row that has no single one.
        if ((_slots[r.index].vflags & VF_SHARED) != 0) return true;
        uint8_t& held = _slots[r.index].writer;
        if (held == who) return true;               // the usual case, every beat
        if (held == WRITER_NONE) { held = who; return true; }

        // A second author. Counted, not logged: this runs in the control phase,
        // from a class with no logger. The driver reads the count on the edge
        // and names the row.
        //
        // Counted ONCE PER ROW, not per collision: both programs keep writing
        // every beat, so a per-collision count would rise forever.
        uint8_t& vf = _slots[r.index].vflags;
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
        return (r.index < _count) ? _slots[r.index].writer
                                           : WRITER_NONE;
    }

    // Writing

    namespace {
        /** @brief Parse @p text as @p d's type into @p out, optionally holding
         *  it to the declared band. Shared by set() and override_set(), which
         *  differ only in whether the band applies. */
        SetResult parse_for(const Descriptor& d, const char* text, bool bounded,
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

    SetResult Bank::param_check(Var h, Scalar v) const {
        const Descriptor* d = h.meta();
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

    SetResult Bank::param_set_scalar(Var h, Scalar v) {
        const SetResult r = set_deferred(h, v);
        if (r != SET_OK) return r;
        // Live now, durable when the operator says so.
        Slot* slot = _editable_slot(h.meta());
        slot->vflags = (uint8_t)(slot->vflags | VF_PARAM_UNSAVED);
        return SET_OK;
    }

    SetResult Bank::set_deferred(Var h, Scalar v) {
        if (param_check(h, v) != SET_OK) return SET_INVALID;
        const Descriptor* d = h.meta();
        Slot* slot = _editable_slot(d);
        if (slot == nullptr) return SET_INVALID;
        slot->v     = v;
        slot->qual  = Q_GOOD;
        // Live now, durable when somebody saves. See the header: it is the one
        // rule that makes a setting able to break its own console recoverable.
        slot->vflags = (uint8_t)(slot->vflags | VF_PARAM_UNSAVED);
        return SET_OK;
    }

    SetResult Bank::param_clear(Var h) {
        const Descriptor* d = h.meta();
        Slot* slot = _editable_slot(d);
        if (slot == nullptr) return SET_INVALID;
        // Nothing owed, nothing to forget. Not an error: `clear` on a settled
        // row is the no-op an operator expects, not a complaint.
        if ((slot->vflags & VF_PARAM_UNSAVED) == 0) return SET_OK;

        uint16_t cap = 0;
        char* txt = text(h, &cap);
        // Exactly what a reboot would give it: the stored copy, or the factory
        // default when nothing was ever saved. The stored copy is NOT touched -
        // that is the whole difference from param_reset().
        if (_store == nullptr || !_store->load(*d, *slot, txt, cap)) {
            slot->v = d->def;
            if (txt != nullptr) copy_bounded(txt, cap, d->def.s);
        }
        slot->qual   = Q_GOOD;                // a parameter always has a value
        slot->vflags = (uint8_t)(slot->vflags & ~VF_PARAM_UNSAVED);
        return SET_OK;
    }

    SetResult Bank::param_reset(Var h) {
        const Descriptor* d = h.meta();
        Slot* slot = _editable_slot(d);
        if (slot == nullptr) return SET_INVALID;
        if (param_retained(*d)) return SET_INVALID;

        slot->v    = d->def;
        slot->qual = Q_GOOD;                  // a parameter always has a value
        uint16_t cap = 0;
        char* txt = text(h, &cap);
        if (txt != nullptr) copy_bounded(txt, cap, d->def.s);
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

    SetResult Bank::param_save(Var h) {
        const Descriptor* d = h.meta();
        Slot* slot = _editable_slot(d);
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
            if ((_slots[i].vflags & VF_PARAM_UNSAVED) == 0) continue;
            if (param_save(var(i)) == SET_OK) saved++; else bad++;
        }
        if (refused != nullptr) *refused = bad;
        return saved;
    }

    uint16_t Bank::unsaved_override_count() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++)
            if ((_slots[i].vflags & VF_OVR_UNSAVED) != 0) n++;
        return n;
    }

    uint16_t Bank::unsaved_count() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++)
            if ((_slots[i].vflags & VF_PARAM_UNSAVED) != 0) n++;
        return n;
    }

    SetResult Bank::param_check(Var h, const char* text) const {
        const Descriptor* d = h.meta();
        if (text == nullptr || d == nullptr) return SET_INVALID;
        if (_editable_slot(d) == nullptr) return SET_INVALID;
        if (d->type == T_STR) {
            uint16_t cap = 0;
            const char* dst = this->text(h, &cap);
            if (dst == nullptr) return SET_INVALID;
            return validate_str(text_slot(*d).kind, text, cap) ? SET_OK
                                                               : SET_INVALID;
        }
        Scalar parsed;
        return (parse_for(*d, text, /*bounded=*/true, &parsed) == SET_OK)
                   ? SET_OK : SET_INVALID;
    }

    SetResult Bank::param_set(Var h, const char* text) {
        const Descriptor* d = h.meta();
        if (text == nullptr) return SET_INVALID;
        Slot* slotp = _editable_slot(d);
        if (slotp == nullptr) return SET_INVALID;
        Slot& slot = *slotp;

        if (d->type == T_STR) {
            uint16_t cap = 0;
            char* dst = this->text(h, &cap);
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

    SetResult Bank::_persist_param(Var h, const Descriptor& d, Slot& slot) {
        if (_store == nullptr) return SET_NOT_PERSISTED;
        uint16_t save_cap = 0;
        char* save_text = text(h, &save_cap);
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
        const Descriptor* d = h.meta();
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
        if (row == VAR_NONE || !_slots[row].ovr_on) return SET_INVALID;
        if (!_persist_override(h)) return SET_NOT_PERSISTED;
        _slots[row].vflags = (uint8_t)(_slots[row].vflags & ~VF_OVR_UNSAVED);
        return SET_OK;
    }

    uint16_t Bank::save_overrides(uint16_t* refused) {
        uint16_t saved = 0, bad = 0;
        for (uint16_t i = 0; i < _count; i++) {
            if ((_slots[i].vflags & VF_OVR_UNSAVED) == 0) continue;
            if (save_override(var(i)) == SET_OK) saved++; else bad++;
        }
        if (refused != nullptr) *refused = bad;
        return saved;
    }

    void Bank::_mark_override_unsaved(Var h) {
        const uint16_t row = _row_of(h);
        if (row == VAR_NONE) return;
        _slots[row].vflags = (uint8_t)(_slots[row].vflags | VF_OVR_UNSAVED);
    }

    bool Bank::_sync_override_mute(uint16_t i) {
        // VAR_NONE lands here as an out-of-range index, so a sink handle is a
        // no-op rather than a special case at each caller.
        if (i >= _count || descriptor(i).role != ROLE_OUTPUT) return false;
        Slot& v = _slots[i];
        const uint8_t before = v.vflags;
        v.vflags = (_mode == CONTROL_DISABLED)
                     ? (uint8_t)(before | VF_OVR_MUTED)
                     : (uint8_t)(before & ~VF_OVR_MUTED);
        return v.vflags != before;
    }

    void Bank::override_clear(Var h) {
        h.override_clear();
        const uint16_t row = _row_of(h);
        if (row == VAR_NONE) return;
        _slots[row].vflags = (uint8_t)(_slots[row].vflags & ~VF_OVR_UNSAVED);
        // Forget the stored copy too, or the override comes back at the next
        // boot. Unconditional: an override cleared before it was ever saved may
        // still have an older durable copy underneath it.
        if (_store != nullptr) (void)_store->clear_override(*h.meta());
    }

    // Which row a descriptor IS, recovered from its address. The segments are
    // disjoint arrays, so this asks each in turn; there are a handful, and every
    // caller is on a cold path (a persist, a parameter edit).
    uint16_t Bank::_row_of(const Descriptor* d) const {
        for (uint8_t s = 0; s < _segments; ++s) {
            if (d < _seg[s] || d >= _seg[s] + _seg_len[s]) continue;
            const uint16_t p = (uint16_t)(_seg_base[s] + (uint16_t)(d - _seg[s]));
            // The segments give a PHYSICAL row; _slots is indexed logically, so
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
    Slot* Bank::_editable_slot(const Descriptor* d) const {
        if (d->role != ROLE_PARAM || (d->flags & F_RDONLY) != 0) return nullptr;
        const uint16_t row = _row_of(d);
        return (row == VAR_NONE) ? nullptr : &_slots[row];
    }

    bool Bank::_persist_override(Var h) {
        const uint16_t row = _row_of(h);
        if (_store == nullptr || row == VAR_NONE) return false;
        const Descriptor* d = h.meta();
        if (!_store->save_override(*d, _slots[row])) {
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
            const Descriptor& d = descriptor(i);
            if (!_store->load_override(d, _slots[i])) continue;
            _slots[i].ovr_on = true;
            // It came OUT of the store, so it is durable by definition.
            _slots[i].vflags = (uint8_t)(_slots[i].vflags & ~VF_OVR_UNSAVED);
            // Restored while the system still holds control, i.e. with the mode
            // at CONTROL_DISABLED, so this is exactly the window the mute covers:
            // an output override must not reach the field until control does.
            (void)_sync_override_mute(i);
            if (d.role == ROLE_OUTPUT) _slots[i].vflags |= VF_FIELD_DIRTY;
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
            const Descriptor& d = descriptor(i);
            if (!retainable(d)) continue;
            if (!_store->load_retained(d, _slots[i])) continue;
            // Q_GOOD, because it is: the value a totalizer carried when the unit
            // went down is what to carry on from. Q_NO_DATA would send every
            // reader to the default this restore exists to avoid.
            _slots[i].qual = Q_GOOD;
            n++;
        }
        return n;
    }

    uint16_t Bank::save_retained() {
        if (_store == nullptr) return 0;
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++) {
            const Descriptor& d = descriptor(i);
            if (!retainable(d)) continue;
            // The LIVE value, never the override: a totalizer forced to 1000 to
            // test an alarm must not enter the plant's history.
            // A refused write is already counted by HAL::Storage, which is what
            // alarm_flash_error watches.
            if (_store->save_retained(d, _slots[i])) n++;
        }
        return n;
    }

    uint16_t Bank::clear_overrides() {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++) {
            if (!_slots[i].ovr_on) continue;
            _slots[i].ovr_on = false;
            _slots[i].vflags = (uint8_t)(_slots[i].vflags & ~VF_OVR_UNSAVED);
            if (_store != nullptr) (void)_store->clear_override(descriptor(i));
            // The field owes the programs' demand again; a release never written
            // out would leave the actuator where the maintainer left it.
            if (descriptor(i).role == ROLE_OUTPUT)
                _slots[i].vflags |= VF_FIELD_DIRTY;
            n++;
        }
        return n;
    }

    uint16_t Bank::override_count() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++) if (_slots[i].ovr_on) n++;
        return n;
    }

}}  // namespace Botanik::Vars

namespace Botanik { namespace Vars {

    void Bank::_expire_task(Component* c, const Tasks::Tick& t) {
        static_cast<Bank*>(c)->expire_stale(t.now_ms);
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
        // The row is a T_ENUM over level_label(), so only the Config
        // fallback arrives unvetted - hence the guard rather than a bare cast.
        if (Logs::level_label((uint64_t)want) != nullptr) {
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
            if (f < 0 || Logs::level_label((uint64_t)f) == nullptr) continue;
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
        _tasks->add(*this, &Bank::_config_task, "sys_params",
              Tasks::GROUP_SLOW, Tasks::PHASE_CONFIG);
        // A full linear scan of the variable table, resolving timeouts measured
        // in seconds - which is why it is GROUP_BACKGROUND and not the beat.
        _tasks->add(*this, &Bank::_expire_task, "sys_expire_stale",
              Tasks::GROUP_BACKGROUND, Tasks::PHASE_INPUT);
    }

}}  // namespace Botanik::Vars

#endif // BOTANIK_FEATURE_VARS
