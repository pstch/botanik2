// REVIEW_35: TODO
#ifndef PAGES_MONITOR_H
#define PAGES_MONITOR_H

/**
 * @file monitor.h
 * @brief MonitorPage - scrollable table of the site's status variables.
 *
 * The read-only sibling of the CONFIG page: it renders the site's status
 * variable table (the VarGroup[] the `status` shell command and MQTT telemetry
 * also read) live off the variable tables, one row per variable, grouped.
 * Generic over any VarGroup[] + Io, so it has no per-variable code.
 *
 * Layout (row 0 is the terminal status bar, the bottom row the alarm bar); the
 * same bordered shape as the other tables:
 *   row 1            blank top margin
 *   row 2            header  (| GROUP | VARIABLE | VALUE |)
 *   row 3            top rule (corners cap to '^' when rows are hidden above)
 *   rows 4..N-3      one row per status variable (window into the full set)
 *   row N-2          bottom rule (corners cap to 'v' when hidden below)
 *   row N-1          footer: nav hint + position
 *
 * UP/DOWN move a selection cursor (the selected variable's NAME is drawn
 * inverted), auto-scrolling the window when the cursor would leave it;
 * PAGE_UP/DOWN move half a window, HOME/END jump to the ends. The table is redrawn
 * immediate-mode each tick and the terminal's back-buffer diff emits only the
 * cells that actually changed, so the page keeps no paint cache.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../libbotanik_config.h"
#include "../hal/platform.h"
#include "page.h"
#include "scroll_table.h"
#include "../console/console.h"
#include "../console/terminal.h"
#include "../vars/access.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen scrollable status-variable table (see file header). */
    class MonitorPage : public ScrollTablePage {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @name Layout
         *  @{ */
        static constexpr uint16_t MARGIN     = 2;    // blank columns each side
        static constexpr uint16_t TOP_ROW    = 1;    // blank top margin
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t TRULE_ROW  = 3;    // top rule
        static constexpr uint16_t DATA_TOP   = 4;    // first variable row
        static constexpr uint16_t GROUP_W    = 12;   // group-topic column
        static constexpr uint16_t NAME_W     = 18;   // variable-name column
        static constexpr uint16_t UNIT_W     = 6;    // physical-unit column
        static constexpr uint16_t VAL_CAP    = 48;   // formatted value buffer
        static constexpr uint16_t LINE_CAP   = 192;  // row string buffer
        // Detail panel below the table (like the CONFIG page): bottom rule +
        // blank spacer + Description + Notes + Quality + blank spacer.
        static constexpr uint16_t PANEL_H    = 6;
        /** @brief NAME field span (contents only): the part inverted for the
         *  selection cursor, leaving the border gap each side. */
        static constexpr uint16_t NAME_CELL_L = MARGIN + 2 + GROUP_W + 3;   // 19
        static constexpr uint16_t NAME_CELL_R = NAME_CELL_L + NAME_W;       // 37
        /** @} */

        /** @name State
         *  @{ */
        Console&            _console;
        const Vars::VarGroup* _groups;
        size_t              _group_count;
        const Vars::Bank*     _io;
        /** @brief A nav happened: redraw on the next tick rather than waiting for
         *  the value cadence, so scrolling/selection feels immediate. */
        bool                _dirty    = true;
        /** @brief Uptime of the last value refresh (see tick()). */
        uint64_t            _last_refresh_ms = 0;
        /** @} */

    public:
        /** @brief @p console owns the content geometry; @p groups names the
         *  variables to show and @p io is what resolves them (any may be
         *  null/0 -> empty table). */
        MonitorPage(Console& console, const Vars::VarGroup* groups, size_t group_count,
                    const Vars::Bank* io)
            : ScrollTablePage(console.terminal()), _console(console),
              _groups(groups), _group_count(group_count), _io(io) {}

        const char* name() const override { return "MONITOR"; }

        /** @name Input
         *  @{ */

        void handle_text(char /*c*/) override {}   // read-only: no text entry

        void handle_command(Command cmd, uint16_t = 1) override {
            if (_nav_command(cmd)) _dirty = true;   // redraw next tick, not the cadence
        }

        /** @} */

        /** @name Focus & update
         *  @{ */

        void deactivate() override {}

        void focus() override {
            _clamp_selection();
            _follow_selection();
            _dirty = true;
        }

        void tick(bool active) override {
            if (!active) return;
            // Rebuild on the value cadence (or immediately after a nav): the table
            // is redrawn immediate-mode and the terminal's back-buffer diff emits
            // only the cells that moved, so the page keeps no per-row cache - but
            // re-formatting every row every tick would still burn CPU for nothing.
            uint64_t now = HAL::Clock::uptime_ms();
            if (!_dirty && now - _last_refresh_ms < Config::PAGE_HOT_MS) return;
            _last_refresh_ms = now;
            _dirty = false;
            uint16_t win = _window();
            _clamp_selection();
            _follow_selection();
            _draw_structure();
            _draw_rules();
            _draw_panel();
            _draw_footer();
            for (uint16_t vi = 0; vi < win; vi++) {
                uint16_t r = (uint16_t)(_top + vi);
                _draw_row(r, (uint16_t)(DATA_TOP + vi), r == _selected);
            }
        }

        /** @} */

    private:
        // Model
        // --------------------------------------------------------------------

        uint16_t _total() const {
            size_t t = 0;
            for (size_t g = 0; g < _group_count; g++) t += _groups[g].count;
            return (uint16_t)t;
        }

        /** @brief Resolve variable index @p r to its group + field, and whether
         *  it leads its group (so the group column prints once). */
        bool _at(uint16_t r, size_t& gi, size_t& fi, bool& first) const {
            size_t acc = 0;
            for (size_t g = 0; g < _group_count; g++) {
                if (r < acc + _groups[g].count) {
                    gi = g; fi = (size_t)(r - acc); first = (fi == 0);
                    return true;
                }
                acc += _groups[g].count;
            }
            return false;
        }

        /** @brief Footer row (position line) and bottom rule row. The bottom rule
         *  sits PANEL_H rows above the footer, reserving the detail panel. */
        uint16_t _footer_row() const { return (uint16_t)(_console.rows() - 1); }
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
        /** @} */

        /** @brief Flexible VALUE width for the 4-column row (group | name | value
         *  | unit): the display minus the three fixed columns and every border,
         *  floored at 8 and capped at VAL_CAP-1. */
        uint16_t _value_w() const {
            return _flex_width((uint16_t)(2 * MARGIN + 2 + GROUP_W + 3 + NAME_W + 3
                                          + 3 + UNIT_W + 2), (uint16_t)(VAL_CAP - 1));
        }

        /** @brief Build a "  | group | name | value | unit |" row into @p out. */
        uint16_t _row4(char* out, size_t cap, const char* g, const char* nm,
                       const char* v, const char* u) const {
            int n = snprintf(out, cap, "%*s| %-*.*s | %-*.*s | %-*.*s | %-*.*s |",
                             (int)MARGIN, "", GROUP_W, GROUP_W, g, NAME_W, NAME_W, nm,
                             _value_w(), _value_w(), v, UNIT_W, UNIT_W, u);
            return (n < 0) ? 0 : (uint16_t)n;
        }

        /** @brief The reference for variable @p r; @p ok is false when @p r is
         *  past the end. */
        Vars::VarRef _ref_at(uint16_t r, bool& ok) const {
            size_t gi = 0, fi = 0; bool first = false;
            ok = _at(r, gi, fi, first);
            if (!ok) { Vars::VarRef none = { Vars::VAR_NONE }; return none; }
            return _groups[gi].refs[fi];
        }

        // Rendering
        // --------------------------------------------------------------------

        /** @brief Format variable @p r's value into @p out. */
        void _value_text(uint16_t r, char* out, size_t cap) const {
            bool ok = false;
            Vars::VarRef ref = _ref_at(r, ok);
            if (!ok || _io == nullptr) { if (cap) out[0] = 0; return; }
            const char* s = Vars::var_format(*_io, ref, out, cap);
            if (s != out) { std::strncpy(out, s, cap - 1); out[cap - 1] = 0; }
        }

        /** @brief Build the full "  | group | name | value | unit |" row. */
        uint16_t _row_text(uint16_t r, char* out, size_t cap) const {
            size_t gi = 0, fi = 0; bool first = false;
            if (!_at(r, gi, fi, first)) { if (cap) out[0] = 0; return 0; }
            bool ok = false;
            Vars::VarRef ref = _ref_at(r, ok);
            char val[VAL_CAP];
            _value_text(r, val, sizeof(val));
            const char* nm = "", *un = "";
            if (_io != nullptr) {
                const Vars::Variable* m = _io->var(ref).meta();
                if (m->name != nullptr) nm = m->name;
                if (m->unit != nullptr) un = m->unit;
            }
            return _row4(out, cap, first ? _groups[gi].topic : "", nm, val, un);
        }

        /** @brief Draw data row @p r at screen @p row, inverting the NAME field for
         *  the selection cursor when @p selected. The terminal's back-buffer diff
         *  pares it to the cells that actually moved. */
        void _draw_row(uint16_t r, uint16_t row, bool selected) {
            char line[LINE_CAP];
            uint16_t n = _row_text(r, line, sizeof(line));
            _draw_selected_line(row, line, n, NAME_CELL_L, NAME_CELL_R, selected);
        }

        void _draw_header() {
            char line[LINE_CAP];
            uint16_t n = _row4(line, sizeof(line), "GROUP", "VARIABLE", "VALUE", "UNIT");
            _draw_line(HEADER_ROW, line, n);
        }

        /** @brief One line saying where a reading comes from. */
        static void _describe_binding(const Vars::Binding& b, char* out, size_t cap) {
            switch (b.transport) {
                case Vars::XP_MODBUS:
                    snprintf(out, cap, "Modbus dev %u fc %u reg %u",
                             (unsigned)b.device, (unsigned)b.fc, (unsigned)b.reg);
                    break;
                case Vars::XP_LOCAL_ANALOG:
                    snprintf(out, cap, "analog channel %u", (unsigned)b.reg);
                    break;
                case Vars::XP_LOCAL_DIGITAL:
                    snprintf(out, cap, "digital channel %u", (unsigned)b.reg);
                    break;
                default:
                    snprintf(out, cap, "computed");
                    break;
            }
        }

        /** @brief Draw a rule row; @p cap ('^' / 'v' / '+') marks hidden rows. */
        void _draw_rule(uint16_t row, char cap) {
            uint16_t spans[4] = { (uint16_t)(GROUP_W + 2), (uint16_t)(NAME_W + 2),
                                  (uint16_t)(_value_w() + 2), (uint16_t)(UNIT_W + 2) };
            ScrollTablePage::_draw_rule(row, MARGIN, spans, 4, '+', cap);
        }

        /** @brief Draw both rules, capping with '^' / 'v' when rows are hidden
         *  above / below the window. */
        void _draw_rules() {
            uint16_t win = _window(), total = _total();
            _draw_rule(TRULE_ROW, _top > 0 ? '^' : '+');
            _draw_rule(_brule_row(), (uint16_t)(_top + win) < total ? 'v' : '+');
        }

        /** @brief Detail panel below the bottom rule: blank, Description, Notes,
         *  blank for the selected variable. */
        void _draw_panel() {
            uint16_t br = _brule_row();
            _blank_line((uint16_t)(br + 1));
            bool ok = false;
            Vars::VarRef ref = _ref_at(_selected, ok);
            const bool have = ok && _io != nullptr;
            Vars::Var h = have ? _io->var(ref) : Vars::Var(nullptr, nullptr);
            _panel_field((uint16_t)(br + 2), "Description", have ? h.meta()->desc : nullptr);
            // The binding, which the byte-offset model had nowhere to put: where
            // this reading physically comes from is the first thing you want
            // when one looks wrong.
            char bind[48];
            if (have) _describe_binding(h.meta()->bind, bind, sizeof(bind));
            _panel_field((uint16_t)(br + 3), "Source", have ? bind : nullptr);
            // Quality is likewise new: a variable nothing has written reads as
            // its default, which looks exactly like a real reading otherwise.
            _panel_field((uint16_t)(br + 4), "Quality",
                         !have ? nullptr : (h.valid() ? "live" : "no data (showing default)"));
            _blank_line((uint16_t)(br + 5));
        }

        void _draw_footer() {
            uint16_t total = _total(), win = _window();
            uint16_t last  = (uint16_t)(_top + (win < total ? win : total));
            char line[LINE_CAP];
            int n = snprintf(line, sizeof(line),
                             " UP/DOWN select   PAGE UP/DOWN page      %u-%u / %u",
                             (unsigned)(total ? _top + 1 : 0), (unsigned)last, (unsigned)total);
            _draw_line(_footer_row(), line, (n < 0) ? 0 : (uint16_t)n);
        }

        /** @brief Draw the static chrome (blank top margin + header); the rules and
         *  footer carry live state and are drawn separately each tick. */
        void _draw_structure() {
            _blank_line(TOP_ROW);
            _draw_header();
        }

        // Selection
        // --------------------------------------------------------------------

    };

}}

#endif // PAGES_MONITOR_H
