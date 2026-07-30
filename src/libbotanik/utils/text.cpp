// REVIEW_35: DONE
/**
 * @file text.cpp
 * @brief Text helper implementations - the fixed-point float formatter that
 *        stands in for the missing `%f` in our snprintf, plus the duration
 *        formatter for "N u ago" readouts.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#include "text.h"

namespace Botanik { namespace Utils {

    // Number formatting
    // ------------------------------------------------------------------------

    // Emit the `r` chars of `rev` (held low-to-high) into `out` un-reversed,
    // honoring `cap`; out is NUL-terminated.
    static const char* _emit_reversed(char* out, size_t cap, const char* rev, size_t r) {
        size_t w = 0;
        while (r > 0 && w + 1 < cap) out[w++] = rev[--r];
        if (cap > 0) out[w] = '\0';
        return out;
    }

    const char* format_float(char* buf, size_t cap, double value,
                             uint8_t decimals, uint8_t width) {
        // Safety check
        if (cap == 0) return buf;

        // NaN handling
        if (value != value) return _emit_reversed(buf, cap, "nan", 3);

        // Infinity handling
        if (value > 1e308 || value < -1e308) {
            char rev[4] = { 'f', 'n', 'i', 0 };          // "inf" reversed
            size_t r = 3;
            if (value < 0.0) rev[r++] = '-';
            return _emit_reversed(buf, cap, rev, r);
        }

        // Cap decimals to 9
        if (decimals > 9) decimals = 9;

        // Handle negative values
        bool negative = value < 0.0;
        if (negative) value = -value;

        // Separate into integer and fractional parts
        uint64_t scale = 1;
        for (uint8_t i = 0; i < decimals; i++) scale *= 10;
        uint64_t scaled = (uint64_t)llround(value * (double)scale);
        uint64_t ipart  = scaled / scale;
        uint64_t fpart  = scaled % scale;

        // Build the body low-digit-first (reversed).
        // FIXME: make more readable
        char rev[40];
        size_t r = 0;
        for (uint8_t i = 0; i < decimals; i++) { rev[r++] = (char)('0' + fpart % 10); fpart /= 10; }
        if (decimals > 0) rev[r++] = '.';
        do { rev[r++] = (char)('0' + ipart % 10); ipart /= 10; } while (ipart && r < sizeof(rev) - 2);

        // Zero-pad (after the sign) up to `width`, then prepend the sign.
        size_t sign_w = negative ? 1 : 0;
        while (r + sign_w < width && r < sizeof(rev) - 1) rev[r++] = '0';
        if (negative) rev[r++] = '-';

        // Reverse written characters and return
        return _emit_reversed(buf, cap, rev, r);
    }

    const char* format_uint(char* buf, size_t cap, unsigned long value, uint8_t width) {
        if (cap == 0) return buf;
        // FIXME: make more readable
        char rev[24];
        size_t r = 0;
        do { rev[r++] = (char)('0' + value % 10); value /= 10; } while (value && r < sizeof(rev) - 1);
        while (r < width && r < sizeof(rev) - 1) rev[r++] = '0';
        return _emit_reversed(buf, cap, rev, r);
    }

    const char* format_int(char* buf, size_t cap, long long value, uint8_t width) {
        if (cap == 0) return buf;
        // FIXME: make more readable
        bool negative = value < 0;
        // Magnitude as unsigned so LLONG_MIN negates without overflow.
        unsigned long long mag = negative ? (unsigned long long)(-(value + 1)) + 1
                                          : (unsigned long long)value;
        char rev[24];   // 20 digits + sign, with slack
        size_t r = 0;
        do { rev[r++] = (char)('0' + mag % 10); mag /= 10; } while (mag && r < sizeof(rev) - 1);
        // Zero-pad (after the sign) up to width, then prepend the sign last.
        size_t sign_w = negative ? 1 : 0;
        while (r + sign_w < width && r < sizeof(rev) - 1) rev[r++] = '0';
        if (negative) rev[r++] = '-';
        return _emit_reversed(buf, cap, rev, r);
    }

    const char* format_size(char* buf, size_t cap, uint32_t bytes) {
        if (cap == 0) return buf;
        unsigned long v;
        const char* unit;

        // Format as rounded kibibytes when > 32 KiB
        if (bytes >= 32768u) {
            v = (unsigned long)((bytes + 512u) / 1024u);
            unit = " KB";
        } else {
            v = (unsigned long)bytes;
            unit = " B";
        }
        format_uint(buf, cap, v);

        // Append the unit within cap (format_uint left buf NUL-terminated).
        size_t n = 0;
        while (buf[n] != '\0') n++;
        for (size_t i = 0; unit[i] != '\0' && n + 1 < cap; i++) buf[n++] = unit[i];
        buf[n] = '\0';
        return buf;
    }

    // Duration formatting
    // ------------------------------------------------------------------------

    const char* format_age(char* buf, size_t cap, uint32_t secs, bool ago, bool floor_min) {
        unsigned v = secs;
        char     u = 's';

        // Decompose value into years/days/hours/minutes
        if      (secs >= 31536000) { v = secs / 31536000u; u = 'y'; }
        else if (secs >= 86400)    { v = secs / 86400u;    u = 'd'; }
        else if (secs >= 3600)     { v = secs / 3600u;     u = 'h'; }
        else if (secs >= 60)       { v = secs / 60u;       u = 'm'; }
        else if (floor_min)        { v = 0;                u = 'm'; }

        // Format in a string buffer
        // FIXME: let's try do this witout snprintf
        if (ago) snprintf(buf, cap, "%3u %c ago", v, u);
        else     snprintf(buf, cap, "%3u %c", v, u);

        return buf;
    }

    // Environment
    // ------------------------------------------------------------------------

    uint32_t env_u32(const char* name, uint32_t fallback) {
        // read environment variable or return fallback if unset
        const char* v = getenv(name);
        if (v == nullptr || *v == '\0') return fallback;
        // measure value length
        uint16_t len = 0;
        while (v[len] != '\0') len++;
        uint32_t n;
        // parse unsigned integer; anything malformed keeps the fallback
        if (!parse_uint(v, len, &n)) return fallback;
        return n;
    }

    uint8_t env_u8(const char* name, uint8_t fallback) {
        // Sentinel-free: read as u32 and reject what doesn't fit a byte. 256 is
        // out of range, not a wrap to 0.
        uint32_t n = env_u32(name, fallback);
        return n > 255 ? fallback : (uint8_t)n;
    }

}}
