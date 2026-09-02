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

    /** @brief Order NUL-terminated @p s against the word [@p key, @p key+len);
     *  @p len 0 means @p key is NUL-terminated too. strcmp's sign convention; a
     *  null @p s sorts after everything and matches nothing.
     *
     *  One rule, three users: the variable bank's sorted name index, the task
     *  registry's program lookup, and the shell matching an argument. All three
     *  ask "is this stored name the word in front of me", where the word is a
     *  slice of a command line with no terminator of its own - and all three had
     *  their own copy of the terminator check that makes a prefix not a match. */
    int compare_bounded(const char* s, const char* key, uint16_t len);

    /** @brief The equality form of compare_bounded(). */
    inline bool equals_bounded(const char* s, const char* key, uint16_t len) {
        return compare_bounded(s, key, len) == 0;
    }

    /** @brief Whether @p hay contains @p needle, ASCII case folded. An empty
     *  @p needle matches anything; a null @p hay matches nothing. */
    bool contains_ci(const char* hay, const char* needle);

    /** @brief Whether NUL-terminated @p s matches the pattern [@p pat,
     *  @p pat+len), where `*` stands for any run of characters and `?` for one.
     *
     *  A pattern with NO wildcard matches as a PREFIX, which is what every
     *  shell listing meant before wildcards existed: `vars list cli_` still
     *  lists the group. One with a wildcard is anchored at both ends, so
     *  `*_pos` means ends-with and `cli_win*_pos` means both.
     *  @note Iterative, with one backtrack point: a pathological pattern costs
     *  time, never stack. */
    bool glob_bounded(const char* s, const char* pat, uint16_t len);

    /** @brief glob_bounded()'s ASCII case-folded form over a NUL-terminated
     *  pattern, where a wildcard-free pattern matches as a SUBSTRING - the row
     *  filter a table page has always had, until a `*` or `?` is typed. */
    bool glob_ci(const char* s, const char* pat);

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

    /** @brief How a parse of a whole numeric field ended. */
    enum NumParse : uint8_t {
        NUM_OK,
        NUM_MALFORMED,   ///< empty, a non-digit, or trailing junk
        NUM_RANGE        ///< well formed, but outside the target's range
    };

    /** @brief Parse [@p s, @p s+len) as a decimal uint32 into @p out. Returns
     *  false on an empty string, a non-digit, or overflow. */
    bool parse_uint(const char* s, uint16_t len, uint32_t* out);

    /** @brief Parse NUL-terminated @p s as a decimal uint64 into @p out,
     *  tolerating spaces and tabs on both ends.
     *  @note A leading `-` is NUM_RANGE: a negative is well formed input this
     *  type cannot hold, and must never arrive as a huge positive. */
    NumParse parse_u64(const char* s, uint64_t* out);

    /** @brief Parse NUL-terminated @p s as a decimal int64 into @p out,
     *  tolerating spaces and tabs on both ends.
     *  @note The magnitude is scanned unsigned, so INT64_MIN parses. */
    NumParse parse_i64(const char* s, int64_t* out);

    /** @}
     *  @name Number formatting
     *  @{ */

    /** @brief Format @p value as a fixed-point decimal into @p buf, mirroring
     *  printf's `%.<decimals>f`, and return @p buf for inline use.
     *
     *  Our Mbed newlib-nano @c snprintf stubs out @c %f, so format with this
     *  first and feed the result through @c %s.
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
