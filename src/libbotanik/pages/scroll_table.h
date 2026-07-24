// REVIEW_35: TODO
#ifndef PAGES_SCROLL_TABLE_H
#define PAGES_SCROLL_TABLE_H

/**
 * @file scroll_table.h
 * @brief ScrollTablePage - shared scroll-band + selection-cursor machinery for
 *        the bordered table pages (ALARMS / MONITOR / CONFIG).
 *
 * Owns the window offset (_top) and the selection cursor (_selected). A subclass
 * supplies only the band geometry (_data_top / _window) and the row count;
 * clamping, follow-scroll, and the DL/IL band shift are shared.
 *
 * Moving the cursor past a window edge scrolls the band through
 * AnsiTerminal::scroll_region(), so the terminal's back-buffer diff repaints just
 * the revealed rows; the subclass's tick() then redraws the table immediate-mode
 * and the diff pares it to the cells that actually moved.
 */

#include <cstdint>

#include "libbotanik/pages/page.h"
#include "libbotanik/console/terminal.h"

namespace Botanik { namespace Pages {

    /** @brief Scrollable, cursor-selectable table page base (see file header). */
    class ScrollTablePage : public Page {
    protected:
        using AnsiTerminal = Terminal::AnsiTerminal;

        /** @name State
         *  @{ */

        /** @brief The VT device the table paints through. */
        AnsiTerminal& _terminal;
        /** @brief First visible row: the scroll offset into the row set. */
        uint16_t      _top      = 0;
        /** @brief Selection cursor: index into the row set (drawn inverted). */
        uint16_t      _selected = 0;

        /** @} */

        explicit ScrollTablePage(AnsiTerminal& terminal) : _terminal(terminal) {}

        /** @name Subclass geometry
         *  @{ */

        /** @brief Screen row the data band starts at. */
        virtual uint16_t _data_top() const = 0;
        /** @brief Data rows that fit in the band (0 = screen too short). */
        virtual uint16_t _window() const = 0;
        /** @brief Rows in the set the cursor moves over. */
        virtual uint16_t _row_count() const = 0;

        /** @} */

        /** @name Scroll / selection
         *  @{ */

        /** @brief Cursor step for PAGE_UP / PAGE_DOWN: half a window (never less
         *  than one row), so a page jump keeps half the current view on screen for
         *  context instead of replacing it wholesale. */
        uint16_t _page_step() const {
            uint16_t win = _window();
            return (win > 2) ? (uint16_t)(win / 2) : 1;
        }

        /** @brief Largest scroll offset that still fills the window (0 = all fit). */
        uint16_t _max_top() const {
            uint16_t n = _row_count(), win = _window();
            return (n > win) ? (uint16_t)(n - win) : 0;
        }

        /** @brief Keep the cursor inside the (possibly reshaped) row set. */
        void _clamp_selection() {
            uint16_t n = _row_count();
            if (n == 0)              _selected = 0;
            else if (_selected >= n) _selected = (uint16_t)(n - 1);
        }

        /** @brief Scroll _top so the selection cursor stays in the window. */
        void _follow_selection() {
            uint16_t win = _window();
            if (win == 0) return;
            if (_selected < _top)             _top = _selected;
            else if (_selected >= _top + win) _top = (uint16_t)(_selected - win + 1);
            uint16_t maxtop = _max_top();
            if (_top > maxtop) _top = maxtop;
        }

        /** @brief Move the cursor by @p dir (clamped to the set), auto-scrolling
         *  the band to keep it visible. A shift smaller than the window rides a
         *  DL/IL so only the revealed rows repaint; a full-page jump keeps no
         *  overlap worth shifting and leaves the whole band to the tick's redraw. */
        void _move_selection(int dir) {
            uint16_t n = _row_count();
            if (n == 0) return;
            int32_t s = (int32_t)_selected + dir;
            if (s < 0)           s = 0;
            if (s >= (int32_t)n) s = (int32_t)n - 1;
            _selected = (uint16_t)s;
            uint16_t old_top = _top;
            _follow_selection();
            int32_t td = (int32_t)_top - (int32_t)old_top;
            if (td == 0) return;
            uint16_t win = _window();
            uint16_t nd  = (uint16_t)(td < 0 ? -td : td);
            if (nd < win) {
                uint16_t top = _data_top();
                _terminal.scroll_region(top, (uint16_t)(top + win - 1),
                                        /*up=*/td > 0, nd);
            }
        }

        /** @} */
    };

}}

#endif // PAGES_SCROLL_TABLE_H
