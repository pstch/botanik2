// REVIEW_35: TODO
#ifndef NETWORK_TELEMETRY_TYPES_H
#define NETWORK_TELEMETRY_TYPES_H

/**
 * @file telemetry_types.h
 * @brief The status-variable descriptor types (ScalarType / ScalarField /
 *        ScalarGroup) and their generic read/format, split out of telemetry.h so
 *        consumers can read the "status variable table" without pulling in the
 *        MqttSender / network stack (e.g. the always-on shell's `status`
 *        command). MqttSender publishes the same tables over MQTT.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "libbotanik/utils/text.h"   // format_float / format_int

namespace Botanik { namespace Network {

    /** @brief How to read + render a ScalarField's bytes (its width and sign;
     *  ENUM is a u8 rendered through a str() converter). */
    enum ScalarType {
        SCALAR_U8, SCALAR_U16, SCALAR_U32,   ///< unsigned integer
        SCALAR_I16, SCALAR_I32,              ///< signed integer
        SCALAR_ENUM                          ///< u8 -> string via ScalarField::str
    };

    /** @brief A scalar status value at a byte @p offset in a flat struct
     *  (typically the site PLC payload), rendered generically - no per-field
     *  formatter. Integer types are verbatim; a @p divisor > 1 makes it
     *  fixed-point (value / divisor with @p decimals places, e.g. 100 for
     *  centi-units); SCALAR_ENUM renders the raw byte through @p str. */
    struct ScalarField {
        const char* name;
        uint16_t    offset;            ///< byte offset into the registered base
        ScalarType  type;
        uint16_t    divisor;           ///< 0/1 = integer; 100 = centi; 1000 = milli
        uint8_t     decimals;          ///< fixed-point precision (divisor > 1)
        const char* (*str)(uint8_t);   ///< SCALAR_ENUM only (else null)
        /** Human-readable description (may be null). Part of the declarative
         *  status-variable spec, generated from the PLC project; last in the
         *  struct so existing positional initializers keep zero-initialising it.
         *  Not rendered yet (MONITOR / `status` show name+value). */
        const char* desc;
    };

    /** @brief A named group of ScalarFields under a shared sub-topic. A site
     *  declares one ScalarField[] per domain (dc_supply, ac_supply, ...) and
     *  lists them as a ScalarGroup[]. */
    struct ScalarGroup {
        const char*        topic;      ///< shared sub-topic (e.g. "ac_supply")
        const ScalarField* fields;
        size_t             count;
    };

    /** @brief Read @p f's value from @p base as a sign-extended 64-bit integer.
     *  memcpy keeps it alignment-safe on the real payload. */
    inline int64_t scalar_read(const ScalarField& f, const void* base) {
        const uint8_t* p = static_cast<const uint8_t*>(base) + f.offset;
        switch (f.type) {
            case SCALAR_U8:
            case SCALAR_ENUM: return *p;
            case SCALAR_U16:  { uint16_t v; memcpy(&v, p, 2); return v; }
            case SCALAR_U32:  { uint32_t v; memcpy(&v, p, 4); return v; }
            case SCALAR_I16:  { int16_t  v; memcpy(&v, p, 2); return v; }
            case SCALAR_I32:  { int32_t  v; memcpy(&v, p, 4); return v; }
        }
        return 0;
    }

    /** @brief Render @p f from @p base into @p buf; returns the text (an enum's
     *  static string when it maps, else @p buf). */
    inline const char* scalar_format(const ScalarField& f, const void* base,
                                     char* buf, size_t cap) {
        int64_t raw = scalar_read(f, base);
        if (f.type == SCALAR_ENUM && f.str != nullptr) {
            const char* s = f.str((uint8_t)raw);
            if (s != nullptr) return s;
        }
        if (f.divisor > 1) {
            Utils::format_float(buf, cap, (double)raw / (double)f.divisor, f.decimals);
        } else {
            Utils::format_int(buf, cap, (long long)raw);
        }
        return buf;
    }

}}  // namespace Botanik::Network

#endif // NETWORK_TELEMETRY_TYPES_H
