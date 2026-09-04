/**
 * @file var.cpp
 * @brief Var: everything a single row does - reading it, publishing to it,
 *        editing it as a parameter, and forcing it.
 *
 * The RAM side only. Every verb with a storage side is a `Vars::Persist` call,
 * and Persist does its RAM half through these.
 */

#include "libbotanik/vars/model.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_VARS

#include <cstring>

#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"
#include "libbotanik/vars/access.h"
#include "libbotanik/vars/edit.h"

namespace Botanik { namespace Vars {

    namespace {
        /** @brief The slot a handle with NO BANK writes into and reads back as
         *  zeros. One object, cleared on every access. */
        Slot g_none;
    }  // namespace

    Slot& Var::_s() const {
        if (_b != nullptr) return _b->slot(_i);
        g_none = Slot();
        return g_none;
    }

    // ---- Quality --------------------------------------------------------------

    Quality Var::quality() const {
        if (null()) return Q_NONE;
        const uint16_t q = quality_row();
        if (q == VAR_NONE) return Q_GOOD;
        const Slot& s = _b->slot(q);
        const Scalar v = ((s.flags & S_OVR_ON) != 0) ? s.ovr : s.v;
        return (v.u > (uint64_t)Q_HELD) ? Q_NONE : (Quality)v.u;
    }

    void Var::set_quality(Quality q) const {
        const uint16_t r = quality_row();
        if (r == VAR_NONE) return;
        Slot& s = _b->slot(r);
        s.v     = Scalar::U((uint64_t)q);
        s.flags = (uint8_t)(s.flags | S_WRITTEN);
    }

    // ---- Bounds ---------------------------------------------------------------

    Scalar Var::_tunable(Scalar fixed, uint16_t row) const {
        if (row == VAR_NONE || _b == nullptr) return fixed;
        return _b->var(row).value().v;
    }

    AnalogValues Var::cal() const {
        AnalogValues v = {};
        const AnalogCal* c = analog_cal(meta());
        if (c == nullptr || _b == nullptr) return v;
        const Scalar  k[6]   = { c->raw_min, c->raw_max, c->eng_min,
                                 c->eng_max, c->min,     c->max };
        float* const  out[6] = { &v.raw_min, &v.raw_max, &v.eng_min,
                                 &v.eng_max, &v.min,     &v.max };
        // The rows are resolved at generate time; the VALUES are read live, so a
        // recalibration lands the moment the parameter commits.
        for (uint8_t i = 0; i < 6; i++)
            *out[i] = (c->row[i] != VAR_NONE) ? _b->var(c->row[i]).as_f32() : k[i].f;
        return v;
    }

    Band Var::band() const {
        const Descriptor& d = meta();
        if (analog(d)) {
            const AnalogValues a = cal();
            return Band{ Scalar::F(a.min), Scalar::F(a.max), VAR_NONE, VAR_NONE };
        }
        const Band b = declared_band(d);
        if (!b.set()) return b;
        return Band{ _tunable(b.lo, b.lo_row), _tunable(b.hi, b.hi_row),
                     VAR_NONE, VAR_NONE };
    }

    bool Var::implausible(const Scalar& v) const {
        const Descriptor& d = meta();
        // Non-finite FIRST: a NaN is in no range, and calling that "out of band"
        // would send an operator to a limit that is fine.
        if (type_real(d.type)) {
            const float f = v.f;
            if (!(f == f) || f > 3.402823466e+38f || f < -3.402823466e+38f) return true;
        }
        const Band b = band();
        return b.set() && !in_range(d.type, v, b.lo, b.hi);
    }

    // ---- Reading --------------------------------------------------------------

    Value Var::wire() const {
        const Descriptor& d = meta();
        // No binding, no wire: a parameter or a computed row has none, and a
        // zero is what the one caller converts.
        if (d.kind != K_INPUT && d.kind != K_OUTPUT)
            return Value{ Scalar(), &d, d.type };
        const uint8_t off = _s().wire_off;
        // 1:1, so the wire IS the value - nothing is stored twice.
        if (off == WIRE_NONE) return Value{ value().v, &d, d.type };
        // An OUTPUT is converted on the way out and nothing ever publishes a
        // count inward for one: its wire value is what WOULD be sent.
        if (d.kind == K_OUTPUT)
            return Value{ from_f32(d.bind.wire, cal().to_raw(as_f32())),
                          &d, d.bind.wire };
        return Value{ _b->wire_at(off), &d, d.bind.wire };
    }

