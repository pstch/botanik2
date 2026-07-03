// REVIEW_35: TODO
#ifndef TERMINAL_FIELD_H
#define TERMINAL_FIELD_H

/**
 * @file field.h
 * @brief Shared fixed-width field render + per-column diff.
 *
 * The row-0 status bar and the full-screen MetricPage both "diff a fixed-width
 * field and repaint only the changed runs" over the terminal's
 * paint_field_runs() painter. Each keeps its own value cache, highlight policy,
 * and pacing; only the mechanical render-and-diff is common, and lives here so
 * the two can't drift apart.
 */

#include <cstdint>

namespace Botanik { namespace Terminal { namespace Field {

    /** @brief Render @p s into @p cf as a @p width-column, space-padded (and
     *  NUL-terminated at [width]) field, truncating an over-long value. Returns
     *  the printed length before padding, which a whole-value highlight uses to
     *  light only the glyphs. @p cf must hold width+1 bytes. */
    inline uint16_t render(char* cf, const char* s, uint16_t width) {
        uint16_t vlen = 0;
        for (const char* p = s; *p && vlen < width; ++p) cf[vlen++] = *p;
        for (uint16_t i = vlen; i < width; ++i) cf[i] = ' ';
        cf[width] = '\0';
        return vlen;
    }

    /** @brief Per-column change mask (bit i set = column i differs) of @p cf vs
     *  @p prev over [0, @p width). On a @p fresh field (first paint after a
     *  clear, no valid @p prev) only non-space cells count, so a blank screen
     *  emits glyphs without spurious space writes. @p width <= 32 (the mask is
     *  one uint32). */
    inline uint32_t changed(const char* cf, const char* prev, uint16_t width,
                            bool fresh) {
        uint32_t chg = 0;
        for (uint16_t i = 0; i < width; ++i) {
            bool diff = fresh ? (cf[i] != ' ') : (cf[i] != prev[i]);
            if (diff) chg |= (1u << i);
        }
        return chg;
    }

}}}

#endif // TERMINAL_FIELD_H
