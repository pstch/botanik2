/**
 * @file params.cpp
 * @brief Parameter persistence, editing and by-name reads.
 */

#include "libbotanik/vars/params.h" // Params / KvParamStore

#include <cstdio>                   // snprintf
#include <cstring>                  // memcpy / strlen

#include "libbotanik/hal/storage.h" // HAL::Storage::set/get/remove
#include "libbotanik/logs/logger.h" // Logs::Logger
#include "libbotanik/vars/access.h" // Vars::var_format / text_at

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
        bool prefixed_key(const Variable& d, const char* prefix,
                          char* out, size_t cap) {
            if (d.name == nullptr) return false;
            const int n = snprintf(out, cap, "%s%s", prefix, d.name);
            return n > 0 && (size_t)n < cap;
        }
    }  // namespace

    bool KvParamStore::load(const Variable& d, Value& v, char* text, uint16_t cap) {
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

    bool KvParamStore::save(const Variable& d, const Value& v,
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

    bool KvParamStore::load_override(const Variable& d, Value& v) {
        char key[HAL::Storage::KEY_MAX];
        if (!prefixed_key(d, "ovr.", key, sizeof(key))) return false;
        const uint8_t w = type_width(d.type);
        if (w == 0) return false;
        // Value bytes then the validity byte: an override records BOTH what the
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

    bool KvParamStore::save_override(const Variable& d, const Value& v) {
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

    bool KvParamStore::clear_override(const Variable& d) {
        char key[HAL::Storage::KEY_MAX];
        if (!prefixed_key(d, "ovr.", key, sizeof(key))) return false;
        return HAL::Storage::remove(key);
    }

    bool KvParamStore::load_retained(const Variable& d, Value& v) {
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

    bool KvParamStore::save_retained(const Variable& d, const Value& v) {
        char key[HAL::Storage::KEY_MAX];
        if (!prefixed_key(d, "ret.", key, sizeof(key))) return false;
        const uint8_t w = type_width(d.type);
        if (w == 0) return false;
        uint64_t raw = encode_scalar(d.type, v.v);
        return HAL::Storage::set(key, &raw, w);
    }

    // ---- Params --------------------------------------------------------------

    void Params::on_setup(uint64_t /*now_ms*/) {
        if (_io == nullptr) return;
        _io->load_params();
        // After the parameters, so a restored override shadows the setting
        // rather than being loaded over.
        const uint16_t n = _io->load_overrides();
        if (n != 0 && _logger != nullptr)
            _logger->critical(Logs::BOOT,
                              "%u override(s) restored from storage\n"
                              "the field is not being read or driven normally",
                              (unsigned)n);

        // Retained totals last: computed rows, untouched by either step above,
        // and a forced totalizer still reads as forced.
        const uint16_t r = _io->load_retained();
        if (r != 0 && _logger != nullptr)
            _logger->notice(Logs::BOOT, "%u retained value(s) restored",
                            (unsigned)r);
    }

    uint32_t Params::_retain_ms() const {
        // Read live rather than cached on a commit serial: it is one lookup on
        // the slow group, and a cadence has nothing to re-initialise when it
        // moves - the next due time is computed from it on the spot.
        const int32_t sec = get_int(_io, "sys_retain_sec",
                                    (int32_t)(Config::RETAIN_SAVE_MS / 1000u));
        return sec > 0 ? (uint32_t)sec * 1000u : 0u;
    }

    void Params::on_config(uint64_t now_ms) {
        if (_io == nullptr) return;
        const uint32_t every = _retain_ms();
        // 0 turns the cadence off: the way down and the `save` command are then
        // the whole mechanism, which is the right trade for a unit whose flash
        // is precious and whose stops are all deliberate.
        if (every == 0) return;
        // A cadence, not a write-through: a flash erase per beat would wear the
        // part out in days. The loss is bounded at one interval of counting on
        // an unexpected power cut; a deliberate stop loses nothing, the shutdown
        // path calling save_retained() itself.
        if (_last_retain_ms != 0 && (now_ms - _last_retain_ms) < every) return;
        _last_retain_ms = now_ms;
        (void)_io->save_retained();
    }

    VarRef Params::find(const char* name, uint16_t len) const {
        VarRef r = { VAR_NONE };
        if (_io == nullptr) return r;
        r = _io->find(name, len);
        if (r.index != VAR_NONE && _io->var(r).role() != ROLE_PARAM) r.index = VAR_NONE;
        return r;
    }

    const char* Params::format(uint16_t k, char* buf, size_t cap) const {
        return var_format(*_io, param(k), buf, cap);
    }

    const char* Params::format_scalar_of(const Variable& d, Scalar v,
                                         char* buf, size_t cap) const {
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

    float get_f32(const Bank* io, const char* name, float dflt) {
        Var h(nullptr, nullptr);
        if (!lookup(io, name, &h)) return dflt;
        return h.value();
    }

    const char* get_str(const Bank* io, const char* name) {
        Var h(nullptr, nullptr);
        if (!lookup(io, name, &h)) return "";
        if (h.meta()->type != T_STR || io->pool() == nullptr) return "";
        const char* t = io->text_at(h);
        return (t != nullptr) ? t : "";
    }

    float tunable(const Bank* io, const Tunable& t) {
        return (t.param == nullptr) ? t.k : get_f32(io, t.param, t.k);
    }

    AnalogValues analog_values(const Bank* io, const AnalogCal* c) {
        AnalogValues v = {};
        if (c == nullptr) return v;
        v.raw_min = tunable(io, c->raw_min);
        v.raw_max = tunable(io, c->raw_max);
        v.eng_min = tunable(io, c->eng_min);
        v.eng_max = tunable(io, c->eng_max);
        v.min     = tunable(io, c->min);
        v.max     = tunable(io, c->max);
        return v;
    }

    NumBand var_band(const Bank* io, const Variable& d) {
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