    char* Var::text(uint16_t* cap_out) const {
        if (cap_out != nullptr) *cap_out = 0;
        if (_b == nullptr) return nullptr;
        const uint16_t cap = text_slot(meta()).cap;
        const uint16_t off = _s().str_off;
        if (cap == 0 || off == STR_NO_SLOT) return nullptr;
        // Belt and braces: init() already bounded this, but this is the only
        // place a pool pointer is handed out.
        if ((uint32_t)off + cap > Config::STR_POOL_BYTES) return nullptr;
        if (cap_out != nullptr) *cap_out = cap;
        return _b->pool() + off;
    }

    // ---- Driver writes --------------------------------------------------------

    void Var::publish(Scalar v, uint64_t now_ms) const {
        Slot& s = _s();
        s.v        = v;
        s.stamp_ms = (uint32_t)now_ms;
        _touch();
        // Judged HERE, where the reading arrives: a fresh value can still be
        // nonsense, and a dead loop reading full scale is what no timeout
        // catches.
        set_quality(implausible(v) ? Q_NONE : Q_GOOD);
    }

    void Var::publish_wire(Scalar raw, uint64_t now_ms) const {
        const Descriptor& d = meta();
        if (!analog(d)) { publish(raw, now_ms); return; }   // 1:1: the count IS the value

        Slot& s = _s();
        if (s.wire_off != WIRE_NONE) _b->wire_at(s.wire_off) = raw;
        const AnalogValues a   = cal();
        const float        eng = a.to_eng(Vars::as_f32(d.bind.wire, raw));
        s.v        = from_f32(d.type, eng);
        s.stamp_ms = (uint32_t)now_ms;
        _touch();
        // A calibration whose two raw endpoints coincide has no slope, so
        // to_eng() returns eng_min for every count the ADC ever reads. That is a
        // plausible number - the bottom of the row's own band - so it would sit
        // there believed for ever. The channel is not reading; say so.
        set_quality((a.degenerate() || !a.plausible(eng)) ? Q_NONE : Q_GOOD);
    }

    // ---- Parameter edits ------------------------------------------------------

    SetResult Var::param_check(Scalar v) const {
        const Descriptor& d = meta();
        if (!writable(d)) return SET_INVALID;
        // A T_STR's live text is in the pool, so there is no typed form of it to
        // write; the text overload is the only way in.
        if (d.type == T_STR) return SET_INVALID;
        // Same rule the text path gets through parse_for(): a labelled enum's
        // codes ARE its labels, and this overload never goes near a parser.
        if (d.type == T_ENUM && d.labels != nullptr && d.labels(v.u) == nullptr)
            return SET_INVALID;
        const Band b = band();
        if (b.set() && !in_range(d.type, v, b.lo, b.hi)) return SET_INVALID;
        return SET_OK;
    }

    SetResult Var::param_check(const char* text) const {
        const Descriptor& d = meta();
        if (text == nullptr || !writable(d)) return SET_INVALID;
        if (d.type == T_STR) {
            uint16_t cap = 0;
            if (this->text(&cap) == nullptr) return SET_INVALID;
            return validate_str(text_slot(d).kind, text, cap) ? SET_OK : SET_INVALID;
        }
        Scalar parsed;
        const Band b = band();
        return parse_for(d, text, &b, &parsed);
    }

    SetResult Var::param_set(Scalar v) const {
        if (param_check(v) != SET_OK) return SET_INVALID;
        Slot& s = _s();
        // A write of the value already in force owes the store NOTHING. A Modbus
        // master re-asserting its setpoint block every poll and an ST program
        // restating its whole output every scan are both normal, and either
        // would be a flash erase per write without this.
        if (s.v.u == v.u) return SET_OK;
        s.v = v;
        _touch();
        // Live now, durable when somebody saves: the one rule that makes a
        // setting able to break its own console recoverable by rebooting.
        _set(S_PARAM_UNSAVED);
        return SET_OK;
    }

