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
 * The bordered-table chrome is shared too: a full-line draw, an N-column dash
 * rule, a selection-cell inversion, a flexible-column width and the detail-panel
 * line all three tables use. Each page keeps its own row builder (their column
 * sets differ).
 *
 * Moving the cursor past a window edge scrolls the band through
 * AnsiTerminal::scroll_region(), so the terminal's back-buffer diff repaints just
 * the revealed rows; the subclass's tick() then redraws the table immediate-mode
 * and the diff pares it to the cells that actually moved.
 */

#include <cstdint>
#include <cstdio>

#include "page.h"
#include "../console/terminal.h"

namespace Botanik { namespace Pages {

    /** @brief Scrollable, cursor-selectable table page base (see file header). */
    class ScrollTablePage : public Page {
    protected:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Command      = Terminal::InputEvent::Command;

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

        /** @brief Handle the standard table nav keys (UP/DOWN, CTRL_P/N,
         *  PAGE_UP/DOWN, HOME/END). @return true if @p cmd was a nav key, so the
         *  caller can set its dirty flag and stop; false leaves the caller to try
         *  its own keys (ENTER = edit / jump, etc.). */
        bool _nav_command(Command cmd) {
            switch (cmd) {
                case Command::UP:   case Command::CTRL_P: _move_selection(-1); return true;
                case Command::DOWN: case Command::CTRL_N: _move_selection(+1); return true;
                case Command::PAGE_UP:   _move_selection(-(int)_page_step()); return true;
                case Command::PAGE_DOWN: _move_selection(+(int)_page_step()); return true;
                case Command::HOME: _move_selection(-(int)_row_count()); return true;
                case Command::END:  _move_selection(+(int)_row_count()); return true;
                default: return false;
            }
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

        /** @}
         *  @name Bordered-table rendering
         *  @{ */

        /** @brief Blank a whole row (normal video). */
        void _blank_line(uint16_t row) { _draw_line(row, "", 0); }

        /** @brief Draw a full line at @p row in normal video, then clear the rest
         *  of the row. The plain (unselected) row / header / rule painter. */
        void _draw_line(uint16_t row, const char* line, uint16_t n) {
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, n);
            _terminal.clear_to_eol();
        }

        /** @brief Draw a prebuilt line, inverting screen columns [@p cell_l,
         *  @p cell_r) when @p selected - the selection cursor. Clamps to @p n, so
         *  a short line never over-reads. */
        void _draw_selected_line(uint16_t row, const char* line, uint16_t n,
                                 uint16_t cell_l, uint16_t cell_r, bool selected) {
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            if (selected && n > cell_l) {
                _terminal.write_chars(line, cell_l);
                _terminal.set_inverted(true);
                uint16_t e = (n < cell_r) ? n : cell_r;
                _terminal.write_chars(line + cell_l, (uint16_t)(e - cell_l));
                _terminal.set_inverted(false);
                if (n > cell_r)
                    _terminal.write_chars(line + cell_r, (uint16_t)(n - cell_r));
            } else {
                _terminal.write_chars(line, n);
            }
            _terminal.clear_to_eol();
        }

        /** @}
         *  @name Column model
         *
         *  A subclass declares its columns once (heading + content width, flex
         *  already resolved) and the base builds the row, the header and the
         *  rule from that, and derives the screen column each cell's contents
         *  start at. Before this, each page carried its own snprintf row
         *  builder plus hand-computed highlight columns that had to be kept in
         *  step with it by eye.
         *  @{ */

        /** @brief One table column: its heading and its CONTENT width (the
         *  cell's border and padding are the base's business). */
        struct Col {
            const char* head;
            uint16_t    width;
        };

        static constexpr uint8_t  MAX_COLS = 6;
        /** @brief Blank columns each side of the table. */
        static constexpr uint16_t MARGIN   = 2;

        /** @brief Fill @p out with this table's columns (flex widths resolved
         *  against the current display) and return how many. */
        virtual uint8_t _columns(Col* out) const = 0;

        /** @brief Whether a cell carries a pad column after its contents, so a
         *  row reads "| a | b |" rather than "| a| b". The alarms grid packs its
         *  cells (it spends the width on the description instead); the other two
         *  pad. A per-table property, not a preference - it sets the cell pitch
         *  every derived column position falls out of. */
        virtual bool _pad_cells() const { return true; }

        /** @brief Columns one cell occupies on screen: "| " + contents + any
         *  trailing pad. */
        uint16_t _cell_pitch(uint16_t width) const {
            return (uint16_t)(width + (_pad_cells() ? 3 : 2));
        }

