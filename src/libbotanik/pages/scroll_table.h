#ifndef PAGES_SCROLL_TABLE_H
#define PAGES_SCROLL_TABLE_H

/**
 * @file scroll_table.h
 * @brief ScrollTablePage - shared scroll-band and selection-cursor machinery for
 *        the bordered table pages (ALARMS / MONITOR / CONFIG).
 *
 * A subclass supplies only the band geometry and the row count; clamping,
 * follow-scroll and the chrome are shared. Each page keeps its own row
 * builder.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik/console/console.h"
#include "libbotanik/console/editor.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/console/page.h"

namespace Botanik { namespace Pages {

    /** @brief Scrollable, cursor-selectable table page base (see file header). */
    class ScrollTablePage : public Terminal::Page {
    protected:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief The console the table lives on: its content geometry, and the
         *  kill ring Alt+W copies into. */
        Console&      _console;
        /** @brief The VT device the table paints through (_console's). */
        AnsiTerminal& _terminal;
        /** @brief First visible row: the scroll offset into the row set. */
        uint16_t      _top      = 0;
        /** @brief Selection cursor: index into the row set (drawn inverted). */
        uint16_t      _selected = 0;

        /** @brief When the Alt+W badge stops being shown, in uptime ms. The mode
         *  line lends its detail slot: a copy is otherwise invisible. */
        uint64_t      _copied_until_ms = 0;
        /** @brief The badge text, naming what was copied. Sized for "copied "
         *  plus the longest key cell worth reading back. */
        char          _copied[56] = {0};

        /** @} */

        explicit ScrollTablePage(Console& console)
            : _console(console), _terminal(console.terminal()) {}

        /** @name Subclass geometry
         *  @{ */

        /** @brief Screen row the data band starts at. */
        virtual uint16_t _data_top() const = 0;
        /** @brief Data rows that fit in the band (0 = screen too short). */
        virtual uint16_t _window() const = 0;
        /** @brief Rows in the set the cursor moves over. */
        virtual uint16_t _row_count() const = 0;

        /** @brief The selected row's KEY CELL for the Alt+W copy, or null when
         *  this table has nothing worth copying. Taken from the MODEL, not the
         *  rendered line, which is padded and column-truncated. */
        virtual const char* _selected_cell() const { return nullptr; }

        /** @} */

        /** @name Scroll / selection
         *  @{ */

        /** @brief Cursor step for PAGE_UP / PAGE_DOWN: half a window (never less
         *  than one row), so a page jump keeps half the view for context. */
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

        /** @brief Whether moving the window announces a band scroll to the
         *  terminal. A move the page repaints behind does not: the repaint
         *  writes every row the scroll would have shifted. */
        enum class ScrollBand : uint8_t { EMIT, SKIP };

        /** @brief Move the window to @p new_top, announcing the band scroll
         *  when @p band says to. Every assignment to _top goes through here,
         *  so each site states which it is. */
        void _set_top(uint16_t new_top, ScrollBand band);

        /** @brief Scroll _top so the selection cursor stays in the window. */
        void _follow_selection();

        /** @brief Handle the standard table nav keys (UP/DOWN, CTRL_P/N,
         *  PAGE_UP/DOWN, HOME/END), and Alt+W over _selected_cell().
         *  @return true if @p cmd was one, so the caller marks itself dirty and
         *  stops; false leaves it to try its own keys. */
        bool _nav_command(Command cmd);

        /** @brief How long the Alt+W badge stays on the mode line (ms). */
        static constexpr uint32_t COPIED_BADGE_MS = 3000;

        /** @brief Put the selected cell in the kill ring (Alt+W), so it can be
         *  yanked into the shell prompt or the PARAMS value field. */
        void _copy_selected_cell();

        /** @brief The Alt+W badge, or "" once it has expired. What
         *  _draw_legend() shows in place of the page's own detail. */
        const char* _copied_badge() const;

        /** @brief Move the cursor by @p dir (clamped to the set), scrolling the
         *  band to keep it visible; the tick's redraw paints it. */
        void _move_selection(int dir);

        /** @}
         *  @name View modes
         *
         *  An ordered set of named views, one active. RIGHT steps forward,
         *  LEFT back - not a typed key, which is the filter's. @{ */

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
         *  @warning Does NOT blank the band first. Clearing would queue a band
         *  scroll, which present() emits before the diff and outside its pacing:
         *  320 bytes in the first frame against 106. Pinned by
         *  test_alarms.cpp. */
        void _cycle_mode(int dir);

        /** @} */

        /** @name Legend layout
         *  Fixed columns; the three view fields are CENTRED in equal slots of
         *  ML_SLOT_W = 11, one past the widest field any table produces
         *  ("[unmasked]", 10). An overrunning field pushes the ones after it
         *  right rather than clipping. @see dev_pages
         *  @{ */
        static constexpr uint16_t ML_SLOT_W = 11;   // each view field's slot
        static constexpr uint16_t ML_PREV   = 1;    // previous view
        static constexpr uint16_t ML_BADGE  = 12;   // "[cur]"
        static constexpr uint16_t ML_NEXT   = 23;   // "> next"
        static constexpr uint16_t ML_POS    = 35;   // "first-last / total"
        static constexpr uint16_t ML_FILTER = 48;   // "[/text]", while one is set
        static constexpr uint16_t ML_DETAIL = 60;   // page-specific extra
        /** @} */

        /** @name Drawing the legend
         *  The column constants above, applied: padding, centring in a slot,
         *  and the row itself. @{ */

        /** @brief Pad @p buf out to column @p col. */
        static void _pad_to(char* buf, uint16_t& w, size_t cap, uint16_t col) {
            while (w < col && (size_t)(w + 1) < cap) buf[w++] = ' ';
        }

        /** @brief Pad to column @p col, or to one space past the text already
         *  there - a field that overran its slot pushes the next one along
         *  rather than running into it. */
        static void _pad_gap(char* buf, uint16_t& w, size_t cap, uint16_t col) {
            _pad_to(buf, w, cap, col);
            if (w >= col && (size_t)(w + 1) < cap && w > 0 && buf[w - 1] != ' ')
                buf[w++] = ' ';
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

        /** @brief Draw the table's legend line: its views (when it has more
         *  than one), how much of the set is on screen, the live filter, and
         *  @p detail - null for the standing hint.
         *  @note ONE pass with the reverse-video fields split out;
         *  normal-then-inverted would flicker. */
        void _draw_legend(uint16_t row, const char* detail);

        /** @brief Declare @p row as this table's legend row: PRIO_CHROME, so
         *  present() puts it on the wire ahead of the table rows.
         *  @note From focus(), not around a draw: a priority is a standing
         *  property, and the Console resets the band on every page switch. */
        void _claim_legend_row(uint16_t row) {
            _terminal.set_row_priority(row, Terminal::PRIO_CHROME);
        }

        /** @brief Handle the mode nav keys. @return true if @p cmd was one. */
        bool _mode_command(Command cmd);

        /** @}
         *  @name Row filter
         *  A live pattern the table narrows to: typing appends, BACKSPACE
         *  pops, ESC clears. `*` and `?` are wildcards; without one the
         *  pattern is a substring. A page opts in through _filter_text() /
         *  _filter_command() and _filter_match(). @{ */

        /** @brief The filter text, NUL-terminated; empty matches every row. */
        char    _filter[Config::TABLE_FILTER_LEN + 1] = {0};
        uint8_t _filter_n = 0;
        /** @brief Bumped on every change, so a page's row cache can hold WHICH
         *  filter it counted rather than a flag something could forget to
         *  clear. Never 0: that is every cache's "never built" value, and this
         *  wraps. */
        uint16_t _filter_gen = 1;

        /** @brief Called after the filter changed, before the redraw: the page
         *  re-collects whatever the new filter shows. */
        virtual void _on_filter_changed() {}

        /** @brief Whether @p s passes the filter (Utils::glob_ci(), so ASCII
         *  case-insensitive). A null @p s matches only the empty filter. */
        bool _filter_match(const char* s) const;

        /** @brief Append a typed character. @return true if @p c was one this
         *  filter takes, so the caller stops.
         *  @note A SPACE on an EMPTY filter is dropped: no name holds one. */
        bool _filter_text(char c);

        /** @brief Handle the filter's editing keys (BACKSPACE, ESC).
         *  @return true if @p cmd was one. */
        bool _filter_command(Command cmd);

        /** @brief Reset the band to the top of the narrowed set, bump the
         *  generation and tell the page. */
        void _filter_changed();

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
         *  A subclass declares its columns once (heading + content width); the
         *  base fits them to the display and derives the row, the header, the
         *  rule and each cell's start column from that. @{ */

        /** @brief One table column: its heading and its CONTENT width (the
         *  cell's border and padding are the base's business). */
        struct Col {
            const char* head;
            uint16_t    width;
        };

        static constexpr uint8_t  MAX_COLS = 6;
        /** @brief Blank columns each side of the table. */
        static constexpr uint16_t MARGIN   = 2;

        /** @brief Narrowest a column may be shaved to on a short display. */
        static constexpr uint16_t MIN_COL_W  = 3;
        /** @brief Narrowest the flexible column is stretched to. */
        static constexpr uint16_t FLEX_MIN   = 8;

        /** @brief Fill @p out with this table's columns and return how many.
         *  The flexible one's width is the base's to fill: declare it 0. */
        virtual uint8_t _columns(Col* out) const = 0;

        /** @brief Which column absorbs the display's slack - and gives it back
         *  when the screen is too narrow for the fixed ones. */
        virtual uint8_t _flex_col() const { return 0; }

        /** @brief The column layout solved for the display as it is now. A
         *  solve, not a lookup: whoever needs two cell positions off one row
         *  takes one of these and reads both out of it. */
        struct Fit {
            Col     col[MAX_COLS];
            uint8_t n;
        };

        /** @brief Solve the layout: _columns() fitted to the display, the
         *  flexible column taking whatever the fixed ones leave, and a row
         *  still too wide shaved widest-cell-first until it fits. The one
         *  place the table's width is decided, so it ends where
         *  _draw_separator() does. */
        Fit _fit_columns() const;

        /** @brief Whether a cell carries a pad column after its contents, so a
         *  row reads "| a | b |" rather than "| a| b". The alarms grid packs its
         *  cells (the width goes to its flexible column); the others pad. It
         *  sets the cell pitch every derived column position falls out of. */
        virtual bool _pad_cells() const { return true; }

        /** @brief Columns one cell occupies on screen: "| " + contents + any
         *  trailing pad. */
        uint16_t _cell_pitch(uint16_t width) const {
            return (uint16_t)(width + (_pad_cells() ? 3 : 2));
        }

        /** @brief Column @p i's contents on screen under @p fit:
         *  [@p start, @p end). */
        void _cell_span(const Fit& fit, uint8_t i,
                        uint16_t& start, uint16_t& end) const;

        /** @brief Build "  | a | b | c |" into @p out from @p cells (one per
         *  column of @p fit; a null cell renders empty). @return its length. */
        uint16_t _build_row(const Fit& fit, char* out, size_t cap,
                            const char* const* cells) const;

        /** @brief Draw the heading row from the column spec. */
        void _draw_header(uint16_t row);

        /** @brief Draw the "+----+--+...+" rule under/over the table. @p cap is
         *  the corner char: '+' plain, or '^' / 'v' to mark rows hidden above /
         *  below the window. @p join is the inner column mark - the alarms grid
         *  passes @p cap for it too, so every mark carries the scroll arrow. */
        void _draw_rule(uint16_t row, char cap, char join = '+');

        /** @brief A full-width dashed separator, for a page that puts a detail
         *  panel under its table. */
        void _draw_separator(uint16_t row);

        /** @brief " Label        : value" panel line (clear_to_eol wipes the old
         *  value's tail when the selection moves to a shorter one). The label
         *  field is 12 wide, the longest label any panel uses ("Acknowledged");
         *  a longer one shoves its colon out of the column the panels share. */
        void _panel_field(uint16_t row, const char* label, const char* value);

        /** @brief Pad @p buf out to column @p col, then append
         *  "label<pad to lw> : value". Returns the new width. Shared by every
         *  multi-field panel row, so they all lay out the same way. */
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
         *  side by side (a min/max band): each row a panel spends is a row the
         *  table loses. */
        void _panel_field2(uint16_t row, const char* l1, const char* v1,
                           uint16_t col2, const char* l2, const char* v2) {
            char buf[Config::TABLE_LINE_LEN];
            uint16_t w = _panel_cell(buf, sizeof(buf), 0, 1, 12, l1, v1);
            w = _panel_cell(buf, sizeof(buf), w, col2, 12, l2, v2);
            _draw_line(row, buf, w);
        }

        /** @brief Three panel fields on one row. The third gets an 8-wide label
         *  field: two 12-wide labels and their values already reach column 60,
         *  so it has to fit in what is left rather than push the first two out
         *  of the column the other panels share. */
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
