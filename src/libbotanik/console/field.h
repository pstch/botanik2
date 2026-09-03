// REVIEW_35: TODO
#ifndef CONSOLE_FIELD_H
#define CONSOLE_FIELD_H

/**
 * @file field.h
 * @brief Shared fixed-width field render + per-column diff.
 *
 * MetricPage diffs a fixed-width field against its last value and asks for the
 * changed runs to be repainted (the watch-style highlight); the mechanical
 * render-and-changed split lives here. The status bar builds its own mask inline
 * and does not use this.
 */

#include <cstdint>

namespace Botanik { namespace Terminal { namespace Field {

    /** @brief Render @p s into @p cf as a @p width-column, space-padded (and
     *  NUL-terminated at [width]) field, truncating an over-long value.
     *  @p cf must hold width+1 bytes. */
    inline void render(char* cf, const char* s, uint16_t width) {
        uint16_t vlen = 0;
        for (const char* p = s; *p && vlen < width; ++p) cf[vlen++] = *p;
        for (uint16_t i = vlen; i < width; ++i) cf[i] = ' ';
        cf[width] = '\0';
    }

    /** @brief Per-column change mask (bit i set = column i differs) of @p cf vs
     *  @p prev over [0, @p width). @p width <= 32 (the mask is one uint32).
     *  @note Whether there IS a meaningful previous value is the caller's to
     *  know: a field drawn onto a freshly cleared screen differs from blank in
     *  every glyph, and highlighting that would light the whole page. */
    inline uint32_t changed(const char* cf, const char* prev, uint16_t width) {
        uint32_t chg = 0;
        for (uint16_t i = 0; i < width; ++i)
            if (cf[i] != prev[i]) chg |= (1u << i);
        return chg;
    }

}}}

#endif // CONSOLE_FIELD_H
