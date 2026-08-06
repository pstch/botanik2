// REVIEW_35: DONE
#ifndef UTILS_TEXT_H
#define UTILS_TEXT_H

/**
 * @file text.h
 * @brief Small text helpers shared by the line-wrapping paths in the pages and
 *        shell layers.
 */

#include <cstddef>                    // size_t - the caller-supplied buffer capacities
#include <cstdint>                    // uint8_t/uint16_t/uint32_t - the values formatted
#include <cstring>                    // strcmp - compare_str's inline body

namespace Botanik { namespace Utils {

    /** @name String comparison
     *  @{ */

    /** @brief Null-safe equality of two NUL-terminated strings
     *  @returns True when both pointers are non-null and contain the same text. */
    inline bool compare_str(const char* a, const char* b) {
        return a != nullptr && b != nullptr && std::strcmp(a, b) == 0;
    }

    /** @}
     *  @name Line wrapping
     *  @{ */

    /** @brief Length of the next display segment of [buf+off, buf+len): up to
     *  @p budget bytes, stopping before an embedded '\n' (the caller consumes
     *  it). */
    inline uint16_t scan_line_segment(const char* buf, size_t off,
                                      size_t len, uint16_t budget) {
        uint16_t take = 0;
        while (take < budget && off + take < len && buf[off + take] != '\n') take++;
        return take;
    }

    /** @}
     *  @name Numeric parsing
     *  @{ */

    /** @brief Parse [@p s, @p s+len) as a decimal uint32 into @p out. Returns
     *  false on an empty string, a non-digit, or overflow. */
    inline bool parse_uint(const char* s, uint16_t len, uint32_t* out) {
        // FIXME: make more readable. the hidden constant is annoying
        if (len == 0) return false;
        uint32_t v = 0;
        for (uint16_t i = 0; i < len; i++) {
            if (s[i] < '0' || s[i] > '9') return false;
            uint32_t d = (uint32_t)(s[i] - '0');
            if (v > (0xFFFFFFFFu - d) / 10u) return false;   // overflow
            v = v * 10u + d;
        }
        *out = v;
        return true;
    }

    /** @}
     *  @name Number formatting
     *  @{ */

    /** @brief Format @p value as a fixed-point decimal into @p buf, mirroring
     *  printf's `%0<width>.<decimals>f`, and return @p buf for inline use.
     *
     *  Our Mbed newlib-nano @c snprintf stubs out @c %f, so format with this
     *  first and feed the result via @c %s:
     *  @code
     *      char fb[12];
     *      snprintf(buf, sizeof(buf), "%s C", format_float(fb, sizeof(fb), t, 1));
     *  @endcode
     *
     *  @param value     Rounded half away from zero to @p decimals places
     *                   (diverges from %f only on exact .5 ties).
     *  @param decimals  Digits after the point (0 omits it). Capped at 9.
     *  @param width     Min field width, zero-padded after the sign (0 = none).
     *  @note Non-finite inputs render "nan" / "inf" / "-inf"; NUL-terminated
     *  when cap > 0. */
    const char* format_float(char* buf, size_t cap, double value,
                             uint8_t decimals = 1);

    /** @brief Format @p value as a signed decimal into @p buf (the `%lld`
     *  stand-in), and return @p buf for inline use. Takes @c long @c long so
     *  callers pass any integer, signed or not, without a magnitude concern.
     *  @note NUL-terminated when @p cap > 0; the negation is done on the
     *  unsigned magnitude, so @c LLONG_MIN is handled without overflow. */
    const char* format_int(char* buf, size_t cap, long long value);

    /** @brief Format @p bytes into @p buf as "<n> B" below 32 KiB, or "<n> KB"
     *  (rounded to the nearest KiB) at or above it. Returns @p buf for inline
     *  use. */
    const char* format_size(char* buf, size_t cap, uint32_t bytes);

    /** @}
     *  @name Duration formatting
     *  @{ */

    /** @brief Format @p secs as a fixed-width age string into @p buf (returned
     *  for inline use). Picks the largest sensible unit (s/m/h/d/y).
     *  Constant-width ("%3u %c ago" / "%3u %c"), so a cell never reflows as the
     *  age grows. @param buf NUL-terminated when @p cap > 0; needs >= 10 bytes.
     *  @param ago Append " ago" (default) or just value+unit. @param floor_min
     *  Floor the unit at minutes (sub-minute reads "0 m"). */
    const char* format_age(char* buf, size_t cap, uint32_t secs,
                           bool ago = true, bool floor_min = false);

    /** @} */

}}

#endif // UTILS_TEXT_H
