#ifndef UTILS_TEXT_H
#define UTILS_TEXT_H

/**
 * @file text.h
 * @brief Small text helpers shared by the line-wrapping paths in the pages and
 *        shell layers.
 */

#include <cstddef>
#include <cstdint>

namespace Botanik { namespace Utils {

    /** @name Line wrapping
     *  @{ */

    /** @brief Length of the next display segment of [buf+off, buf+len): up to
     *  @p budget bytes, stopping before an embedded '\n' (which the caller consumes).
     *  @note This is the inner scan shared by the various line-wrapping loops. */
    inline uint16_t scan_line_segment(const char* buf, size_t off,
                                      size_t len, uint16_t budget) {
        uint16_t take = 0;
        while (take < budget && off + take < len && buf[off + take] != '\n') take++;
        return take;
    }

    /** @} */

    /** @name Number formatting
     *  @{ */

    /** @brief Format @p value as a fixed-point decimal into @p buf, mirroring
     *  printf's `%0<width>.<decimals>f`, and return @p buf for inline use.
     *
     *  Our Mbed OS build ships a newlib-nano @c snprintf with floating-point
     *  conversion stubbed out, so @c %f produces nothing. Format the number with
     *  this helper first and feed the result to @c snprintf via @c %s:
     *  @code
     *      char fb[12];
     *      snprintf(buf, sizeof(buf), "%s C", format_float(fb, sizeof(fb), t, 1));
     *  @endcode
     *
     *  @param buf       Destination buffer (always NUL-terminated when @p cap > 0).
     *  @param cap       Size of @p buf in bytes.
     *  @param value     Value to format. Rounded half away from zero to @p decimals
     *                   places (diverges from %f only on exact .5 ties).
     *  @param decimals  Digits after the decimal point (0 omits the point). Capped at 9.
     *  @param width     Minimum total field width, left-padded with zeros after the
     *                   sign (0 = no padding), matching the `%0<width>` flag.
     *  @note Non-finite inputs render as "nan" / "inf" / "-inf". */
    const char* format_float(char* buf, size_t cap, double value,
                             uint8_t decimals = 1, uint8_t width = 0);

    /** @} */

    /** @name Duration formatting
     *  @{ */

    /** @brief Format @p secs as a fixed-width age string into @p buf and return
     *  @p buf for inline use.
     *
     *  Picks the largest sensible unit so an age reads naturally instead of piling
     *  up seconds: seconds, then minutes ('m'), hours ('h'), days ('d'), years
     *  ('y'). The field is constant-width ("%3u %c ago", 9 chars; "%3u %c", 5 chars
     *  without the suffix), so a cell showing it never reflows as the age grows.
     *
     *  @param buf        Destination buffer (NUL-terminated when @p cap > 0); needs >= 10 bytes.
     *  @param cap        Size of @p buf in bytes.
     *  @param secs       Age in seconds.
     *  @param ago        Append " ago" (true, default) or just the value+unit (false).
     *  @param floor_min  Floor the unit at minutes (a sub-minute age reads "0 m")
     *                    instead of showing seconds. */
    const char* format_age(char* buf, size_t cap, uint32_t secs,
                           bool ago = true, bool floor_min = false);

    /** @} */

    /** @name Environment
     *  @{ */

    /** @brief Read environment variable @p name as an unsigned 8-bit integer.
     *  Returns @p fallback when it is unset, empty, non-numeric, or outside 0..255.
     *  On targets without an environment (OPTA) getenv yields null, so this always
     *  returns @p fallback. */
    uint8_t env_u8(const char* name, uint8_t fallback = 0);

    /** @} */

}}

#endif // UTILS_TEXT_H