        /** @brief Screen column where column @p i's CONTENTS start - what a
         *  selection cursor or an in-cell badge inverts from. Derived from the
         *  same spec that builds the row, so the two cannot drift. */
        uint16_t _cell_col(uint8_t i) const {
            Col cols[MAX_COLS];
            uint8_t n = _columns(cols);
            uint16_t c = (uint16_t)(MARGIN + 2);          // margin + "| "
            for (uint8_t j = 0; j < i && j < n; j++) c = (uint16_t)(c + _cell_pitch(cols[j].width));
            return c;
        }

        /** @brief One past the last column of column @p i's contents. */
        uint16_t _cell_end(uint8_t i) const {
            Col cols[MAX_COLS];
            uint8_t n = _columns(cols);
            return (uint16_t)(_cell_col(i) + (i < n ? cols[i].width : 0));
        }

        /** @brief Build "  | a | b | c |" into @p out from @p cells (one per
         *  declared column; a null cell renders empty). @return its length. */
        uint16_t _build_row(char* out, size_t cap, const char* const* cells) const {
            Col cols[MAX_COLS];
            uint8_t n = _columns(cols);
            const bool pad = _pad_cells();
            uint16_t w = 0;
            for (uint16_t i = 0; i < MARGIN && w < cap - 1; i++) out[w++] = ' ';
            for (uint8_t c = 0; c < n; c++) {
                if (w < cap - 1) out[w++] = '|';
                const int cw = (int)cols[c].width;
                int m = snprintf(out + w, cap - w, pad ? " %-*.*s " : " %-*.*s",
                                 cw, cw, cells[c] != nullptr ? cells[c] : "");
                if (m < 0) m = 0;
                w = (uint16_t)(w + m);
                if (w > cap - 1) { w = (uint16_t)(cap - 1); break; }
            }
            if (w < cap - 1) out[w++] = '|';               // closing rule
            return w;
        }

        /** @brief Draw the heading row from the column spec. */
        void _draw_header(uint16_t row) {
            Col cols[MAX_COLS];
            uint8_t n = _columns(cols);
            const char* heads[MAX_COLS];
            for (uint8_t i = 0; i < n; i++) heads[i] = cols[i].head;
            char line[192];
            _draw_line(row, line, _build_row(line, sizeof(line), heads));
        }

        /** @brief Draw the "+----+--+...+" rule under/over the table. @p cap is
         *  the corner char: '+' plain, or '^' / 'v' to mark rows hidden above /
         *  below the window. @p join is the inner column mark - the alarms grid
         *  passes @p cap for it too, so every mark carries the scroll arrow. */
        void _draw_rule(uint16_t row, char cap, char join = '+') {
            Col cols[MAX_COLS];
            uint8_t n = _columns(cols);
            char line[192];
            uint16_t w = 0;
            for (uint16_t i = 0; i < MARGIN && w < sizeof(line) - 1; i++) line[w++] = ' ';
            if (w < sizeof(line) - 1) line[w++] = cap;
            for (uint8_t c = 0; c < n; c++) {
                // The rule spans the whole cell: its contents plus the padding
                // the row builder puts either side.
                uint16_t span = (uint16_t)(_cell_pitch(cols[c].width) - 1);
                for (uint16_t k = 0; k < span && w < sizeof(line) - 1; k++) line[w++] = '-';
                if (w < sizeof(line) - 1) line[w++] = (c + 1 == n) ? cap : join;
            }
            _draw_line(row, line, w);
        }

        /** @brief Flexible column width: the display minus @p used, floored at
         *  8 and capped at @p cap. */
        uint16_t _flex_width(uint16_t used, uint16_t cap) const {
            uint16_t cols = _terminal.cols();
            uint16_t w = (cols > used) ? (uint16_t)(cols - used) : 8;
            if (w < 8)   w = 8;
            if (w > cap) w = cap;
            return w;
        }

        /** @brief " Label        : value" panel line (clear_to_eol wipes the old
         *  value's tail when the selection moves to a shorter one). The label
         *  field is 12 wide because that is the longest label any panel uses
         *  ("Acknowledged"); a longer one would shove its own colon right and
         *  break the column the panels share. */
        void _panel_field(uint16_t row, const char* label, const char* value) {
            char buf[192];
            int n = snprintf(buf, sizeof(buf), " %-12s : %s", label,
                             (value && value[0]) ? value : "-");
            _draw_line(row, buf, (n < 0) ? 0 : (uint16_t)n);
        }

        /** @} */
    };

}}

#endif // PAGES_SCROLL_TABLE_H
