/**
 * @file params.cpp
 * @brief Parameter persistence, editing and by-name reads.
 */

#include "libbotanik/tasks/tasks.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_VARS
#include "libbotanik/vars/params.h"
#include "libbotanik/vars/edit.h"

#include <cstdio>
#include <cstring>

#include "libbotanik/hal/storage.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"
#include "libbotanik/config/logging.h"
#include "libbotanik/vars/access.h"

namespace Botanik { namespace Vars {

    // ---- KvParamStore --------------------------------------------------------
    // The narrow value bytes under the parameter's own name. Narrow is required:
    // storing the 8-byte Scalar changes the encoding of every u8/u16/u32/f32
    // key, and HAL::Storage::get rejects an oversized value, so the whole
    // configuration would revert to factory on the next boot.

    namespace {
        /** @brief A stored word back into a Scalar of @p t's type. The one copy
         *  of the width-dependent sign extension: get it wrong and a stored
         *  negative total returns as a very large positive one. */
        Scalar decode_scalar(VarType t, uint64_t raw, uint8_t w) {
            if (type_real(t)) { float f; memcpy(&f, &raw, 4); return Scalar::F(f); }
            if (type_signed(t)) {
                const int64_t x = (int64_t)raw;
                const unsigned shift = (unsigned)(64 - 8 * w);
                return Scalar::I((int64_t)((x << shift) >> shift));
            }
            return Scalar::U(raw);
        }

        /** @brief A Scalar into the word that is stored. The inverse of
         *  decode_scalar(); a float goes out as its four bytes, everything else
         *  as the union's unsigned arm. */
        uint64_t encode_scalar(VarType t, const Scalar& s) {
            uint64_t raw = 0;
            if (type_real(t)) { const float f = s.f; memcpy(&raw, &f, 4); }
            else              { raw = s.u; }
            return raw;
        }

        /** @brief "<prefix><name>" - a storage key in its own namespace.
         *
         *  A row may carry a setting, an override and a retained total at once,
         *  so each lives behind a prefix no identifier can spell. */
        bool prefixed_key(const Descriptor& d, const char* prefix,
                          char* out, size_t cap) {
            if (d.name == nullptr) return false;
            const int n = snprintf(out, cap, "%s%s", prefix, d.name);
            return n > 0 && (size_t)n < cap;
        }
    }  // namespace

    bool KvParamStore::load(const Descriptor& d, Slot& v, char* text, uint16_t cap) {
        if (d.type == T_STR) {
            if (text == nullptr || cap == 0) return false;
            size_t n = 0;
            if (!HAL::Storage::get(d.name, text, cap, &n)) return false;
            // A stored value that filled the slot leaves no room for the NUL.
            text[n < cap ? n : (size_t)cap - 1] = 0;
            v.qual = Q_GOOD;
            return true;
        }
        const uint8_t w = type_width(d.type);
        if (w == 0) return false;
        uint64_t raw = 0;
        size_t n = 0;
        if (!HAL::Storage::get(d.name, &raw, w, &n) || n != w) return false;
        v.v = decode_scalar(d.type, raw, w);
        v.qual = Q_GOOD;
        return true;
    }

    bool KvParamStore::save(const Descriptor& d, const Slot& v,
                            const char* text, uint16_t cap) {
        if (d.type == T_STR) {
            if (text == nullptr || cap == 0) return false;
            return HAL::Storage::set(d.name, text, cap);
        }
        const uint8_t w = type_width(d.type);
        if (w == 0) return false;
        uint64_t raw = encode_scalar(d.type, v.v);
        return HAL::Storage::set(d.name, &raw, w);
    }

    bool KvParamStore::load_override(const Descriptor& d, Slot& v) {
        char key[HAL::Storage::KEY_MAX];
        if (!prefixed_key(d, "ovr.", key, sizeof(key))) return false;
        const uint8_t w = type_width(d.type);
        if (w == 0) return false;
        // Slot bytes then the validity byte: an override records BOTH what the
        // variable reads and whether it reads as good data.
        uint8_t buf[sizeof(uint64_t) + 1] = {};
        size_t n = 0;
        if (!HAL::Storage::get(key, buf, (size_t)w + 1, &n)) return false;
        if (n != (size_t)w + 1) return false;
        uint64_t raw = 0;
        memcpy(&raw, buf, w);
        v.ovr = decode_scalar(d.type, raw, w);
        v.ovr_valid = (buf[w] != 0);
        return true;
    }

    bool KvParamStore::save_override(const Descriptor& d, const Slot& v) {
        char key[HAL::Storage::KEY_MAX];
        if (!prefixed_key(d, "ovr.", key, sizeof(key))) return false;
        const uint8_t w = type_width(d.type);
        if (w == 0) return false;
        uint8_t buf[sizeof(uint64_t) + 1] = {};
        uint64_t raw = encode_scalar(d.type, v.ovr);
        memcpy(buf, &raw, w);
        buf[w] = v.ovr_valid ? 1 : 0;
        return HAL::Storage::set(key, buf, (size_t)w + 1);
    }

    bool KvParamStore::clear_override(const Descriptor& d) {
        char key[HAL::Storage::KEY_MAX];
        if (!prefixed_key(d, "ovr.", key, sizeof(key))) return false;
        return HAL::Storage::remove(key);
    }

    bool KvParamStore::clear(const Descriptor& d) {
        // A parameter's key is its bare name (see save()).
        if (d.name == nullptr) return false;
        return HAL::Storage::remove(d.name);
    }

