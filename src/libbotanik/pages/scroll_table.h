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
#include <cstring>

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
         *  @name View modes
         *
         *  A table usually has more than one useful view of its rows: the alarms
         *  filter (recent / all / active / unmasked), the monitor's variable
         *  groups (all / power / environment / ...). Both are the same thing -
         *  a small ordered set of named views, one active at a time - so it lives
         *  here rather than being re-invented per page.
         *
         *  Keys: SPACE and RIGHT step forward, LEFT steps back. NOT Shift+SPACE:
         *  a terminal sends 0x20 for both Space and Shift+Space, so the two are
         *  indistinguishable on the wire and a "shift to go back" binding would
         *  simply never fire. LEFT/RIGHT are free (the row cursor uses UP/DOWN)
         *  and read as "move the view" against "move within it".
         *  @{ */

        /** @brief Active view. Always < _mode_count() (or 0 when there are none). */
        uint8_t _mode = 0;

        /** @brief How many views this table has; 0 (the default) means the page
         *  has none and the mode keys fall through to it. */
        virtual uint8_t _mode_count() const { return 0; }

        /** @brief Short label for view @p i, for the footer badge. */
        virtual const char* _mode_label(uint8_t /*i*/) const { return ""; }

        /** @brief Called after the view changed, before the redraw: the page
         *  re-collects whatever the new view shows. */
        virtual void _on_mode_changed() {}

        /** @brief Label of the active view / the one SPACE would move to. */
        const char* _mode_name() const { return _mode_label(_mode); }
        const char* _next_mode_name() const {
            uint8_t n = _mode_count();
            return n ? _mode_label((uint8_t)((_mode + 1) % n)) : "";
        }

        /** @brief Step the view by @p dir, wrapping, and reset the cursor to the
         *  top of the new set.
         *
         *  Deliberately does NOT blank the band first. The badge already reaches
         *  the wire ahead of the rows, because the footer row is declared
         *  PRIO_CHROME and present() walks rows by priority class - so one tick
         *  after the keypress the new mode is on screen and the table follows
         *  under the byte budget. Clearing would instead queue a band scroll,
         *  which present() emits BEFORE the diff and outside that pacing: the
         *  first frame then carries a table's worth of bytes (measured 320 vs
         *  106) and data rows land with the badge rather than after it. The
         *  keypress feels acknowledged because of the priority, not in spite of
         *  the rows. See the "mode change shows the new mode before the rows"
         *  case in test_alarms_render.cpp, which pins exactly this. */
        void _cycle_mode(int dir) {
            uint8_t n = _mode_count();
            if (n <= 1) return;
            _mode     = (uint8_t)(((int)_mode + dir + (int)n) % (int)n);
            _top      = 0;
            _selected = 0;
            _on_mode_changed();
        }

        /** @name Mode-line layout
         *  Fixed columns, so the four fields sit in the same place on every
         *  table and on every redraw. Free-form padding drifted the moment a
         *  label changed length, which made the eye re-find the position counter
         *  each time a group was cycled.
         *  @{ */
        static constexpr uint16_t ML_BADGE  = 2;    // "[mode]"
        static constexpr uint16_t ML_GROUP  = 14;   // "(group)", when shown
        static constexpr uint16_t ML_POS    = 24;   // "first-last / total"
        static constexpr uint16_t ML_DETAIL = 40;   // page-specific extra
        /** @} */

        /** @brief Pad @p buf out to column @p col. */
        static void _pad_to(char* buf, uint16_t& w, size_t cap, uint16_t col) {
            while (w < col && w + 1 < cap) buf[w++] = ' ';
        }

        /** @brief Draw the view's legend line: a reverse-video badge naming the
         *  active view, the selected row's @p group in parentheses when the page
         *  passes one, how much of the set is on screen, @p detail, and @p hint
         *  right-aligned.
         *
         *  Every table gets the same line because every table now has views, and
         *  the badge is the only thing that tells an operator the keypress
         *  landed - the rows themselves arrive over the following ticks under the
         *  byte budget. Painted in ONE pass with the badge split out rather than
         *  drawn normal-then-inverted, which would flicker.
         *
         *  The position counter is computed here rather than passed in: _top,
         *  _window() and _row_count() are the base's own state, so a page could
         *  only have got it wrong.
         *
         *  Pair it with _claim_mode_row(): the line is only fast because its row
         *  outranks the table. */
        void _draw_mode_line(uint16_t row, const char* group, const char* detail,
                             const char* hint) {
            char left[160];
            uint16_t w = 0;

            _pad_to(left, w, sizeof(left), ML_BADGE);
            const uint16_t badge_at = w;
            int n = snprintf(left + w, sizeof(left) - w, "[%s]", _mode_name());
            if (n > 0) w = (uint16_t)(w + n);
            const uint16_t badge_end = w;

            if (group != nullptr && group[0] != '\0') {
                _pad_to(left, w, sizeof(left), ML_GROUP);
                n = snprintf(left + w, sizeof(left) - w, "(%s)", group);
                if (n > 0) w = (uint16_t)(w + n);
            }

            // How much of the set is on screen - every table shows it, since
            // "am I looking at all of this?" is the same question everywhere.
            _pad_to(left, w, sizeof(left), ML_POS);
            const uint16_t total = _row_count(), win = _window();
            const uint16_t last  = (uint16_t)(_top + (win < total ? win : total));
            n = snprintf(left + w, sizeof(left) - w, "%u-%u / %u",
                         (unsigned)(total ? _top + 1 : 0), (unsigned)last,
                         (unsigned)total);
            if (n > 0) w = (uint16_t)(w + n);

            if (detail != nullptr && detail[0] != '\0') {
                _pad_to(left, w, sizeof(left), ML_DETAIL);
                n = snprintf(left + w, sizeof(left) - w, "%s", detail);
                if (n > 0) w = (uint16_t)(w + n);
            }
            if (w > sizeof(left) - 1) w = (uint16_t)(sizeof(left) - 1);

            // One pass: plain up to the badge, inverted through it, plain after.
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(left, badge_at);
            _terminal.set_inverted(true);
            _terminal.write_chars(left + badge_at, (uint16_t)(badge_end - badge_at));
            _terminal.set_inverted(false);
            _terminal.write_chars(left + badge_end, (uint16_t)(w - badge_end));
            _terminal.clear_to_eol();

            if (hint == nullptr) return;
            uint16_t hn   = (uint16_t)strlen(hint);
            uint16_t cols = _terminal.cols();
            uint16_t hcol = (cols > (uint16_t)(hn + 2)) ? (uint16_t)(cols - hn - 2) : 0;
            if (hcol > (uint16_t)(w + 1)) {          // only if it clears the left
                _terminal.move_to({row, hcol});
                _terminal.write_chars(hint, hn);
            }
        }

        /** @brief Declare @p row as this table's mode line: PRIO_CHROME, so
         *  present() puts it on the wire ahead of the table rows and the new
         *  badge shows up the tick after the keypress.
         *
         *  Call it from focus(), not around a draw: the Console hands the content
         *  band back as plain structure on every page switch, so the claim has to
         *  be re-made, and it is a standing property of the row rather than
         *  something raised for one paint. */
        void _claim_mode_row(uint16_t row) {
            _terminal.set_row_priority(row, Terminal::PRIO_CHROME);
        }

        /** @brief Handle the mode nav keys. @return true if @p cmd was one. */
        bool _mode_command(Command cmd) {
            if (_mode_count() <= 1) return false;
            if (cmd == Command::RIGHT) { _cycle_mode(+1); return true; }
            if (cmd == Command::LEFT)  { _cycle_mode(-1); return true; }
            // Getting back to the whole set should not mean cycling through
            // every group to reach it.
            if (cmd == Command::BACKSPACE) {
                if (_mode != 0) _cycle_mode(-(int)_mode);
                return true;
            }
            return false;
        }

        /** @brief SPACE steps forward (it is a printable byte, so it arrives
         *  through handle_text rather than as a command). */
        bool _mode_text(char c) {
            if (c != ' ' || _mode_count() <= 1) return false;
            _cycle_mode(+1);
            return true;
        }

        /** @brief Last screen row the page may use. A table reserves rows below
         *  its band for a panel/footer, so the subclass says where the page
         *  ends; the default is the terminal's own last row. */
        virtual uint16_t _console_rows() const { return _terminal.rows_total(); }

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

        /** @brief Two panel fields on one row, for a pair that reads naturally
         *  side by side (a min/max band). Each row a panel spends is a row the
         *  table loses, so a pair that is really one fact should cost one. */
        void _panel_field2(uint16_t row, const char* l1, const char* v1,
                           uint16_t col2, const char* l2, const char* v2) {
            char buf[192];
            int n = snprintf(buf, sizeof(buf), " %-12s : %s", l1,
                             (v1 && v1[0]) ? v1 : "-");
            if (n < 0) n = 0;
            uint16_t w = (uint16_t)((size_t)n < sizeof(buf) - 1 ? (size_t)n
                                                                : sizeof(buf) - 1);
            while (w < col2 && w < sizeof(buf) - 1) buf[w++] = ' ';   // pad to col2
            int m = snprintf(buf + w, sizeof(buf) - w, "%-12s : %s", l2,
                             (v2 && v2[0]) ? v2 : "-");
            if (m > 0) w = (uint16_t)(w + m);
            if (w > sizeof(buf) - 1) w = (uint16_t)(sizeof(buf) - 1);
            _draw_line(row, buf, w);
        }

        /** @} */
    };

}}

#endif // PAGES_SCROLL_TABLE_H
