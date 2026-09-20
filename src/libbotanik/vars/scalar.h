#ifndef VARS_SCALAR_H
#define VARS_SCALAR_H

/**
 * @file scalar.h
 * @brief One value slot for every type a variable can hold, and the type-driven
 *        read / write / render / parse that go with it.
 * @note The rationale behind this file is @ref scalar_one_tag, @ref scalar_flash, @ref scalar_width.
 */

#include <cstddef>
#include <cstdint>

namespace Botanik { namespace Vars {

    /** @name Types
     *  @{ */

    /** @brief What a variable's value IS. Distinct from how it arrives on the
     *  wire, which is the binding's business. */
    enum VarType : uint8_t {
        T_BOOL,
        T_U8, T_U16, T_U32, T_U64,
        T_I8, T_I16, T_I32, T_I64,
        T_F32,
        T_ENUM,   ///< a small unsigned code, rendered through a str() converter
        T_STR     ///< text; the live value lives in the site's char pool
    };

    /** @brief How to validate a T_STR's edit text. */
    enum StrKind : uint8_t {
        STR_NONE,       ///< any text that fits
        STR_IPV4,       ///< dotted quad, required
        STR_IPV4_OPT,   ///< dotted quad, empty allowed
        STR_HOST,       ///< hostname, required
        STR_HOST_OPT    ///< hostname, empty allowed
    };

    /**
     * @brief One value slot: 8 bytes, whatever the type.
     *
     * @c s holds a T_STR's DEFAULT text (a flash pointer). A T_STR's LIVE text
     * is not here - it lives in the site's char pool, because a slot cannot hold
     * a hostname.
     */
    union Scalar {
        uint64_t    u;
        int64_t     i;
        float       f;
        const char* s;

        constexpr Scalar()              : u(0) {}
        constexpr Scalar(uint64_t v)    : u(v) {}
        constexpr Scalar(int64_t v)     : i(v) {}
        constexpr Scalar(float v)       : f(v) {}
        constexpr Scalar(const char* v) : s(v) {}
        /** Deleted so a stray `Scalar(0)` names itself in the error instead of
         *  listing four equally good candidates. */
        Scalar(int) = delete;

        static constexpr Scalar U(uint64_t v)    { return Scalar(v); }
        static constexpr Scalar I(int64_t v)     { return Scalar(v); }
        static constexpr Scalar F(float v)       { return Scalar(v); }
        static constexpr Scalar S(const char* v) { return Scalar(v); }
        static constexpr Scalar B(bool v)        { return Scalar((uint64_t)(v ? 1u : 0u)); }
    };
    static_assert(sizeof(Scalar)  == 8, "Scalar must stay 8 bytes");
    static_assert(alignof(Scalar) == 8, "Scalar must stay 8-aligned");

    // Flash placement rests on these staying usable in a constant expression:
    // the moment one factory stops being constexpr, a site's table acquires a
    // runtime initialiser and moves to RAM.
    static_assert(Scalar::U(64).u  == 64, "Scalar::U must be constexpr");
    static_assert(Scalar::I(-5).i  == -5, "Scalar::I must be constexpr");
    static_assert(Scalar::B(true).u == 1, "Scalar::B must be constexpr");

    /** @} */

    /** @name Type queries
     *  Each body is a single return: a C++11 constexpr function may be nothing
     *  else.
     *  @{ */

    /** @brief Bytes a value occupies when PERSISTED (0 for T_STR, whose width is
     *  its declared capacity). Deliberately narrow, not sizeof(Scalar). */
    constexpr uint8_t type_width(VarType t) {
        return (t == T_BOOL || t == T_U8 || t == T_I8 || t == T_ENUM) ? 1 :
               (t == T_U16  || t == T_I16)                            ? 2 :
               (t == T_U32  || t == T_I32 || t == T_F32)              ? 4 :
               (t == T_U64  || t == T_I64)                            ? 8 : 0;
    }
    constexpr bool type_signed(VarType t) {
        return t == T_I8 || t == T_I16 || t == T_I32 || t == T_I64;
    }
    constexpr bool type_real(VarType t) { return t == T_F32; }

    // Same claim as the Scalar asserts above: a site's table selects on the type
    // in a constant expression, so this must stay usable in one.
    static_assert(type_width(T_U16)  == 2, "type_width must be constexpr");
    static_assert(type_width(T_BOOL) == 1, "narrow widths are the persisted ones");
    static_assert(type_width(T_STR)  == 0, "T_STR's width is its declared capacity");

    /** @brief Short display name ("bool", "u16", "f32", "enum", "str"). */
    const char* type_name(VarType t);

    /** @brief The type's own representable range, as a Scalar of that type.
     *  Used to reject a value that parses but does not fit. */
    Scalar type_min(VarType t);
    Scalar type_max(VarType t);

