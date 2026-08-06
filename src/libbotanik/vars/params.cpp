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
    // The narrow value bytes under the parameter's own name. Narrow on purpose:
    // serialising the 8-byte Scalar would change the encoding of every u8/u16/
    // u32/f32 key, and HAL::Storage::get rejects an oversized value, so the
    // whole configuration would quietly revert to factory on the next boot.

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
        Scalar s;
        if (type_real(d.type))        { float f; memcpy(&f, &raw, 4); s = Scalar::F(f); }
        else if (type_signed(d.type)) {
            // Sign-extend from the stored width.
            int64_t x = (int64_t)raw;
            const unsigned shift = (unsigned)(64 - 8 * w);
            s = Scalar::I((int64_t)((x << shift) >> shift));
        }
        else                          { s = Scalar::U(raw); }
        v.v = s;
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
        uint64_t raw = 0;
        if (type_real(d.type)) { const float f = v.v.f; memcpy(&raw, &f, 4); }
        else                   { raw = v.v.u; }
        return HAL::Storage::set(d.name, &raw, w);
    }

    namespace {
        /** @brief An override's storage key: the variable's name behind a prefix
         *  no identifier can carry, so it can never collide with the parameter
         *  key of the same row - the setting and the override are two facts
         *  about one variable and must not overwrite each other. */
        bool override_key(const Variable& d, char* out, size_t cap) {
            if (d.name == nullptr) return false;
            const int n = snprintf(out, cap, "ovr.%s", d.name);
            return n > 0 && (size_t)n < cap;
        }
    }  // namespace

    bool KvParamStore::load_override(const Variable& d, Value& v) {
        char key[HAL::Storage::KEY_MAX];
        if (!override_key(d, key, sizeof(key))) return false;
        const uint8_t w = type_width(d.type);
        if (w == 0) return false;
        // Value bytes then the validity byte: an override records BOTH what the
        // variable reads and whether it reads as good data, because "force this
        // to 20 C" and "make this read as a dead sensor" are different jobs.
        uint8_t buf[sizeof(uint64_t) + 1] = {};
        size_t n = 0;
        if (!HAL::Storage::get(key, buf, (size_t)w + 1, &n)) return false;
        if (n != (size_t)w + 1) return false;
        uint64_t raw = 0;
        memcpy(&raw, buf, w);
        if (type_real(d.type))        { float f; memcpy(&f, &raw, 4); v.ovr = Scalar::F(f); }
        else if (type_signed(d.type)) {
            const int64_t x = (int64_t)raw;
            const unsigned shift = (unsigned)(64 - 8 * w);
            v.ovr = Scalar::I((int64_t)((x << shift) >> shift));
        }
        else                          { v.ovr = Scalar::U(raw); }
        v.ovr_valid = (buf[w] != 0);
        return true;
    }

    bool KvParamStore::save_override(const Variable& d, const Value& v) {
        char key[HAL::Storage::KEY_MAX];
        if (!override_key(d, key, sizeof(key))) return false;
        const uint8_t w = type_width(d.type);
        if (w == 0) return false;
        uint8_t buf[sizeof(uint64_t) + 1] = {};
        uint64_t raw = 0;
        if (type_real(d.type)) { const float f = v.ovr.f; memcpy(&raw, &f, 4); }
        else                   { raw = v.ovr.u; }
        memcpy(buf, &raw, w);
        buf[w] = v.ovr_valid ? 1 : 0;
        return HAL::Storage::set(key, buf, (size_t)w + 1);
    }

    bool KvParamStore::clear_override(const Variable& d) {
        char key[HAL::Storage::KEY_MAX];
        if (!override_key(d, key, sizeof(key))) return false;
        return HAL::Storage::remove(key);
    }

    // ---- Params --------------------------------------------------------------

    void Params::on_setup(uint64_t /*now_ms*/) {
        if (_io == nullptr) return;
        _io->load_params();
        // After the parameters, so a restored override shadows the setting it is
        // meant to shadow rather than being loaded over.
        const uint16_t n = _io->load_overrides();
        if (n != 0 && _logger != nullptr)
            _logger->critical(Logs::BOOT,
                              "%u override(s) restored from storage - the field is "
                              "not being read or driven normally", (unsigned)n);
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
        // "<group>_serial". The suffix matters: without it the serial was named
        // bare after its group, so it was the one parameter whose name carried no
        // group prefix - it sorted apart from the group it belongs to and read
        // like a stray in any listing.
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
