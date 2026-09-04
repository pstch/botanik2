/**
 * @file text.cpp
 * @brief Text helper implementations - the fixed-point float formatter that
 *        stands in for the missing `%f` in our snprintf, plus the duration
 *        formatter for "N u ago" readouts.
 */
#include "libbotanik/utils/text.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace Botanik { namespace Utils {

    // Numeric parsing

    namespace {

        const char* _skip_spaces(const char* p) {
            while (*p == ' ' || *p == '\t') p++;
            return p;
        }

        // Decimal digits of [s, s+len) into *out, refusing a result above
        // `limit`. The overflow test is the usual v > (limit - d)/10, which is
        // exact for every limit >= 9 - the only ones any caller passes.
        NumParse _scan_digits(const char* s, uint16_t len, uint64_t limit, uint64_t* out) {
            if (len == 0) return NUM_MALFORMED;
            uint64_t v = 0;
            for (uint16_t i = 0; i < len; i++) {
                if (s[i] < '0' || s[i] > '9') return NUM_MALFORMED;
                const uint64_t d = (uint64_t)(s[i] - '0');
                if (v > (limit - d) / 10u) return NUM_RANGE;
                v = v * 10u + d;
            }
            *out = v;
            return NUM_OK;
        }

        // Length of the digit run at `p`, true when only spaces follow it.
        bool _digit_run(const char* p, uint16_t* len) {
            const char* e = p;
            while (*e >= '0' && *e <= '9') e++;
            *len = (uint16_t)(e - p);
            return *_skip_spaces(e) == 0;
        }

        // 10^22 is the largest power of ten a double holds exactly, so scaling
        // in steps no wider than this keeps every single multiply exact. One
        // clamped step past it is not a rounding error, it is a wrong decade.
        const int    POW10_MAX = 22;
        const double POW10[POW10_MAX + 1] = {
            1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
            1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
            1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
        };

    }  // namespace

    bool parse_uint(const char* s, uint16_t len, uint32_t* out) {
        uint64_t v = 0;
        if (_scan_digits(s, len, 0xFFFFFFFFu, &v) != NUM_OK) return false;
        *out = (uint32_t)v;
        return true;
    }

    NumParse parse_u64(const char* s, uint64_t* out) {
        if (s == nullptr || out == nullptr) return NUM_MALFORMED;
        const char* p = _skip_spaces(s);
        if (*p == '-') return NUM_RANGE;
        if (*p == '+') p++;
        uint16_t len = 0;
        if (!_digit_run(p, &len)) return NUM_MALFORMED;
        return _scan_digits(p, len, UINT64_MAX, out);
    }

    NumParse parse_i64(const char* s, int64_t* out) {
        if (s == nullptr || out == nullptr) return NUM_MALFORMED;
        const char* p = _skip_spaces(s);
        const bool neg = (*p == '-');
        if (neg || *p == '+') p++;
        uint16_t len = 0;
        if (!_digit_run(p, &len)) return NUM_MALFORMED;

        // INT64_MIN's magnitude is not an int64, so scan it unsigned and negate
        // on the unsigned value.
        const uint64_t limit = neg ? 9223372036854775808ull : (uint64_t)INT64_MAX;
        uint64_t mag = 0;
        const NumParse r = _scan_digits(p, len, limit, &mag);
        if (r != NUM_OK) return r;
        *out = neg ? (int64_t)(~mag + 1u) : (int64_t)mag;
        return NUM_OK;
    }

    NumParse parse_f64(const char* s, double* out) {
        if (s == nullptr || out == nullptr) return NUM_MALFORMED;
        const char* p   = _skip_spaces(s);
        const bool  neg = (*p == '-');
        if (neg || *p == '+') p++;

        // The digits as one integer, with the point's position folded into the
        // exponent. Past the 19th digit the mantissa is full: an integer digit
        // then moves the exponent instead, and a fractional one is dropped,
        // being far below what a float can hold anyway.
        uint64_t mant  = 0;
        int      exp   = 0;
        bool     digit = false;
        bool     dot   = false;
        for (;; p++) {
            if (*p == '.') {
                if (dot) return NUM_MALFORMED;
                dot = true;
                continue;
            }
            if (*p < '0' || *p > '9') break;
            digit = true;
            if (mant <= (UINT64_MAX - 9u) / 10u) {
                mant = mant * 10u + (uint64_t)(*p - '0');
                if (dot) exp--;
            } else if (!dot) {
                exp++;
            }
        }
        if (!digit) return NUM_MALFORMED;

        if (*p == 'e' || *p == 'E') {
            const char* q    = p + 1;
            const bool  eneg = (*q == '-');
            if (eneg || *q == '+') q++;
            uint16_t elen = 0;
            const char* e = q;
            while (*e >= '0' && *e <= '9') { e++; elen++; }
            if (elen == 0) return NUM_MALFORMED;      // "1e" is not a number
            uint64_t ev = 0;
            if (_scan_digits(q, elen, 9999u, &ev) != NUM_OK) return NUM_RANGE;
            exp += eneg ? -(int)ev : (int)ev;
            p = e;
        }
        if (*_skip_spaces(p) != 0) return NUM_MALFORMED;

        double v = (double)mant;
        for (int e = exp; e > 0; ) {
            const int k = (e < POW10_MAX) ? e : POW10_MAX;
            v *= POW10[k];
            e -= k;
        }
        for (int e = exp; e < 0; ) {
            const int k = (-e < POW10_MAX) ? -e : POW10_MAX;
            v /= POW10[k];
            e += k;
        }
        *out = neg ? -v : v;
        return NUM_OK;
    }

    // Number formatting

    // Emit the `r` chars of `rev` (held low-to-high) into `out` un-reversed,
    // honoring `cap`; out is NUL-terminated.
    static const char* _emit_reversed(char* out, size_t cap, const char* rev, size_t r) {
        size_t w = 0;
        while (r > 0 && w + 1 < cap) out[w++] = rev[--r];
        if (cap > 0) out[w] = '\0';
        return out;
    }

    const char* format_float(char* buf, size_t cap, double value,
                             uint8_t decimals) {
        if (cap == 0) return buf;

        if (value != value) return _emit_reversed(buf, cap, "nan", 3);  // "nan" reverses to itself

        if (value > 1e308 || value < -1e308) {
            char rev[4] = { 'f', 'n', 'i', 0 };          // "inf" reversed
            size_t r = 3;
            if (value < 0.0) rev[r++] = '-';
            return _emit_reversed(buf, cap, rev, r);
        }

        if (decimals > 9) decimals = 9;

        bool negative = value < 0.0;
        if (negative) value = -value;

        // Separate into integer and fractional parts
        uint64_t scale = 1;
        for (uint8_t i = 0; i < decimals; i++) scale *= 10;
        // llround()'s rule, without llround(): value is non-negative by here,
        // so half away from zero is half up. Compare the remainder rather than
        // adding 0.5 first - for the double just under .5 that addition rounds
        // to 1.0 and carries a digit that is not there.
        const double  wanted = value * (double)scale;
        uint64_t      scaled = (uint64_t)wanted;
        if (wanted - (double)scaled >= 0.5) scaled++;
        uint64_t ipart  = scaled / scale;
        uint64_t fpart  = scaled % scale;

        // Build the body low-digit-first (reversed).
        char rev[40];
        size_t r = 0;
        for (uint8_t i = 0; i < decimals; i++) { rev[r++] = (char)('0' + fpart % 10); fpart /= 10; }
        if (decimals > 0) rev[r++] = '.';
        do { rev[r++] = (char)('0' + ipart % 10); ipart /= 10; } while (ipart && r < sizeof(rev) - 2);

        if (negative) rev[r++] = '-';
        return _emit_reversed(buf, cap, rev, r);
    }

    // ASCII lowercase; anything else is left alone.
    static inline char _fold(char c) {
        return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }

    int compare_bounded(const char* s, const char* key, uint16_t len) {
        if (s == nullptr)   return (key == nullptr) ? 0 : 1;
        if (key == nullptr) return -1;
        if (len == 0) return strcmp(s, key);
        const int c = strncmp(s, key, len);
        if (c != 0) return c;
        // Equal over the first `len`: a longer stored name sorts AFTER the key,
        // which is what makes a prefix not a match and keeps both arms of the
        // order the same.
        return (s[len] == 0) ? 0 : 1;
    }

    int compare_bounded_ci(const char* s, const char* key, uint16_t len) {
        if (s == nullptr)   return (key == nullptr) ? 0 : 1;
        if (key == nullptr) return -1;
        uint16_t i = 0;
        for (;; i++) {
            // The key ends where its length says, or at its own NUL.
            const bool key_end = (len != 0 && i >= len) || key[i] == '\0';
            if (key_end) return (s[i] == '\0') ? 0 : 1;
            if (s[i] == '\0') return -1;
            const char a = _fold(s[i]), b = _fold(key[i]);
            if (a != b) return (a < b) ? -1 : 1;
        }
    }

    bool contains_ci(const char* hay, const char* needle) {
        if (needle == nullptr || needle[0] == '\0') return true;
        if (hay == nullptr) return false;
        // Folded here rather than through tolower(): a plain char is signed on
        // ARM, and the table pages feed this every row of a repaint.
        for (; *hay != '\0'; hay++) {
            const char* h = hay;
            const char* n = needle;
            while (*n != '\0' && *h != '\0' && _fold(*h) == _fold(*n)) { h++; n++; }
            if (*n == '\0') return true;
        }
        return false;
    }

    // Whether [pat, pat+len) holds a wildcard. Both matchers anchor themselves
    // only once one does; without it each keeps the meaning it had before
    // wildcards existed.
    static bool _has_wild(const char* pat, uint16_t len) {
        for (uint16_t k = 0; k < len; k++)
            if (pat[k] == '*' || pat[k] == '?') return true;
        return false;
    }

    static inline bool _same(char a, char b, bool fold) {
        return fold ? (_fold(a) == _fold(b)) : (a == b);
    }

    // [pat, pat+len) against the WHOLE of s, both ends anchored.
    static bool _glob_anchored(const char* s, const char* pat, uint16_t len, bool fold) {
        // One star may need to give characters back, so its position and the
        // input it was first tried at are remembered. Only the LAST star has to
        // be: an earlier one can always yield to it.
        uint16_t p = 0, i = 0;
        uint16_t star = len, star_i = 0;   // star == len: none seen yet
        while (s[i] != 0) {
            if (p < len && (pat[p] == '?' || _same(pat[p], s[i], fold))) { p++; i++; continue; }
            if (p < len && pat[p] == '*')  { star = p++; star_i = i; continue; }
            if (star != len)               { p = (uint16_t)(star + 1); i = ++star_i; continue; }
            return false;
        }
        // Trailing stars match the empty rest.
        while (p < len && pat[p] == '*') p++;
        return p == len;
    }

    bool glob_bounded(const char* s, const char* pat, uint16_t len) {
        if (s == nullptr) return false;
        if (pat == nullptr || len == 0) return true;
        // No wildcard: the pattern is a plain prefix.
        if (!_has_wild(pat, len)) {
            uint16_t k = 0;
            while (k < len && s[k] != 0 && s[k] == pat[k]) k++;
            return k == len;
        }
        return _glob_anchored(s, pat, len, false);
    }

    bool glob_ci(const char* s, const char* pat) {
        if (pat == nullptr || pat[0] == '\0') return true;
        if (s == nullptr) return false;
        const uint16_t len = (uint16_t)strlen(pat);
        // No wildcard: the pattern is a plain substring.
        if (!_has_wild(pat, len)) return contains_ci(s, pat);
        return _glob_anchored(s, pat, len, true);
    }

    const char* format_int(char* buf, size_t cap, long long value) {
        if (cap == 0) return buf;
        bool negative = value < 0;
        // Magnitude as unsigned so LLONG_MIN negates without overflow.
        unsigned long long mag = negative ? (unsigned long long)(-(value + 1)) + 1
                                          : (unsigned long long)value;
        char rev[24];   // 20 digits + sign, with slack
        size_t r = 0;
        do { rev[r++] = (char)('0' + mag % 10); mag /= 10; } while (mag && r < sizeof(rev) - 1);
        if (negative) rev[r++] = '-';
        return _emit_reversed(buf, cap, rev, r);
    }

    const char* format_size(char* buf, size_t cap, uint32_t bytes) {
        if (cap == 0) return buf;
        long long v;
        const char* unit;

        // Format as rounded kibibytes when > 32 KiB
        if (bytes >= 32768u) {
            v = (long long)((bytes + 512u) / 1024u);
            unit = " KB";
        } else {
            v = (long long)bytes;
            unit = " B";
        }
        format_int(buf, cap, v);

        // Append the unit within cap (format_int left buf NUL-terminated).
        size_t n = 0;
        while (buf[n] != '\0') n++;
        for (size_t i = 0; unit[i] != '\0' && n + 1 < cap; i++) buf[n++] = unit[i];
        buf[n] = '\0';
        return buf;
    }

    // Duration formatting

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
        // FIXME: let's try do this without snprintf
        if (ago) snprintf(buf, cap, "%3u %c ago", v, u);
        else     snprintf(buf, cap, "%3u %c", v, u);

        return buf;
    }

}}