    SetResult Var::param_set(const char* text) const {
        const Descriptor& d = meta();
        if (text == nullptr || !writable(d)) return SET_INVALID;

        if (d.type == T_STR) {
            uint16_t cap = 0;
            char* dst = this->text(&cap);
            if (dst == nullptr) return SET_INVALID;
            if (!validate_str(text_slot(d).kind, text, cap)) return SET_INVALID;
            if (std::strcmp(dst, text) == 0) return SET_OK;
            copy_bounded(dst, cap, text);
            _touch();
            _set(S_PARAM_UNSAVED);
        } else {
            Scalar parsed;
            const Band b = band();
            if (parse_for(d, text, &b, &parsed) != SET_OK) return SET_INVALID;
            if (param_set(parsed) != SET_OK) return SET_INVALID;
        }
        // The TEXT form is the channel an operator or a remote master writes
        // through; a control program goes via the typed overload and would
        // otherwise stamp a watering date into the log every scan.
        if (_b->log() != nullptr)
            _b->log()->notice(Logs::USER, "set '%s'\nto %s", d.name, text);
        return SET_OK;
    }

    void Var::param_owe() const {
        // Only a write that CHANGED something is owed: param_set() leaves an
        // unchanged row unmarked, and re-owing it would be an erase for a value
        // the store already holds.
        if (!param_unsaved()) return;
        _clear(S_PARAM_UNSAVED);
        _set(S_PARAM_OWED);
    }

    // ---- Overrides ------------------------------------------------------------

    namespace {
        /** @brief Force @p h's slot, with no audit line and no companion: the
         *  half both the row and its quality share. */
        void force_slot(const Bank* b, uint16_t row, Scalar v) {
            Slot& s = b->slot(row);
            s.ovr   = v;
            s.flags = (uint8_t)(s.flags | S_OVR_ON | S_OVR_UNSAVED);
        }
    }  // namespace

    void Var::override_set(Scalar v) const {
        if (null()) return;
        // Before the write, so the line carries what the row read BEFORE the
        // force: an override that changes nothing still has to be accountable,
        // and the value it displaced is the half nothing else records.
        if (_b->log() != nullptr) {
            char was[48];
            _b->log()->notice(Logs::USER, "forced '%s'\nwas %s", name(),
                              var_format(*_b, *this, was, sizeof(was)));
        }
        force_slot(_b, _i, v);
        _touch_field();
        // A forced value is BELIEVED. Forcing a dead probe's reading so the
        // plant runs on it is what an override is for, and inheriting the doubt
        // underneath would make that impossible. Written into the COMPANION
        // because that is the one store - a rule applied at read time would be
        // an answer only C++ could see, ST reading the row itself.
        const uint16_t q = quality_row();
        if (q != VAR_NONE) force_slot(_b, q, Scalar::U((uint64_t)Q_GOOD));
    }

    SetResult Var::override_set(const char* text) const {
        const Descriptor& d = meta();
        // A T_STR's live text lives in the pool, not the slot, so there is
        // nothing for an override to shadow.
        if (text == nullptr || d.type == T_STR || null()) return SET_INVALID;
        Scalar parsed;
        // Band deliberately NOT applied: an override may push a reading past
        // what is plausible - that is often the whole point of forcing one.
        if (parse_for(d, text, /*band=*/nullptr, &parsed) != SET_OK) return SET_INVALID;
        override_set(parsed);
        return SET_OK;
    }

    void Var::override_clear() const {
        if (null() || !override_on()) return;
        if (_b->log() != nullptr) {
            char was[48];
            _b->log()->notice(Logs::USER, "released '%s'\nwas %s", name(),
                              var_format(*_b, *this, was, sizeof(was)));
        }
        _clear((uint8_t)(S_OVR_ON | S_OVR_UNSAVED));
        _touch_field();
        // ...and the belief that came with it, or a released reading would go on
        // being believed by whatever put it there. Not a second thing the
        // operator did: the companion goes with the row it speaks for.
        const uint16_t q = quality_row();
        if (q == VAR_NONE) return;
        Slot& s = _b->slot(q);
        s.flags = (uint8_t)(s.flags & ~(S_OVR_ON | S_OVR_UNSAVED));
    }

}}  // namespace Botanik::Vars

#endif // BOTANIK_FEATURE_VARS
