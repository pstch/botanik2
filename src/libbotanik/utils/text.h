// REVIEW_35: DONE
#ifndef UTILS_TEXT_H
#define UTILS_TEXT_H

/**
 * @file text.h
 * @brief Small text helpers shared by the line-wrapping paths in the pages and
 *        shell layers.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>

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
     *  @name Argument parsing helpers (for the shell) 
     *  @{ */

    /** @brief Extract the next whitespace-delimited word from [@p args, @p
     *  args+alen). Returns the word start (nullptr if none) and sets @p wlen to
     *  its length, advancing @p args / @p alen past the word (leading spaces
     *  are skipped first). */
    inline const char* arg_next(const char*& args, uint16_t& alen, uint16_t& wlen) {
        while (alen > 0 && *args == ' ') { args++; alen--; }
        if (alen == 0) { wlen = 0; return nullptr; }
        const char* w = args;
        wlen = 0;
        while (alen > 0 && *args != ' ') { args++; alen--; wlen++; }
        return w;
    }

    /** @brief True if word [@p w, @p w+wlen) equals the NUL-terminated literal
     *  @p lit. */
    inline bool arg_eq(const char* w, uint16_t wlen, const char* lit) {
        for (uint16_t i = 0; i < wlen; i++)
            if (lit[i] == '\0' || w[i] != lit[i]) return false;
        return lit[wlen] == '\0';
    }

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
                             uint8_t decimals = 1, uint8_t width = 0);

    /** @brief Format @p value as an unsigned decimal into @p buf (the `%lu`
     *  stand-in), and return @p buf for inline use. Digits are peeled with
     *  integer math, so it carries no libc/`%f` baggage.
     *  @param width  Min field width, zero-padded (0 = none).
     *  @note NUL-terminated when @p cap > 0. */
    const char* format_uint(char* buf, size_t cap, unsigned long value,
                            uint8_t width = 0);

    /** @brief Format @p value as a signed decimal into @p buf (the `%lld`
     *  stand-in), and return @p buf for inline use. Takes @c long @c long so
     *  callers pass any signed integer without a magnitude concern.
     *  @param width  Min field width, zero-padded after the sign (0 = none).
     *  @note NUL-terminated when @p cap > 0; the negation is done on the
     *  unsigned magnitude, so @c LLONG_MIN is handled without overflow. */
    const char* format_int(char* buf, size_t cap, long long value,
                           uint8_t width = 0);

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

    /** @}
     *  @name Environment
     *  @{ */

    /** @brief Read environment variable @p name as an unsigned 8-bit integer.
     *  @returns @p fallback if unset, otherwise parsed uint8_t value.
     *  @note This is only for the simulator, on other targets we just return @p fallback . */
    uint8_t env_u8(const char* name, uint8_t fallback = 0);

    /** @} */

}}

#endif // UTILS_TEXT_H