    bool KvParamStore::load_retained(const Descriptor& d, Slot& v) {
        char key[HAL::Storage::KEY_MAX];
        if (!prefixed_key(d, "ret.", key, sizeof(key))) return false;
        const uint8_t w = type_width(d.type);
        if (w == 0) return false;
        uint64_t raw = 0;
        size_t n = 0;
        if (!HAL::Storage::get(key, &raw, w, &n)) return false;
        if (n != w) return false;
        v.v = decode_scalar(d.type, raw, w);
        return true;
    }

    bool KvParamStore::save_retained(const Descriptor& d, const Slot& v) {
        char key[HAL::Storage::KEY_MAX];
        if (!prefixed_key(d, "ret.", key, sizeof(key))) return false;
        const uint8_t w = type_width(d.type);
        if (w == 0) return false;
        uint64_t raw = encode_scalar(d.type, v.v);
        return HAL::Storage::set(key, &raw, w);
    }







    const char* format_declared(const Descriptor& d, Scalar v,
                                char* buf, size_t cap) {
        if (d.type == T_STR) return "";
        return format_scalar(d.type, v, d.digits, d.str, buf, cap);
    }

    // ---- By-name reads -------------------------------------------------------

    namespace {
        /** @brief Resolve @p name to a parameter handle, or the sink. */
        bool lookup(const Bank* io, const char* name, Var* out) {
            if (io == nullptr || name == nullptr) return false;
            const VarRef r = io->find(name);
            if (r.index == VAR_NONE) return false;
            *out = io->var(r);
            return true;
        }
    }  // namespace

    int32_t get_int(const Bank* io, const char* name, int32_t dflt) {
        Var h(nullptr, nullptr);
        if (!lookup(io, name, &h)) return dflt;
        return (int32_t)h.i();
    }


    const char* get_str(const Bank* io, const char* name) {
        Var h(nullptr, nullptr);
        if (!lookup(io, name, &h)) return "";
        if (h.meta()->type != T_STR || io->pool() == nullptr) return "";
        const char* t = io->text(h);
        return (t != nullptr) ? t : "";
    }

    /** @brief A float parameter by name. Only tunable() wants one: everything
     *  else reads a float through `var(name).as_f32()`. */
    static float get_f32(const Bank* io, const char* name, float dflt) {
        Var h(nullptr, nullptr);
        if (!lookup(io, name, &h)) return dflt;
        return h.as_f32();
    }

    float tunable(const Bank* io, const Tunable& t) {
        return (t.param == nullptr) ? t.k : get_f32(io, t.param, t.k);
    }

    AnalogValues analog_values(const Bank* io, const AnalogCal* c) {
        AnalogValues v = {};
        if (c == nullptr) return v;
        // Through the indices the bank resolved at freeze() where it has them:
        // by NAME this is six binary searches over the whole table, and it runs
        // on every analog read and write. The VALUES are still read live, so a
        // recalibration lands the moment the parameter commits.
        const VarRef* r = (io != nullptr) ? io->analog_refs(c) : nullptr;
        if (r == nullptr) {
            v.raw_min = tunable(io, c->raw_min);
            v.raw_max = tunable(io, c->raw_max);
            v.eng_min = tunable(io, c->eng_min);
            v.eng_max = tunable(io, c->eng_max);
            v.min     = tunable(io, c->min);
            v.max     = tunable(io, c->max);
            return v;
        }
        const Tunable* t[6] = { &c->raw_min, &c->raw_max, &c->eng_min,
                                &c->eng_max, &c->min,     &c->max };
        float* out[6] = { &v.raw_min, &v.raw_max, &v.eng_min,
                          &v.eng_max, &v.min,     &v.max };
        for (uint8_t k = 0; k < 6; k++)
            *out[k] = (r[k].index != VAR_NONE) ? io->var(r[k]).as_f32() : t[k]->k;
        return v;
    }

    NumBand var_band(const Bank* io, const Descriptor& d) {
        const AnalogCal* c = analog_cal(d);
        if (c == nullptr) return num_band(d);
        const AnalogValues v = analog_values(io, c);
        return NumBand{Scalar::F(v.min), Scalar::F(v.max)};
    }

    uint32_t group_serial(const Bank* io, const char* group) {
        // "<group>_serial": the suffix keeps the row inside its own group's name
        // prefix, so it sorts and lists with the group it belongs to.
        char key[HAL::Storage::KEY_MAX];
        if (group == nullptr) return 0;
        size_t n = strlen(group);
        if (n + 8 >= sizeof(key)) return 0;      // "_serial" + NUL
        // The suffix copy below is what terminates the key: it carries
        // "_serial" AND its NUL, which the checker cannot see from here.
        // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
        memcpy(key, group, n);
        memcpy(key + n, "_serial", 8);           // copies the NUL too
        return (uint32_t)get_int(io, key, 0);
    }

}}  // namespace Botanik::Vars

namespace Botanik { namespace Vars {



}}  // namespace Botanik::Vars

namespace Botanik { namespace Vars {



    /** @brief Copy @p src into @p dst, always NUL-terminating. */
    void copy_bounded(char* dst, uint16_t cap, const char* src) {
        if (dst == nullptr || cap == 0) return;
        if (src == nullptr) { dst[0] = 0; return; }
        uint16_t n = 0;
        while (src[n] != 0 && n + 1u < cap) { dst[n] = src[n]; n++; }
        dst[n] = 0;
    }

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

    uint16_t Bank::unsaved_count() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++)
            if ((_slots[i].vflags & VF_PARAM_UNSAVED) != 0) n++;
        return n;
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

}}  // namespace Botanik::Vars

#endif // BOTANIK_FEATURE_VARS
