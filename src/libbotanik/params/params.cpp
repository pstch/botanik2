/**
 * @file params.cpp
 * @brief The generic parameter read/render/validate/write helpers declared in
 *        params.h, plus ParamHandler's delegation to its ParamStore.
 */
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "libbotanik/params/params.h"
#include "libbotanik/params/store.h"     // ParamStore (ParamHandler delegates to it)
#include "libbotanik/utils/text.h"       // format_float / format_int

namespace Botanik { namespace Params {

    // The library ships no built-in parameter table: a site declares every
    // parameter over its own PLC input/output payload (see the SiteDescriptor).
    // These are the generic read / render / validate helpers the PARAMS page,
    // the `params` command and the stores use, all driven by a ScalarField.

    // Read / render

    const char* type_name(const Param& p) {
        if (p.field.type == SCALAR_U8 && p.meta.min == 0 && p.meta.max == 1)
            return "b";                                                    // boolean (0/1)
        switch (p.field.type) {
            case SCALAR_U8:     return "u8";
            case SCALAR_U16:    return "u16";
            case SCALAR_U32:    return "u32";
            case SCALAR_I16:    return "i16";
            case SCALAR_I32:    return "i32";
            case SCALAR_F32:    return "f32";
            case SCALAR_ENUM:   return "enum";
            case SCALAR_STRING: return "str";
        }
        return "?";
    }

    size_t param_width(const Param& p) {
        switch (p.field.type) {
            case SCALAR_U8: case SCALAR_ENUM: return 1;
            case SCALAR_U16: case SCALAR_I16: return 2;
            case SCALAR_U32: case SCALAR_I32: case SCALAR_F32: return 4;
            case SCALAR_STRING:               return p.meta.len;
        }
        return 0;
    }

    int32_t read_scalar(const Param& p, const void* base) {
        return (int32_t)scalar_read(p.field, base);   // sign-extended, truncated to 32
    }

    // Copy at most cap-1 bytes of s into buf, NUL-terminate, return length.
    static int copy_str(char* buf, size_t cap, const char* s) {
        if (cap == 0) return 0;
        size_t n = 0;
        while (s[n] != 0 && n < cap - 1) { buf[n] = s[n]; n++; }
        buf[n] = 0;
        return (int)n;
    }

    int format_value(const Param& p, const void* base, char* buf, size_t cap) {
        if (cap == 0) return 0;
        const ScalarField& f = p.field;
        if (f.type == SCALAR_STRING) return copy_str(buf, cap, scalar_read_string(f, base));
        if (f.type == SCALAR_F32) {
            Utils::format_float(buf, cap, (double)scalar_read_f32(f, base), f.digits);
            return (int)strlen(buf);
        }
        int32_t v = read_scalar(p, base);
        if (f.type == SCALAR_ENUM && f.str != nullptr) {
            const char* s = f.str((uint8_t)v);
            if (s != nullptr) return copy_str(buf, cap, s);
        }
        int n = snprintf(buf, cap, "%ld", (long)v);
        return (n < 0) ? 0 : (n < (int)cap ? n : (int)cap - 1);
    }

    // Validate / parse

    bool parse_scalar(const Param& p, const char* s, int32_t* out) {
        if (s == nullptr || *s == 0) return false;
        char* end = nullptr;
        long v = strtol(s, &end, 10);
        if (end == s) return false;
        while (*end == ' ') end++;
        if (*end != 0) return false;                 // trailing garbage
        if (v < INT32_MIN || v > INT32_MAX) return false;
        if (v < p.meta.min || v > p.meta.max) return false;
        *out = (int32_t)v;
        return true;
    }

    bool parse_f32(const Param& p, const char* s, float* out) {
        if (s == nullptr || *s == 0) return false;
        char* end = nullptr;
        double d = strtod(s, &end);
        if (end == s) return false;
        while (*end == ' ') end++;
        if (*end != 0) return false;                 // trailing garbage
        if (d < (double)p.meta.min || d > (double)p.meta.max) return false;
        *out = (float)d;
        return true;
    }

    bool validate_ipv4(const char* s, bool allow_empty) {
        if (s == nullptr || *s == 0) return allow_empty;
        int octets = 0;
        for (;;) {
            if (*s < '0' || *s > '9') return false;
            int val = 0, digits = 0;
            while (*s >= '0' && *s <= '9') {
                val = val * 10 + (*s - '0');
                if (val > 255) return false;
                digits++; s++;
            }
            if (digits == 0 || digits > 3) return false;
            octets++;
            if (*s == '.') { s++; if (octets == 4) return false; continue; }
            if (*s == 0) break;
            return false;
        }
        return octets == 4;
    }

    bool validate_hostname(const char* s, uint16_t cap, bool allow_empty) {
        if (s == nullptr || *s == 0) return allow_empty;
        uint16_t n = 0;
        for (const char* q = s; *q != 0; ++q) {
            char c = *q;
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '.';
            if (!ok) return false;
            if (++n >= cap) return false;   // must fit including the NUL
        }
        return true;
    }

    bool validate_string(const Param& p, const char* s) {
        switch (p.meta.kind) {
            case STR_IPV4:     return validate_ipv4(s, false);
            case STR_IPV4_OPT: return validate_ipv4(s, true);
            case STR_HOST:     return validate_hostname(s, p.meta.len, false);
            case STR_HOST_OPT: return validate_hostname(s, p.meta.len, true);
            default:           return false;
        }
    }

    // Shared write path (used by both stores): writes @p p's parsed value into
    // @p base at byte @p offset.
    // the local store passes the single-image out_offset. Writability is

    bool write_value(const Param& p, const char* text, void* base, int16_t offset) {
        uint8_t* dst = (uint8_t*)base + offset;
        if (is_string(p)) {
            if (!validate_string(p, text)) return false;
            size_t n = 0;
            while (text[n] != 0 && n + 1 < p.meta.len) { dst[n] = (uint8_t)text[n]; n++; }
            memset(dst + n, 0, p.meta.len - n);   // NUL-pad the fixed-size field
            return true;
        }
        if (p.field.type == SCALAR_F32) {
            float f = 0.0f;
            if (!parse_f32(p, text, &f)) return false;
            memcpy(dst, &f, 4);
            return true;
        }
        int32_t v = 0;
        if (!parse_scalar(p, text, &v)) return false;
        switch (p.field.type) {
            case SCALAR_U8: case SCALAR_ENUM: { uint8_t  b = (uint8_t)v;  memcpy(dst, &b, 1); break; }
            case SCALAR_U16:                  { uint16_t b = (uint16_t)v; memcpy(dst, &b, 2); break; }
            case SCALAR_U32:                  { uint32_t b = (uint32_t)v; memcpy(dst, &b, 4); break; }
            case SCALAR_I16:                  { int16_t  b = (int16_t)v;  memcpy(dst, &b, 2); break; }
            case SCALAR_I32:                  { int32_t  b = v;           memcpy(dst, &b, 4); break; }
            default:                          return false;
        }
        return true;
    }

    // ParamHandler: the model delegates the value surface to its store.

    const void* ParamHandler::image() const { return _store->image(); }

    bool ParamHandler::set(const Param& p, const char* text) { return _store->set(p, text); }

    void ParamHandler::setup(uint64_t /*now_ms*/) {
        for (uint8_t i = 0; i < count(); i++) _store->load(param(i));
    }

}}