    /** @} */

    /** @name Reads - the slot, as whatever the caller wants
     *  @{ */

    /** @brief As a float. Digitals read 0/1, so a consumer that only wants to
     *  display or publish never branches on the type. */
    inline float as_f32(VarType t, Scalar v) {
        return type_real(t) ? v.f : (type_signed(t) ? (float)v.i : (float)v.u);
    }
    /** @brief Whether the slot holds a non-zero. Compared in the arm's own
     *  type: casting a float to an integer first is undefined for a NaN or an
     *  out-of-range magnitude, and nothing here needs the integer.
     *  @note The SATURATING reads a display or a transport wants are
     *  Value::as_i64() and Value::as_u64(); there is deliberately no free form
     *  of those, because one that did not clamp would be the same name with
     *  quieter semantics. */
    inline bool as_bool(VarType t, Scalar v) {
        return type_real(t) ? (v.f != 0.0f) : (type_signed(t) ? (v.i != 0) : (v.u != 0u));
    }

    /** @} */

    /** @name Writes - normalised INTO the slot the type names
     *  The one place a write is coerced, so a later read cannot reinterpret the
     *  wrong union member.
     *  @{ */

    inline Scalar from_f32(VarType t, float x) {
        // roundf, not (int)(x + 0.5f): add-then-truncate rounds the wrong way
        // for values just under .5, where the addition itself carries. The
        // builtin, not <cmath>: the header would cost 3.8k preprocessed lines
        // in each of the 29 TUs this one reaches.
        return type_real(t)   ? Scalar::F(x)
             : type_signed(t) ? Scalar::I((int64_t)__builtin_roundf(x))
                              : Scalar::U(x < 0.0f ? 0u : (uint64_t)__builtin_roundf(x));
    }
    inline Scalar from_i64(VarType t, int64_t x) {
        return type_real(t)   ? Scalar::F((float)x)
             : type_signed(t) ? Scalar::I(x)
                              : Scalar::U(x < 0 ? 0u : (uint64_t)x);
    }
    inline Scalar from_bool(VarType t, bool b) {
        return type_real(t) ? Scalar::F(b ? 1.0f : 0.0f) : Scalar::U(b ? 1u : 0u);
    }

    /** @} */

    /** @name Compare, render, parse
     *  @{ */

    /** @brief Is @p v within [@p lo, @p hi], compared as @p t? */
    bool in_range(VarType t, Scalar v, Scalar lo, Scalar hi);

    /**
     * @brief Render @p v into @p buf.
     *
     * @param digits decimals for T_F32 (ignored otherwise)
     * @param str    T_ENUM converter; when it maps, its static string is
     *               returned directly and @p buf is untouched
     * @return the text - @p buf, or a static string
     *
     * T_STR is not renderable from a Scalar (its live text is in the pool), and
     * returns "".
     */
    const char* format_scalar(VarType t, Scalar v, uint8_t digits,
                              const char* (*str)(uint64_t), char* buf, size_t cap);

    /** @brief Why a parse failed. */
    enum ParseResult : uint8_t {
        PARSE_OK,
        PARSE_MALFORMED,   ///< empty, unparseable, or trailing junk
        PARSE_RANGE        ///< parsed, but outside the type's own range
    };

    /** @brief Parse @p text as @p t into @p out. Rejects trailing junk, so
     *  "12abc" is malformed rather than quietly becoming 12. Range-checks
     *  against the TYPE only; declared min/max are the caller's business (see
     *  in_range). T_STR always returns PARSE_MALFORMED - a string is not a
     *  scalar; use validate_str().
     *
     *  @p str is a T_ENUM's converter, and it makes the labels part of the
     *  TYPE rather than of a band:
     *    - `text` matching a label parses to that label's code, so an operator
     *      types the word rather than looking a number up;
     *    - a NUMBER the converter does not map is PARSE_RANGE. An enum's codes
     *      are not an interval, so `in_range` cannot express "one of these" and
     *      a bare band would admit the gaps between them.
     *  Null (the default) leaves T_ENUM a plain small unsigned, which is what
     *  every other type passes. */
    ParseResult parse_scalar(VarType t, const char* text, Scalar* out,
                             const char* (*str)(uint64_t) = nullptr);

    /** @brief Does @p text satisfy @p kind, and fit in @p cap bytes incl NUL? */
    bool validate_str(StrKind kind, const char* text, uint16_t cap);

    /** @brief Dotted-quad IPv4. */
    bool validate_ipv4(const char* s, bool allow_empty = false);
    /** @brief Hostname or dotted quad, at most @p cap bytes incl NUL. */
    bool validate_hostname(const char* s, uint16_t cap, bool allow_empty = false);

    /** @} */

}}  // namespace Botanik::Vars

#endif // VARS_SCALAR_H
