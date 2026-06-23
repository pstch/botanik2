/**
 * @file text.cpp
 * @brief Text helper implementations - the fixed-point float formatter that
 *        stands in for the missing `%f` in our snprintf, plus the duration
 *        formatter for "N u ago" readouts.
 */
#include <cstddef>
#include <cstdint>
// snprintf - integer/char conversions are intact on our Mbed nano
#include <cstdio>
#include <cstdlib>   // getenv - env_u8 (parsing is in-house via parse_uint)
// llround - round-half-away-from-zero without the +0.5 float trap
#include <cmath>

#include "libbotanik/utils/text.h"

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
        if (cap == 0) return buf;

        // Non-finite: emit a tag, not garbage. (value != value is the NaN test;
        // the huge-bound compare catches infinities without <cmath>.)
        if (value != value) return _emit_reversed(buf, cap, "nan", 3);
        if (value > 1e308 || value < -1e308) {
            char rev[4] = { 'f', 'n', 'i', 0 };          // "inf" reversed
            size_t r = 3;
            if (value < 0.0) rev[r++] = '-';
            return _emit_reversed(buf, cap, rev, r);
        }

        if (decimals > 9) decimals = 9;

        bool negative = value < 0.0;
        if (negative) value = -value;

        // Scale by 10^decimals, round half away from zero into an integer, then
        // peel digits with integer arithmetic (the rounding carry into the
        // integer part is free). Diverges from %f only on exact .5 ties (rounds
        // up vs round-to-even); matching %f's tie-break isn't worth it for a
        // sensor readout.
        uint64_t scale = 1;
        for (uint8_t i = 0; i < decimals; i++) scale *= 10;
        uint64_t scaled = (uint64_t)llround(value * (double)scale);
        uint64_t ipart  = scaled / scale;
        uint64_t fpart  = scaled % scale;

        // Build the body low-digit-first (reversed).
        char rev[40];
        size_t r = 0;
        for (uint8_t i = 0; i < decimals; i++) { rev[r++] = (char)('0' + fpart % 10); fpart /= 10; }
        if (decimals > 0) rev[r++] = '.';
        do { rev[r++] = (char)('0' + ipart % 10); ipart /= 10; } while (ipart && r < sizeof(rev) - 2);

        // Zero-pad (after the sign) up to `width`, then prepend the sign last
        // (lands at the front once un-reversed). The `r < sizeof(rev)-1` guard
        // clamps an oversized width rather than overrun, so the build buffer
        // (~39) bounds the field.
        size_t sign_w = negative ? 1 : 0;
        while (r + sign_w < width && r < sizeof(rev) - 1) rev[r++] = '0';
        if (negative) rev[r++] = '-';

        return _emit_reversed(buf, cap, rev, r);
    }

    const char* format_size(char* buf, size_t cap, uint32_t bytes) {
        if (cap == 0) return buf;
        if (bytes >= 32768u)   // >= 32 KiB: switch to KB, rounded to nearest
            snprintf(buf, cap, "%lu KB", (unsigned long)((bytes + 512u) / 1024u));
        else
            snprintf(buf, cap, "%lu B", (unsigned long)bytes);
        return buf;
    }

    // Duration formatting
    // ------------------------------------------------------------------------

    const char* format_age(char* buf, size_t cap, uint32_t secs, bool ago, bool floor_min) {
        unsigned v = secs;
        char     u = 's';
        // Roll up to years past 365 days so the value stays <= 3 digits (a
        // uint32 of seconds tops out at ~136 years), keeping the field
        // fixed-width.
        // 365-day years
        if      (secs >= 31536000) { v = secs / 31536000u; u = 'y'; }
        else if (secs >= 86400)    { v = secs / 86400u;    u = 'd'; }
        else if (secs >= 3600)     { v = secs / 3600u;     u = 'h'; }
        else if (secs >= 60)       { v = secs / 60u;       u = 'm'; }
        // sub-minute -> "0 m"
        else if (floor_min)        { v = 0;                u = 'm'; }
        if (ago) snprintf(buf, cap, "%3u %c ago", v, u);
        else     snprintf(buf, cap, "%3u %c", v, u);
        return buf;
    }

    // Environment
    // ------------------------------------------------------------------------

    uint8_t env_u8(const char* name, uint8_t fallback) {
        const char* v = getenv(name);
        if (v == nullptr || *v == '\0') return fallback;
        uint16_t len = 0;
        // bounded by the env string
        while (v[len] != '\0') len++;
        uint32_t n;
        // in-house, no strtol
        if (!parse_uint(v, len, &n) || n > 255) return fallback;
        return (uint8_t)n;
    }

}}
