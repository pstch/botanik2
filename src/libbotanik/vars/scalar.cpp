/**
 * @file scalar.cpp
 * @brief Type-driven render, parse and validate for a Scalar.
 *
 * The parsing and IPv4 / hostname validation are carried over from the byte-image
 * parameter model (params.cpp) with their behaviour intact - the tests that
 * pinned them still apply. What changed is the addressing: these take a VarType
 * and a value, not a descriptor and a byte offset.
 */

#include "libbotanik/vars/scalar.h" // Scalar + the read/write/render/parse implemented here

#include <cerrno>                   // errno
#include <cstdlib>                  // strtoll / strtoull / strtod
#include <cstring>                  // strlen

#include "libbotanik/utils/text.h"  // Utils::format_float / format_int

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
            // Not -FLT_MAX: a float min/max is a display and clamping aid, and
            // the honest bound for "no declared minimum" is the whole range.
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
         *  Rejecting trailing junk is deliberate: "12abc" must be an error, not
         *  a quiet 12, so a broken table entry is obviously wrong rather than
         *  subtly plausible. */
        bool only_spaces_left(const char* end) {
            while (*end == ' ' || *end == '\t') end++;
            return *end == 0;
        }
    }  // namespace

    ParseResult parse_scalar(VarType t, const char* text, Scalar* out) {
        if (out == nullptr || text == nullptr || *text == 0) return PARSE_MALFORMED;
        if (t == T_STR) return PARSE_MALFORMED;   // not a scalar; see validate_str

        char* end = nullptr;
        // strto* SATURATE on overflow rather than failing, so a literal too big
        // for the type arrives as a perfectly plausible maximum. For the narrow
        // types in_range catches it; for the widest it would not, which is the
        // one place a malformed input could pass as a real setting.
        errno = 0;
        if (type_real(t)) {
            const double d = strtod(text, &end);
            if (end == text || !only_spaces_left(end)) return PARSE_MALFORMED;
            if (errno == ERANGE) return PARSE_RANGE;
            *out = Scalar::F((float)d);
            return PARSE_OK;
        }
        if (type_signed(t)) {
            const long long v = strtoll(text, &end, 10);
            if (end == text || !only_spaces_left(end)) return PARSE_MALFORMED;
            if (errno == ERANGE) return PARSE_RANGE;
            Scalar s = Scalar::I((int64_t)v);
            if (!in_range(t, s, type_min(t), type_max(t))) return PARSE_RANGE;
            *out = s;
            return PARSE_OK;
        }
        // Unsigned, bool and enum. strtoull wraps a negative rather than
        // failing, so reject the sign before it can become a huge positive.
        {
            const char* p = text;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '-') return PARSE_RANGE;
        }
        const unsigned long long v = strtoull(text, &end, 10);
        if (end == text || !only_spaces_left(end)) return PARSE_MALFORMED;
        if (errno == ERANGE) return PARSE_RANGE;
        Scalar s = Scalar::U((uint64_t)v);
        if (!in_range(t, s, type_min(t), type_max(t))) return PARSE_RANGE;
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
