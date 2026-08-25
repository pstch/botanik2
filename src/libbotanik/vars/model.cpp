/**
 * @file model.cpp
 * @brief Bank's core: seeding, reading, the wire layer, staleness expiry and
 *        the fail-safe sweep, plus Value's conversions.
 *
 * Each sweep is ONE loop over the one table, with a role switch; none names a
 * subset of rows by hand. The three surfaces that grew their own file are
 * resolve.cpp (bring-up), params.cpp (editing) and overrides.cpp (forcing).
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
#include "libbotanik/vars/edit.h"     // copy_bounded
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

    // Registration



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
                                ? (int32_t)var(r).as_i64()
                                : (int32_t)(Config::RETAIN_SAVE_MS / 1000u);
        return sec > 0 ? (uint32_t)sec * 1000u : 0u;
    }

    void Bank::_apply_log_level() {
        if (_logger == nullptr) return;
        const VarRef r = find("sys_log_level");
        const int32_t want = (r.index != VAR_NONE) ? (int32_t)var(r).as_i64()
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
            const int32_t f = (int32_t)var(fr).as_i64();
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


    void Bank::register_tasks() {
        // The parameter image everything below reads its config from.
        _tasks->add(*this, "sys_params",
              Tasks::GROUP_SLOW, Tasks::PHASE_CONFIG);
        // A full linear scan of the variable table, resolving timeouts measured
        // in seconds - which is why it is GROUP_BACKGROUND and not the beat.
        _tasks->add(*this, &Bank::_expire_task, "sys_expire_stale",
              Tasks::GROUP_BACKGROUND, Tasks::PHASE_INPUT);
    }

}}  // namespace Botanik::Vars

#endif // BOTANIK_FEATURE_VARS
