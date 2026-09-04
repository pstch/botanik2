/**
 * @file scalar.cpp
 * @brief Type-driven render, parse and validate for a Scalar.
 */

#include "libbotanik/vars/scalar.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_VARS

#include <cstring>

#include "libbotanik/utils/text.h"

namespace Botanik { namespace Vars {

    const char* type_name(VarType t) {
        switch (t) {
            case T_BOOL: return "bool";
            case T_U8:   return "u8";
            case T_U16:  return "u16";
            case T_U32:  return "u32";
            case T_U64:  return "u64";
            case T_I8:   return "i8";
            case T_I16:  return "i16";
            case T_I32:  return "i32";
            case T_I64:  return "i64";
            case T_F32:  return "f32";
            case T_ENUM: return "enum";
            case T_STR:  return "str";
        }
        return "?";
    }

    Scalar type_min(VarType t) {
        switch (t) {
            case T_I8:   return Scalar::I(INT8_MIN);
            case T_I16:  return Scalar::I(INT16_MIN);
            case T_I32:  return Scalar::I(INT32_MIN);
            case T_I64:  return Scalar::I(INT64_MIN);
            // The whole range: the honest bound for "no declared minimum".
            case T_F32:  return Scalar::F(-3.402823466e+38f);
            default:     return Scalar::U(0);
        }
    }

    Scalar type_max(VarType t) {
        switch (t) {
            case T_BOOL: return Scalar::U(1);
            case T_U8:
            case T_ENUM: return Scalar::U(UINT8_MAX);
            case T_U16:  return Scalar::U(UINT16_MAX);
            case T_U32:  return Scalar::U(UINT32_MAX);
            case T_U64:  return Scalar::U(UINT64_MAX);
            case T_I8:   return Scalar::I(INT8_MAX);
            case T_I16:  return Scalar::I(INT16_MAX);
            case T_I32:  return Scalar::I(INT32_MAX);
            case T_I64:  return Scalar::I(INT64_MAX);
            case T_F32:  return Scalar::F(3.402823466e+38f);
            case T_STR:  return Scalar::U(0);
        }
        return Scalar::U(0);
    }

    bool in_range(VarType t, Scalar v, Scalar lo, Scalar hi) {
        if (type_real(t))   return v.f >= lo.f && v.f <= hi.f;
        if (type_signed(t)) return v.i >= lo.i && v.i <= hi.i;
        return v.u >= lo.u && v.u <= hi.u;
    }

    const char* format_scalar(VarType t, Scalar v, uint8_t digits,
                              const char* (*str)(uint64_t), char* buf, size_t cap) {
        if (cap == 0) return "";
        switch (t) {
            case T_STR:
                // The live text is in the site's pool, not in this slot.
                buf[0] = 0;
                return buf;
            case T_ENUM:
                if (str != nullptr) {
                    const char* s = str(v.u);
                    if (s != nullptr) return s;
                }
                Utils::format_int(buf, cap, (long long)v.u);
                return buf;
            case T_F32:
                return Utils::format_float(buf, cap, (double)v.f, digits);
            case T_BOOL:
                Utils::format_int(buf, cap, v.u != 0 ? 1 : 0);
                return buf;
            default:
                Utils::format_int(buf, cap,
                    type_signed(t) ? (long long)v.i : (long long)v.u);
                return buf;
        }
    }

    namespace {
        /** @brief Step past trailing spaces; true when nothing else follows.
         *  Trailing junk is rejected: "12abc" is an error, never a quiet 12. */
        bool only_spaces_left(const char* end) {
            while (*end == ' ' || *end == '\t') end++;
            return *end == 0;
        }

