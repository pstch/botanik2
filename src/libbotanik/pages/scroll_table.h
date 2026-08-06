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

#include <cstdint>                          // uint8_t/uint16_t/int32_t
#include <cstdio>                           // snprintf
#include <cstring>                          // strlen

#include "libbotanik/console/terminal.h"    // AnsiTerminal
#include "libbotanik/pages/page.h"          // Pages::Page

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
        void _clamp_selection();

        /** @brief Scroll _top so the selection cursor stays in the window. */
        void _follow_selection();

        /** @brief Handle the standard table nav keys (UP/DOWN, CTRL_P/N,
         *  PAGE_UP/DOWN, HOME/END). @return true if @p cmd was a nav key, so the
         *  caller can set its dirty flag and stop; false leaves the caller to try
         *  its own keys (ENTER = edit / jump, etc.). */
        bool _nav_command(Command cmd);

        /** @brief Move the cursor by @p dir (clamped to the set), auto-scrolling
         *  the band to keep it visible. A shift smaller than the window rides a
         *  DL/IL so only the revealed rows repaint; a full-page jump keeps no
         *  overlap worth shifting and leaves the whole band to the tick's redraw. */
        void _move_selection(int dir);

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

        /** @brief Label of the active view. */
        const char* _mode_name() const { return _mode_label(_mode); }

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
        void _cycle_mode(int dir);

        /** @name Mode-line layout
         *  Fixed columns, so the four fields sit in the same place on every
         *  table and on every redraw. Free-form padding drifted the moment a
         *  label changed length, which made the eye re-find the position counter
         *  each time a group was cycled.
         *
         *  The neighbours sit either side of the badge - "prev < [cur] > next" -
         *  rather than as a hint off on the right. The keys move you along that
         *  row, so the row is the thing to look at, and it says where you are and
         *  what is on each side in one glance.
         *
         *  The three view fields are CENTRED in equal slots. Their labels are all
         *  different lengths, so aligning them to an edge left the trio looking
         *  lopsided and moved the badge - the one thing the eye returns to -
         *  whenever a neighbour's name changed length. Centring in a fixed slot
         *  keeps each field in its own region and lets it grow both ways.
         *  @{ */
        static constexpr uint16_t ML_SLOT_W = 15;   // each view field's slot
        static constexpr uint16_t ML_PREV   = 2;    // previous view
        static constexpr uint16_t ML_BADGE  = 17;   // "[cur]"
        static constexpr uint16_t ML_NEXT   = 32;   // "> next", or "(group)"
        static constexpr uint16_t ML_POS    = 48;   // "first-last / total"
        static constexpr uint16_t ML_DETAIL = 62;   // page-specific extra
        /** @} */

        /** @brief Pad @p buf out to column @p col. */
        static void _pad_to(char* buf, uint16_t& w, size_t cap, uint16_t col) {
            while (w < col && (size_t)(w + 1) < cap) buf[w++] = ' ';
        }

        /** @brief Write @p text centred in the @p slot_w-wide slot at @p col.
         *  @return the column its first character landed on (the badge needs it
         *  to know what to invert). */
        static uint16_t _place_centered(char* buf, uint16_t& w, size_t cap,
                                        uint16_t col, uint16_t slot_w,
                                        const char* text) {
            const uint16_t len = (uint16_t)strlen(text);
            const uint16_t off = (len < slot_w) ? (uint16_t)((slot_w - len) / 2) : 0;
            _pad_to(buf, w, cap, (uint16_t)(col + off));
            const uint16_t at = w;
            int n = snprintf(buf + w, cap - w, "%s", text);
            if (n > 0) w = (uint16_t)(w + n);
            return at;
        }

        /** @brief Draw the view's legend line: the neighbouring views either side
         *  of a reverse-video badge naming the active one, how much of the set is
         *  on screen, and @p detail.
         *
         *  When the page passes a @p group - which it does only in an "all" view,
         *  where the badge cannot say which group the cursor is in - that takes
         *  the place of the forward neighbour: in "all" the useful thing to show
         *  is where you ARE, not where SPACE would take you.
         *
         *  The badge is the only thing that tells an operator the keypress
         *  landed, since the rows themselves arrive over the following ticks
         *  under the byte budget. Painted in ONE pass with the badge split out
         *  rather than drawn normal-then-inverted, which would flicker.
         *
         *  The position counter is computed here rather than passed in: _top,
         *  _window() and _row_count() are the base's own state, so a page could
         *  only have got it wrong.
         *
         *  Pair it with _claim_mode_row(): the line is only fast because its row
         *  outranks the table. */
        void _draw_mode_line(uint16_t row, const char* group, const char* detail);

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
        bool _mode_command(Command cmd);

        /** @brief SPACE steps forward (it is a printable byte, so it arrives
         *  through handle_text rather than as a command). */
        bool _mode_text(char c);

        /** @}
         *  @name Bordered-table rendering
         *  @{ */

        /** @brief Blank a whole row (normal video). */
        void _blank_line(uint16_t row) { _draw_line(row, "", 0); }

        /** @brief Draw a full line at @p row in normal video, then clear the rest
         *  of the row. The plain (unselected) row / header / rule painter. */
        void _draw_line(uint16_t row, const char* line, uint16_t n);

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
        uint16_t _cell_col(uint8_t i) const;

        /** @brief One past the last column of column @p i's contents. */
        uint16_t _cell_end(uint8_t i) const;

        /** @brief Build "  | a | b | c |" into @p out from @p cells (one per
         *  declared column; a null cell renders empty). @return its length. */
        uint16_t _build_row(char* out, size_t cap, const char* const* cells) const;

        /** @brief Draw the heading row from the column spec. */
        void _draw_header(uint16_t row);

        /** @brief Draw the "+----+--+...+" rule under/over the table. @p cap is
         *  the corner char: '+' plain, or '^' / 'v' to mark rows hidden above /
         *  below the window. @p join is the inner column mark - the alarms grid
         *  passes @p cap for it too, so every mark carries the scroll arrow. */
        void _draw_rule(uint16_t row, char cap, char join = '+');

        /** @brief Flexible column width: the display minus @p used, floored at
         *  8 and capped at @p cap. */
        uint16_t _flex_width(uint16_t used, uint16_t cap) const;

        /** @brief A full-width dashed separator, for a page that puts a detail
         *  panel under its table. Chrome, so it lives here beside _draw_rule()
         *  and _panel_field() rather than being re-derived per page. */
        void _draw_separator(uint16_t row);

        /** @brief " Label        : value" panel line (clear_to_eol wipes the old
         *  value's tail when the selection moves to a shorter one). The label
         *  field is 12 wide because that is the longest label any panel uses
         *  ("Acknowledged"); a longer one would shove its own colon right and
         *  break the column the panels share. */
        void _panel_field(uint16_t row, const char* label, const char* value);

        /** @brief Pad @p buf out to column @p col, then append
         *  "label<pad to lw> : value". Returns the new width.
         *
         *  The one piece of column arithmetic the multi-field panel rows share,
         *  so a row with two fields and a row with three lay their columns out
         *  the same way rather than each getting it slightly wrong. */
        static uint16_t _panel_cell(char* buf, size_t cap, uint16_t w,
                                    uint16_t col, uint8_t lw,
                                    const char* label, const char* value) {
            while (w < col && w < cap - 1) buf[w++] = ' ';
            int m = snprintf(buf + w, cap - w, "%-*s : %s", (int)lw, label,
                             (value && value[0]) ? value : "-");
            if (m > 0) w = (uint16_t)(w + m);
            if (w > cap - 1) w = (uint16_t)(cap - 1);
            return w;
        }

        /** @brief Two panel fields on one row, for a pair that reads naturally
         *  side by side (a min/max band). Each row a panel spends is a row the
         *  table loses, so a pair that is really one fact should cost one. */
        void _panel_field2(uint16_t row, const char* l1, const char* v1,
                           uint16_t col2, const char* l2, const char* v2) {
            char buf[Config::TABLE_LINE_LEN];
            uint16_t w = _panel_cell(buf, sizeof(buf), 0, 1, 12, l1, v1);
            w = _panel_cell(buf, sizeof(buf), w, col2, 12, l2, v2);
            _draw_line(row, buf, w);
        }

        /** @brief Three panel fields on one row. The third gets a narrower label
         *  field: two 12-wide labels and their values already reach column 60,
         *  and the point of putting a third fact here rather than on its own row
         *  is that the row is free - so it has to fit in what is left, not push
         *  the first two out of the column the other panels share. */
        void _panel_field3(uint16_t row, const char* l1, const char* v1,
                           uint16_t col2, const char* l2, const char* v2,
                           uint16_t col3, const char* l3, const char* v3) {
            char buf[Config::TABLE_LINE_LEN];
            uint16_t w = _panel_cell(buf, sizeof(buf), 0, 1, 12, l1, v1);
            w = _panel_cell(buf, sizeof(buf), w, col2, 12, l2, v2);
            w = _panel_cell(buf, sizeof(buf), w, col3,  8, l3, v3);
            _draw_line(row, buf, w);
        }

        /** @} */
    };

}}

#endif // PAGES_SCROLL_TABLE_H
