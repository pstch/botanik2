// REVIEW_35: TODO
#ifndef PAGES_MONITOR_H
#define PAGES_MONITOR_H

/**
 * @file monitor.h
 * @brief MonitorPage - scrollable table of the site's status variables.
 *
 * The read-only sibling of the PARAMS page: every variable except the two kinds
 * that have a page of their own (parameters, alarms), live off the variable
 * table, one row per variable, grouped by the prefix of its name. Nothing is
 * declared per site - naming a variable is what files it - so the page has no
 * per-variable code and no table to fall out of step with.
 *
 * Layout (row 0 is the terminal status bar, the bottom row the alarm bar); the
 * same bordered shape as the other tables:
 *   row 1            blank top margin
 *   row 2            header  (| SCOPE | VARIABLE | VALUE | UNIT | OVERRIDE |)
 *   row 3            top rule (corners cap to '^' when rows are hidden above)
 *   rows 4..N-3      one row per status variable (window into the full set)
 *   row N-8          bottom rule (corners cap to 'v' when hidden below)
 *   rows N-7..N-3    detail panel: Description / Notes / Mapping / Quality
 *   row N-2          legend: [group] badge + position + hint, blank each side
 *
 * UP/DOWN move a selection cursor (the selected variable's NAME is drawn
 * inverted), auto-scrolling the window when the cursor would leave it;
 * PAGE_UP/DOWN move half a window, HOME/END jump to the ends. The table is redrawn
 * immediate-mode each tick and the terminal's back-buffer diff emits only the
 * cells that actually changed, so the page keeps no paint cache.
 */

#include <cstdint>                          // uint8_t/uint16_t/uint64_t
#include <cstring>                          // strcmp / strncpy

#include "libbotanik/config/terminal.h"   // Config::SCREEN_* / EDITOR_* / SHELL_* / PAGE_*

#include "libbotanik/console/console.h"     // Console::rows / terminal
#include "libbotanik/console/terminal.h"    // AnsiTerminal
#include "libbotanik/hal/platform.h"        // HAL::Clock::uptime_ms
#include "libbotanik/pages/page.h"          // Pages::Page
#include "libbotanik/pages/scroll_table.h"  // Pages::ScrollTablePage
#include "libbotanik/vars/access.h"         // Vars::var_published / var_group / var_format

namespace Botanik { namespace Pages {