        /** @brief @p text as one of @p str's labels, or PARSE_MALFORMED.
         *
         *  Searched rather than inverted: the converter is a function, so the
         *  only way back from a label to its code is to ask it for each. The
         *  space is bounded and small - T_ENUM's type_max is UINT8_MAX - and
         *  this runs on a `set`, which is a human typing, never on a beat. */
        ParseResult parse_enum_label(const char* (*str)(uint64_t),
                                     const char* text, Scalar* out) {
            while (*text == ' ' || *text == '\t') text++;
            for (uint32_t code = 0; code <= 255u; code++) {
                const char* label = str(code);
                if (label == nullptr) continue;
                const char* a = label;
                const char* b = text;
                while (*a != 0 && *a == *b) { a++; b++; }
                if (*a == 0 && only_spaces_left(b)) {
                    *out = Scalar::U(code);
                    return PARSE_OK;
                }
            }
            return PARSE_MALFORMED;
        }
    }  // namespace

    ParseResult parse_scalar(VarType t, const char* text, Scalar* out,
                             const char* (*str)(uint64_t)) {
        if (out == nullptr || text == nullptr || *text == 0) return PARSE_MALFORMED;
        if (t == T_STR) return PARSE_MALFORMED;   // not a scalar; see validate_str

        // A labelled enum answers to its words first, so `set <row> warning`
        // works and is the spelling worth teaching. A number still parses below,
        // which is what a Modbus master and the stored form both write.
        if (t == T_ENUM && str != nullptr) {
            if (parse_enum_label(str, text, out) == PARSE_OK) return PARSE_OK;
        }

        if (type_real(t)) {
            double d = 0.0;
            const Utils::NumParse r = Utils::parse_f64(text, &d);
            if (r != Utils::NUM_OK) {
                return r == Utils::NUM_RANGE ? PARSE_RANGE : PARSE_MALFORMED;
            }
            // A good DOUBLE is not yet a good float: 1e300 becomes +inf on the
            // narrowing cast, and an overridden infinity reads false against
            // every downstream compare, exactly as a NaN does.
            if (d > 3.4028234663852886e+38 ||
                d < -3.4028234663852886e+38) return PARSE_RANGE;
            *out = Scalar::F((float)d);
            return PARSE_OK;
        }
        if (type_signed(t)) {
            int64_t v = 0;
            const Utils::NumParse r = Utils::parse_i64(text, &v);
            if (r != Utils::NUM_OK) {
                return r == Utils::NUM_RANGE ? PARSE_RANGE : PARSE_MALFORMED;
            }
            Scalar s = Scalar::I(v);
            if (!in_range(t, s, type_min(t), type_max(t))) return PARSE_RANGE;
            *out = s;
            return PARSE_OK;
        }
        // Unsigned, bool and enum.
        uint64_t v = 0;
        const Utils::NumParse r = Utils::parse_u64(text, &v);
        if (r != Utils::NUM_OK) {
            return r == Utils::NUM_RANGE ? PARSE_RANGE : PARSE_MALFORMED;
        }
        Scalar s = Scalar::U(v);
        if (!in_range(t, s, type_min(t), type_max(t))) return PARSE_RANGE;
        // A labelled enum's codes are not an interval, so the type range above
        // admits every gap between them. Membership is the real bound: a code
        // the converter cannot name is one this row has no meaning for.
        if (t == T_ENUM && str != nullptr && str(s.u) == nullptr) return PARSE_RANGE;
        *out = s;
        return PARSE_OK;
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
            const char c = *q;
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '.';
            if (!ok) return false;
            if (++n >= cap) return false;   // must fit including the NUL
        }
        return true;
    }

    bool validate_str(StrKind kind, const char* text, uint16_t cap) {
        switch (kind) {
            case STR_IPV4:     return validate_ipv4(text, false);
            case STR_IPV4_OPT: return validate_ipv4(text, true);
            case STR_HOST:     return validate_hostname(text, cap, false);
            case STR_HOST_OPT: return validate_hostname(text, cap, true);
            case STR_NONE:
                // Anything that fits, empty included.
                return text != nullptr && strlen(text) + 1 <= (size_t)cap;
        }
        return false;
    }

}}  // namespace Botanik::Vars

#endif // BOTANIK_FEATURE_VARS
