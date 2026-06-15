/**
 * @file text.cpp
 * @brief Text helper implementations — the fixed-point float formatter that
 *        stands in for the missing `%f` in our snprintf, plus the duration
 *        formatter for "N u ago" readouts.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>    // snprintf — integer/char conversions are intact on our Mbed nano
#include <cmath>     // llround — round-half-away-from-zero without the +0.5 float trap

#include "libbotanik/utils/text.h"

namespace Botanik { namespace Utils {

    // Number formatting
    // ------------------------------------------------------------------------

    // Emit a reversed body (digits/sign/padding low-to-high) into `out`, honoring
    // `cap`. `rev` holds `r` characters in reverse order; out is NUL-terminated.
    static const char* _emit_reversed(char* out, size_t cap, const char* rev, size_t r) {
        size_t w = 0;
        while (r > 0 && w + 1 < cap) out[w++] = rev[--r];
        if (cap > 0) out[w] = '\0';
        return out;
    }

    const char* format_float(char* buf, size_t cap, double value,
                             uint8_t decimals, uint8_t width) {
        if (cap == 0) return buf;

        // Non-finite values: emit a tag rather than numeric garbage. (value !=
        // value is the standard NaN test; comparing against a huge bound catches
        // infinities without pulling in <cmath>.)
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

        // Scale by 10^decimals, then round half away from zero into an integer so
        // the digits fall out with plain integer arithmetic (any rounding carry
        // into the integer part happens for free). This diverges from libc's %f
        // only on exact .5 ties — it rounds them up where %f rounds to even —
        // which costs at most 1 in the last shown digit. Matching %f's tie-break
        // would need an arbitrary-precision expansion of the double, and on the
        // OPTA core (long double == double) the usual precision trick wouldn't
        // even be portable, so it isn't worth it for a sensor readout.
        uint64_t scale = 1;
        for (uint8_t i = 0; i < decimals; i++) scale *= 10;
        uint64_t scaled = (uint64_t)llround(value * (double)scale);
        uint64_t ipart  = scaled / scale;
        uint64_t fpart  = scaled % scale;

        // Build the body low-digit-first, so it ends up reversed.
        char rev[40];
        size_t r = 0;
        for (uint8_t i = 0; i < decimals; i++) { rev[r++] = (char)('0' + fpart % 10); fpart /= 10; }
        if (decimals > 0) rev[r++] = '.';
        do { rev[r++] = (char)('0' + ipart % 10); ipart /= 10; } while (ipart && r < sizeof(rev) - 2);

        // Zero-pad (after the sign) up to `width`, then prepend the sign last so
        // it lands at the front of the final, un-reversed string.
        size_t sign_w = negative ? 1 : 0;
        while (r + sign_w < width && r < sizeof(rev) - 1) rev[r++] = '0';
        if (negative) rev[r++] = '-';

        return _emit_reversed(buf, cap, rev, r);
    }

    // Duration formatting
    // ------------------------------------------------------------------------

    const char* format_age(char* buf, size_t cap, uint32_t secs) {
        unsigned v = secs;
        char     u = 's';
        if      (secs >= 86400) { v = secs / 86400u; u = 'd'; }
        else if (secs >= 3600)  { v = secs / 3600u;  u = 'h'; }
        else if (secs >= 60)    { v = secs / 60u;    u = 'm'; }
        snprintf(buf, cap, "%3u %c ago", v, u);
        return buf;
    }

}}