    /** @brief Full-screen scrollable status-variable table (see file header). */
    class MonitorPage : public ScrollTablePage {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @name Layout
         *  @{ */
        static constexpr uint16_t TOP_ROW    = 1;    // blank top margin
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t TRULE_ROW  = 3;    // top rule
        static constexpr uint16_t DATA_TOP   = 4;    // first variable row
        static constexpr uint16_t SCOPE_W    = 8;    // where the value lives
        // Wide enough for the longest site name today (18) plus headroom, since
        // the generated tables keep growing and a clipped name is unusable.
        static constexpr uint16_t NAME_W     = 24;   // variable-name column
        static constexpr uint16_t UNIT_W     = 6;    // physical-unit column
        static constexpr uint16_t OVR_W      = 9;    // forced value, or "-"
        static constexpr uint16_t VAL_CAP    = 48;   // formatted value buffer
        static constexpr uint16_t LINE_CAP   = Config::TABLE_LINE_LEN;
        // Detail panel below the table: bottom rule + blank spacer +
        // Description + Mapping + Quality + blank spacer, then the legend and its
        // own trailing blank (see _footer_row()).
        static constexpr uint16_t PANEL_H    = 7;
        /** @brief Column order, for the derived cell positions below. */
        enum { C_SCOPE = 0, C_NAME, C_VALUE, C_UNIT, C_OVR };
        /** @} */

        /** @name State
         *  @{ */
        Console&            _console;
        /** @brief Distinct groups discovered from the published rows' names,
         *  interned so the mode labels can keep pointing at them. Built once,
         *  lazily: the table does not change after boot. */
        mutable const char* _group_name[Vars::VAR_GROUPS_MAX] = {};
        mutable uint8_t     _group_n = 0;
        mutable bool        _groups_built = false;
        /** @brief Sequential-scan memo for _ref_at(), and the cached row total.
         *
         *  Both _ref_at() and _total() walk the whole table applying _in_mode(),
         *  which is a strcmp per row. _ref_at() is called once per DRAWN row, so
         *  a 12-row window over a 259-row table cost ~3100 comparisons a refresh
         *  to answer a question whose answer only changes with the mode.
         *
         *  Rows are drawn top to bottom, so remembering the last (row -> index)
         *  pair turns the walk into a resume: the first row of a window pays the
         *  scan, every row after it advances by one. Invalidated by
         *  _on_mode_changed(), which is the only thing that moves the mapping -
         *  the table's rows and scopes are fixed at build time. */
        mutable uint16_t    _memo_row  = 0;
        mutable uint16_t    _memo_idx  = 0;
        mutable bool        _memo_ok   = false;
        mutable uint16_t    _total_n   = 0;
        /** @brief Which mode _total_n was counted for; 0xFF = none yet. Holding
         *  the mode rather than a valid/invalid flag makes the invalidation
         *  structural - a future path that moves _mode without going through
         *  _on_mode_changed() cannot leave a stale count behind. */
        mutable uint8_t     _total_mode = 0xFF;
        const Vars::Bank*     _io;
        /** @brief How to turn a row's writer index into a program name, and the
         *  thing that knows. Null when the build has no scan.
         *
         *  A function pointer rather than a `Scan::Driver*` so this page does
         *  not have to be compiled differently depending on a feature flag it
         *  has no other interest in - and so the page depends on "something can
         *  name a writer" rather than on the scan cycle. */
        const char* (*_writer_name)(const void*, uint8_t) = nullptr;
        const void*           _writer_ctx = nullptr;
        /** @brief A nav happened: redraw on the next tick rather than waiting for
         *  the value cadence, so scrolling/selection feels immediate. */
        bool                _dirty    = true;
        /** @brief Uptime of the last value refresh (see tick()). */
        uint64_t            _last_refresh_ms = 0;
        /** @} */

    public:
        /** @brief @p console owns the content geometry; @p io is the variable
         *  table this shows (null -> empty page). */
        MonitorPage(Console& console,
                    const Vars::Bank* io)
            : ScrollTablePage(console.terminal()), _console(console),
              _io(io) {}

        /** @brief Teach the page to name the program that computes a row.
         *  Wired by the firmware, which is the only thing holding both the page
         *  and the scan driver. Without it a computed row still reads
         *  "computed", which is what it said before. */
        void set_writer_names(const char* (*fn)(const void*, uint8_t),
                              const void* ctx) {
            _writer_name = fn;
            _writer_ctx  = ctx;
        }

        const char* name() const override { return "MONITOR"; }

        /** @name Input
         *  @{ */

        void handle_command(Command cmd, uint16_t = 1) override {
            if (_mode_command(cmd)) return;         // LEFT/RIGHT pick the group
            if (_nav_command(cmd)) _dirty = true;   // redraw next tick, not the cadence
        }

        void handle_text(char c) override { (void)_mode_text(c); }   // SPACE: next group

        /** @} */

        /** @name Focus & update
         *  @{ */

        void deactivate() override {}

        void focus() override;

        void tick(bool active) override;

        /** @} */

    private:
        // Model
        // --------------------------------------------------------------------

        /** @name Group modes
         *  Mode 0 shows every group; modes 1..N show one each. The site already
         *  groups its variables for the monitor table and telemetry, so this is
         *  the grouping it declared, surfaced as a view rather than a second
         *  concept. SPACE / RIGHT step forward, LEFT back (scroll_table.h).
         *  @{ */
        /** @brief Discover the distinct groups, in table order. */
        void _ensure_groups() const;

        uint8_t _mode_count() const override { _ensure_groups(); return (uint8_t)(_group_n + 1); }
        const char* _mode_label(uint8_t i) const override;
        void _on_mode_changed() override {
            _dirty = true;
            _memo_ok = false;      // the row -> index mapping just moved
        }
        /** @} */

        /** @brief Whether row @p i is in the table at all, and in the active
         *  view. Mode 0 is every published row; mode k is one group. */
        bool _in_mode(uint16_t i) const;

        uint16_t _total() const;

        /** @brief Legend row and bottom rule row. The legend sits one row up
         *  from the bottom so it has a blank line each side, the same framing the
         *  ALARMS table gives its own - a badge reads as a badge when it is not
         *  jammed against the alarm bar. The bottom rule sits PANEL_H rows above
         *  it, reserving the detail panel. */
        uint16_t _footer_row() const { return (uint16_t)(_console.rows() - 2); }
        uint16_t _brule_row() const {
            uint16_t f = _footer_row();
            return (f > (uint16_t)(DATA_TOP + PANEL_H)) ? (uint16_t)(f - PANEL_H) : DATA_TOP;
        }

        /** @brief Visible variable rows (table shrinks to leave room for the
         *  detail panel below the bottom rule). */
        uint16_t _window() const override {
            uint16_t brule = _brule_row();
            return (brule > DATA_TOP) ? (uint16_t)(brule - DATA_TOP) : 0;
        }

        /** @name ScrollTablePage geometry
         *  @{ */
        uint16_t _data_top()  const override { return DATA_TOP; }
        uint16_t _row_count() const override { return _total(); }
        uint16_t _selected_cell(char* out, uint16_t cap) const override;
        /** @} */

        /** @brief Flexible VALUE width for the 4-column row (group | name | value
         *  | unit): the display minus the three fixed columns and every border,
         *  floored at 8 and capped at VAL_CAP-1. */
        uint16_t _value_w() const;

        uint8_t _columns(Col* out) const override;

        /** @brief The reference for variable @p r; @p ok is false when @p r is
         *  past the end. */
        Vars::VarRef _ref_at(uint16_t r, bool& ok) const;

        // Rendering
        // --------------------------------------------------------------------

        /** @brief Copy @p s into @p out unless it already IS @p out (var_format
         *  returns flash / pool text for an enum or a string). */
        static void _adopt(char* out, size_t cap, const char* s) {
            if (s != out) { std::strncpy(out, s, cap - 1); out[cap - 1] = 0; }
        }

        /** @brief Build the full "  | scope | name | value | unit | override |"
         *  row. Resolves the variable ONCE: the row is drawn per visible line
         *  every refresh, and _at() is a walk over the group table. */
        uint16_t _row_text(uint16_t r, char* out, size_t cap) const;

        /** @brief Draw data row @p r at screen @p row, inverting the NAME field for
         *  the selection cursor when @p selected. The terminal's back-buffer diff
         *  pares it to the cells that actually moved. */
        void _draw_row(uint16_t r, uint16_t row, bool selected);

        /** @brief One line saying where a reading comes from. */
        static void _describe_binding(const Vars::Binding& b, char* out, size_t cap);

        /** @brief Draw both rules, capping with '^' / 'v' when rows are hidden
         *  above / below the window. */
        void _draw_rules();

        /** @brief Detail panel below the bottom rule: blank, Description, Notes,
         *  blank for the selected variable. */
        void _draw_panel();

        void _draw_footer();

        /** @brief Draw the static chrome (blank top margin + header); the rules and
         *  footer carry live state and are drawn separately each tick. */
        void _draw_structure() {
            _blank_line(TOP_ROW);
            _draw_header(HEADER_ROW);
        }

    };

}}

#endif // PAGES_MONITOR_H
