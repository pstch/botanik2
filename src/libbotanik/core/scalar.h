#ifndef CORE_SCALAR_H
#define CORE_SCALAR_H

/**
 * @file scalar.h
 * @brief The scalar descriptor (ScalarType / ScalarField) and its generic
 *        read/format, addressed by byte offset into a value image.
 *
 * Status variables have moved to the variable model (vars/), which addresses by
 * table and index rather than by offset. What is left here backs the CONFIG
 * parameters, whose store is still a byte image - folding those in too waits on
 * an index-addressed parameter store.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../utils/text.h"   // format_float / format_int
#include "../../libbotanik_config.h"

namespace Botanik {

    /** @brief How to read + render a ScalarField's bytes (its width and sign).
     *  SCALAR_F32 is a 32-bit float; SCALAR_ENUM a u8 through a str() converter;
     *  SCALAR_STRING a NUL-terminated char buffer (config-only, e.g. an IP or
     *  host). The one enum for both status variables and config parameters. */
    enum ScalarType {
        SCALAR_U8, SCALAR_U16, SCALAR_U32,   ///< unsigned integer
        SCALAR_I16, SCALAR_I32,              ///< signed integer
        SCALAR_F32,                          ///< 32-bit float, shown to `digits` decimals
        SCALAR_ENUM,                         ///< u8 -> string via ScalarField::str
        SCALAR_STRING                        ///< NUL-terminated text (config only)
    };

    /**
     * @brief One config parameter's read/display descriptor: addressed by byte
     * offset into the parameter image, rendered generically - no per-field
     * code. A Params::Param pairs it with the edit metadata (default / bounds /
     * group).
     *
     * @p out_offset is where the current value is read. Real-valued quantities
     * are SCALAR_F32 in native units (no scaled-integer / divisor convention),
     * displayed to @c digits decimals.
     */
    struct ScalarField {
        const char* name;
        int16_t     out_offset;        ///< read offset in the PLC output image (>= 0)
        ScalarType  type;
        uint8_t     digits;            ///< SCALAR_F32 display decimals (0 for ints/enum/string)
        const char* (*str)(uint8_t);   ///< SCALAR_ENUM only (else null)
        const char* unit;              ///< physical unit, e.g. "C"/"V"/"%" ("" = none)
        const char* desc;              ///< one-line description
        const char* notes;             ///< extra notes / caveats
    };

    // Published as Config::PAYLOAD_OFFSET_MAX for generated payloads to
    // static_assert their size against: an offset past this truncates when
    // stored, silently addressing the wrong field.
    static_assert(Config::PAYLOAD_OFFSET_MAX == (uint16_t)INT16_MAX,
                  "Config::PAYLOAD_OFFSET_MAX must track the offset field width");

    /** @brief Read @p f's value from @p base (at @p f.out_offset) as a
     *  sign-extended 64-bit integer. memcpy keeps it alignment-safe on the real
     *  payload. Undefined for SCALAR_STRING (0). */
    inline int64_t scalar_read(const ScalarField& f, const void* base) {
        const uint8_t* p = static_cast<const uint8_t*>(base) + f.out_offset;
        switch (f.type) {
            case SCALAR_U8:
            case SCALAR_ENUM: return *p;
            case SCALAR_U16:  { uint16_t v; memcpy(&v, p, 2); return v; }
            case SCALAR_U32:  { uint32_t v; memcpy(&v, p, 4); return v; }
            case SCALAR_I16:  { int16_t  v; memcpy(&v, p, 2); return v; }
            case SCALAR_I32:  { int32_t  v; memcpy(&v, p, 4); return v; }
            case SCALAR_F32:  { float    v; memcpy(&v, p, 4); return (int64_t)v; }
            case SCALAR_STRING: return 0;
        }
        return 0;
    }

    /** @brief Read @p f's value as a 32-bit float (SCALAR_F32 only; 0 otherwise).
     *  memcpy keeps it alignment-safe on the real payload. */
    inline float scalar_read_f32(const ScalarField& f, const void* base) {
        if (f.type != SCALAR_F32) return 0.0f;
        float v; memcpy(&v, static_cast<const uint8_t*>(base) + f.out_offset, 4);
        return v;
    }

    /** @brief Pointer to a SCALAR_STRING value's text within @p base. */
    inline const char* scalar_read_string(const ScalarField& f, const void* base) {
        return static_cast<const char*>(base) + f.out_offset;
    }

    /** @brief Render @p f from @p base into @p buf; returns the text (an enum's
     *  static string when it maps, a string field verbatim, else @p buf). */
    inline const char* scalar_format(const ScalarField& f, const void* base,
                                     char* buf, size_t cap) {
        if (f.type == SCALAR_F32)
            return Utils::format_float(buf, cap, (double)scalar_read_f32(f, base), f.digits);
        if (f.type == SCALAR_STRING)
            return scalar_read_string(f, base);
        int64_t raw = scalar_read(f, base);
        if (f.type == SCALAR_ENUM && f.str != nullptr) {
            const char* s = f.str((uint8_t)raw);
            if (s != nullptr) return s;
        }
        Utils::format_int(buf, cap, (long long)raw);
        return buf;
    }

}  // namespace Botanik

#endif // CORE_SCALAR_H
