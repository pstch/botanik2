/**
 * @file params.cpp
 * @brief Parameter persistence, editing and by-name reads.
 */

#include "params.h"

#include <cstring>

#include "../hal/storage.h"
#include "../logs/logger.h"
#include "access.h"

namespace Botanik { namespace Vars {

    // ---- KvParamStore --------------------------------------------------------
    // The narrow value bytes under the parameter's own name. Narrow on purpose:
    // serialising the 8-byte Scalar would change the encoding of every u8/u16/
    // u32/f32 key, and HAL::Storage::get rejects an oversized value, so the
    // whole configuration would quietly revert to factory on the next boot.

    bool KvParamStore::load(const Variable& d, Value& v, char* pool) {
        if (d.type == T_STR) {
            if (pool == nullptr || d.str_cap == 0) return false;
            size_t n = 0;
            if (!HAL::Storage::get(d.name, pool + d.str_off, d.str_cap, &n)) return false;
            // A stored value that filled the slot leaves no room for the NUL.
            pool[d.str_off + (n < d.str_cap ? n : (size_t)d.str_cap - 1)] = 0;
            v.valid = true;
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
        v.valid = true;
        return true;
    }

    bool KvParamStore::save(const Variable& d, const Value& v, const char* pool) {
        if (d.type == T_STR) {
            if (pool == nullptr || d.str_cap == 0) return false;
            return HAL::Storage::set(d.name, pool + d.str_off, d.str_cap);
        }
        const uint8_t w = type_width(d.type);
        if (w == 0) return false;
        uint64_t raw = 0;
        if (type_real(d.type)) { const float f = v.v.f; memcpy(&raw, &f, 4); }
        else                   { raw = v.v.u; }
        return HAL::Storage::set(d.name, &raw, w);
    }

    // ---- Params --------------------------------------------------------------

    void Params::setup(uint64_t /*now_ms*/) {
        if (_io != nullptr) _io->load_params();
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
        return io->pool() + h.meta()->str_off;
    }

    uint32_t group_serial(const Bank* io, const char* group) {
        // "<group>_serial". The suffix matters: without it the serial was named
        // bare after its group, so it was the one parameter whose name carried no
        // group prefix - it sorted apart from the group it belongs to and read
        // like a stray in any listing.
        char key[32];
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
