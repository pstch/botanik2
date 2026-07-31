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
        if (_io == nullptr || _store == nullptr) return;
        for (uint16_t k = 0; k < count(); k++) {
            const uint16_t i = _io->tables().param_first + k;
            const Variable& d = _io->tables().var[i];
            // A missing key is normal: the parameter keeps the declared default
            // that init() already seeded.
            (void)_store->load(d, _io->tables().val[i], _io->pool());
        }
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

    SetResult Params::set(Var p, const char* text) {
        const Variable* d = p.meta();
        if (_io == nullptr || d->role != ROLE_PARAM) return SET_INVALID;
        if ((d->flags & F_RDONLY) != 0) return SET_INVALID;

        const uint16_t i = _io->find(d->name).index;
        if (i == VAR_NONE) return SET_INVALID;
        Value& slot = _io->tables().val[i];

        if (d->type == T_STR) {
            if (_io->pool() == nullptr || d->str_cap == 0) return SET_INVALID;
            if (!validate_str(d->kind, text, d->str_cap)) return SET_INVALID;
            char* dst = _io->pool() + d->str_off;
            uint16_t n = 0;
            while (text[n] != 0 && n + 1u < d->str_cap) { dst[n] = text[n]; n++; }
            dst[n] = 0;
            slot.valid = true;
        } else {
            Scalar parsed;
            if (parse_scalar(d->type, text, &parsed) != PARSE_OK) return SET_INVALID;
            if ((d->flags & F_BOUNDED) != 0 &&
                !in_range(d->type, parsed, d->min, d->max)) return SET_INVALID;
            slot.v = parsed;
            slot.valid = true;
        }

        if (_store == nullptr) return SET_NOT_PERSISTED;
        if (!_store->save(*d, slot, _io->pool())) {
            if (_logger)
                _logger->critical(Logs::STORAGE, "param '%s' not persisted", d->name);
            return SET_NOT_PERSISTED;
        }
        return SET_OK;
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
        return (uint32_t)get_int(io, group, 0);
    }

}}  // namespace Botanik::Vars
