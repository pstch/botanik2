/**
 * @file model.cpp
 * @brief The Bank: laying the RAM out over the generated table, finding a row
 *        by name, the tallies and the expiry sweep. Plus Value's conversions.
 */

#include "libbotanik/vars/model.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_VARS

#include <cstring>

#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/utils/text.h"
#include "libbotanik/vars/edit.h"     // copy_bounded

namespace Botanik { namespace Vars {

    /** The one bank a Ref reads. Defined here rather than beside the firmware:
     *  a build with no site still links the laws. */
    Bank* runtime_bank = nullptr;

    // Named so a bad index is obvious in a log or a debugger.
    const Descriptor sink_descriptor = {
        Scalar::U(0), Payload(), "<none>", "", "", "",
        { 0, 0, 0, 0, T_U8, 0, 0 }, 0, 0, K_STATUS, T_U8, nullptr, 0, 0
    };


    void Bank::init(const Descriptor* rows, const uint16_t* order, uint16_t count,
                    uint16_t param_first, uint16_t param_count) {
        _rows        = rows;
        _order       = order;
        _count       = (rows != nullptr && count <= Config::VAR_MAX) ? count : 0;
        _param_first = param_first;
        _param_count = param_count;
        BOTANIK_ASSERT(rows == nullptr || count <= Config::VAR_MAX);

        // Text rows first, in table order, so a slot's position is derived and
        // never declared.
        _pool_used  = 0;
        _wire_count = 0;
        for (uint16_t i = 0; i < _count; i++) {
            const Descriptor& d = _rows[i];
            Slot& s = _slots[i];
            s = Slot();
            s.v = d.def;
            s.str_off  = 0;
            s.wire_off = WIRE_NONE;

            const uint16_t cap = text_slot(d).cap;
            if (cap != 0) {
                // STR_NO_SLOT, not 0: offset 0 is the first text row's slot, so
                // an overflowed row parked there would share its bytes. The
                // generator sizes the pool, so this is the belt to its braces.
                if ((uint32_t)_pool_used + cap > Config::STR_POOL_BYTES) {
                    s.str_off = STR_NO_SLOT;
                } else {
                    s.str_off  = _pool_used;
                    _pool_used = (uint16_t)(_pool_used + cap);
                    copy_bounded(_pool + s.str_off, cap, d.def.s);
                }
            }

            // A wire cell for every row that converts: the rest read their own
            // value as the wire, so they need no storage.
            if (analog(d) && _wire_count < Config::ANALOG_ROWS_MAX) {
                s.wire_off = _wire_count;
                _wire[_wire_count++] = Scalar();
            }
        }
        BOTANIK_ASSERT(_pool_used <= Config::STR_POOL_BYTES);
    }


    uint16_t Bank::_lookup(const char* name, uint16_t len) const {
        if (_count == 0) return VAR_NONE;
        // An ad-hoc table has no order beside it: walk it. A site's is
        // generated, and every lookup below binary-searches that.
        if (_order == nullptr) {
            for (uint16_t i = 0; i < _count; i++)
                if (Utils::compare_bounded_ci(_rows[i].name, name, len) == 0) return i;
            return VAR_NONE;
        }
        // Lower bound over the generated name order, then one equality check.
        uint16_t lo = 0, hi = _count;
        while (lo < hi) {
            const uint16_t mid = (uint16_t)(lo + (hi - lo) / 2);
            if (Utils::compare_bounded_ci(_rows[_order[mid]].name, name, len) < 0)
                lo = (uint16_t)(mid + 1);
            else
                hi = mid;
        }
        if (lo >= _count) return VAR_NONE;
        const uint16_t row = _order[lo];
        return Utils::compare_bounded_ci(_rows[row].name, name, len) == 0 ? row
                                                                          : VAR_NONE;
    }

    uint16_t Bank::find(const char* name) const {
        return (name != nullptr) ? _lookup(name, 0) : VAR_NONE;
    }

    uint16_t Bank::find(const char* name, uint16_t len) const {
        if (name == nullptr || len == 0) return VAR_NONE;
        return _lookup(name, len);
    }


    uint16_t Bank::_tally(uint8_t bit) const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++)
            if ((_slots[i].flags & bit) != 0) n++;
        return n;
    }

    uint16_t Bank::unsaved_count() const { return _tally(S_PARAM_UNSAVED); }
    uint16_t Bank::owed_count() const    { return _tally(S_PARAM_OWED); }
    uint16_t Bank::unsaved_override_count() const { return _tally(S_OVR_UNSAVED); }


    void Bank::expire_stale(uint64_t now_ms) {
        const uint32_t now = (uint32_t)now_ms;
        for (uint16_t i = 0; i < _count; i++) {
            const Descriptor& d = _rows[i];
            if (d.kind != K_INPUT || d.timeout_ms == 0) continue;
            const Var h = var(i);
            if (!h.usable()) continue;
            if ((uint32_t)(now - _slots[i].stamp_ms) > d.timeout_ms)
                h.set_quality(Q_NONE);
        }
    }

    void Bank::_expire_task(Component* c, const Tasks::Tick& t) {
        static_cast<Bank*>(c)->expire_stale(t.now_ms);
    }

    void Bank::register_tasks() {
        // A full linear scan of the variable table, resolving timeouts measured
        // in seconds - which is why it runs at VAR_EXPIRY_MS and not the beat.
        _tasks->add(*this, &Bank::_expire_task, "sys_expire_stale",
                    Config::VAR_EXPIRY_MS, Tasks::PHASE_INPUT);
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
            BOTANIK_ASSERT(!str || !"a T_STR has no scalar value; use Var::text()");
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
        if (d == nullptr || t != T_ENUM || d->labels == nullptr) return nullptr;
        return d->labels(as_u64());
    }


    void copy_bounded(char* dst, uint16_t cap, const char* src) {
        if (dst == nullptr || cap == 0) return;
        if (src == nullptr) { dst[0] = 0; return; }
        uint16_t n = 0;
        while (src[n] != 0 && n + 1u < cap) { dst[n] = src[n]; n++; }
        dst[n] = 0;
    }

    SetResult parse_for(const Descriptor& d, const char* text,
                        const Band* band, Scalar* out) {
        // d.labels goes in so a labelled enum answers to its own words, and so a
        // code it cannot name is refused. That refusal is NOT the band and so
        // applies even unbounded: an override may push a reading past what is
        // plausible, but there is no such thing as an enum value outside its
        // labels - it would render as a bare number and mean nothing.
        if (parse_scalar(d.type, text, out, d.labels) != PARSE_OK) return SET_INVALID;
        if (band != nullptr && band->set() &&
            !in_range(d.type, *out, band->lo, band->hi)) return SET_INVALID;
        return SET_OK;
    }

}}  // namespace Botanik::Vars

#endif // BOTANIK_FEATURE_VARS
