// REVIEW_35: TODO
#ifndef PAGES_MONITOR_H
#define PAGES_MONITOR_H

/**
 * @file monitor.h
 * @brief MonitorPage - scrollable table of the site's status variables.
 *
 * The read-only sibling of the CONFIG page: it renders the site's status
 * variable table (the VarGroup[] MQTT telemetry
 * also read) live off the variable tables, one row per variable, grouped.
 * Generic over any VarGroup[] + Io, so it has no per-variable code.
 *
 * Layout (row 0 is the terminal status bar, the bottom row the alarm bar); the
 * same bordered shape as the other tables:
 *   row 1            blank top margin
 *   row 2            header  (| SCOPE | VARIABLE | VALUE | UNIT | OVERRIDE |)
 *   row 3            top rule (corners cap to '^' when rows are hidden above)
 *   rows 4..N-3      one row per status variable (window into the full set)
 *   row N-8          bottom rule (corners cap to 'v' when hidden below)
 *   rows N-7..N-3    detail panel for the selected variable
 *   row N-2          legend: [group] badge + position + hint, blank each side
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
        static constexpr uint16_t LINE_CAP   = 192;  // row string buffer
        // Detail panel below the table: bottom rule + blank spacer +
        // Description + Source + Quality + blank spacer, then the legend and its
        // own trailing blank (see _footer_row()).
        static constexpr uint16_t PANEL_H    = 6;
        /** @brief Column order, for the derived cell positions below. */
        enum { C_SCOPE = 0, C_NAME, C_VALUE, C_UNIT, C_OVR };
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

        void handle_command(Command cmd, uint16_t = 1) override {
            if (_mode_command(cmd)) return;         // LEFT/RIGHT pick the group
            if (_nav_command(cmd)) _dirty = true;   // redraw next tick, not the cadence
        }

        void handle_text(char c) override { (void)_mode_text(c); }   // SPACE: next group

        /** @} */

        /** @name Focus & update
         *  @{ */

        void deactivate() override {}

        void focus() override {
            // The footer carries the group badge, which acknowledges a keypress,
            // so it must beat the table rows it sits under.
            _claim_mode_row(_footer_row());
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

        /** @name Group modes
         *  Mode 0 shows every group; modes 1..N show one each. The site already
         *  groups its variables for the monitor table and telemetry, so this is
         *  the grouping it declared, surfaced as a view rather than a second
         *  concept. SPACE / RIGHT step forward, LEFT back (scroll_table.h).
         *  @{ */
        uint8_t _mode_count() const override { return (uint8_t)(_group_count + 1); }
        const char* _mode_label(uint8_t i) const override {
            if (i == 0) return "all";
            return (i <= _group_count) ? _groups[i - 1].topic : "all";
        }
        void _on_mode_changed() override { _dirty = true; }
        uint16_t _console_rows() const override { return _console.rows(); }

        /** @brief First and one-past-last group index the active mode shows. */
        size_t _first_group() const { return _mode == 0 ? 0 : (size_t)(_mode - 1); }
        size_t _last_group() const  { return _mode == 0 ? _group_count : (size_t)_mode; }
        /** @} */

        uint16_t _total() const {
            size_t t = 0;
            for (size_t g = _first_group(); g < _last_group(); g++) t += _groups[g].count;
            return (uint16_t)t;
        }

        /** @brief Resolve variable index @p r to its group + field, and whether
         *  it leads its group (so the group column prints once). */
        bool _at(uint16_t r, size_t& gi, size_t& fi, bool& first) const {
            size_t acc = 0;
            for (size_t g = _first_group(); g < _last_group(); g++) {
                if (r < acc + _groups[g].count) {
                    gi = g; fi = (size_t)(r - acc); first = (fi == 0);
                    return true;
                }
                acc += _groups[g].count;
            }
            return false;
        }

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
        /** @} */

        /** @brief Flexible VALUE width for the 4-column row (group | name | value
         *  | unit): the display minus the three fixed columns and every border,
         *  floored at 8 and capped at VAL_CAP-1. */
        uint16_t _value_w() const {
            return _flex_width((uint16_t)(2 * MARGIN + 2 + SCOPE_W + 3 + NAME_W + 3
                                          + 3 + UNIT_W + 3 + OVR_W + 2),
                               (uint16_t)(VAL_CAP - 1));
        }

        uint8_t _columns(Col* out) const override {
            out[C_SCOPE] = Col{ "SCOPE",    SCOPE_W    };
            out[C_NAME]  = Col{ "VARIABLE", NAME_W     };
            out[C_VALUE] = Col{ "VALUE",    _value_w() };
            out[C_UNIT]  = Col{ "UNIT",     UNIT_W     };
            // A forced reading is indistinguishable from a real one in the VALUE
            // column - that is the whole hazard of an override - so the table
            // says so per row rather than only in the detail panel.
            out[C_OVR]   = Col{ "OVERRIDE", OVR_W      };
            return 5;
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
            const char* nm = "", *un = "", *sc = "";
            char ovr[VAL_CAP]; ovr[0] = '-'; ovr[1] = 0;
            if (_io != nullptr) {
                Vars::Var h = _io->var(ref);
                const Vars::Variable* m = h.meta();
                if (m->name != nullptr) nm = m->name;
                if (m->unit != nullptr) un = m->unit;
                // Scope, not the group: the group is the VIEW now (SPACE/LEFT/
                // RIGHT), so repeating it down a column said the same thing on
                // every row. Where a reading comes from does not.
                sc = Vars::var_scope(*m);
                if (h.overridden()) {
                    const char* o = Vars::var_format(*_io, h, ovr, sizeof(ovr));
                    if (o != ovr) { std::strncpy(ovr, o, sizeof(ovr) - 1);
                                    ovr[sizeof(ovr) - 1] = 0; }
                }
            }
            (void)first;
            const char* cells[5] = { sc, nm, val, un, ovr };
            return _build_row(out, cap, cells);
        }

        /** @brief Draw data row @p r at screen @p row, inverting the NAME field for
         *  the selection cursor when @p selected. The terminal's back-buffer diff
         *  pares it to the cells that actually moved. */
        void _draw_row(uint16_t r, uint16_t row, bool selected) {
            char line[LINE_CAP];
            uint16_t n = _row_text(r, line, sizeof(line));
            _draw_selected_line(row, line, n, _cell_col(C_NAME), _cell_end(C_NAME),
                                selected);
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
            // its default, which looks exactly like a real reading otherwise -
            // and so does one a maintainer forced hours ago, which is why an
            // override outranks the rest here.
            const char* quality = nullptr;
            if (have) {
                if (h.overridden())   quality = h.valid() ? "OVERRIDDEN (forced by hand)"
                                                          : "OVERRIDDEN (forced invalid)";
                else if (h.valid())   quality = "live";
                else                  quality = "no data (showing default)";
            }
            _panel_field((uint16_t)(br + 4), "Quality", quality);
            _blank_line((uint16_t)(br + 5));
        }

        void _draw_footer() {
            uint16_t total = _total(), win = _window();
            uint16_t last  = (uint16_t)(_top + (win < total ? win : total));
            char detail[64], hint[48];
            // In the "all" view the badge does not say which group the cursor is
            // sitting in, so the legend does; in a single-group view it would
            // just repeat the badge.
            const char* grp = "";
            if (_mode == 0) {
                size_t gi = 0, fi = 0; bool first = false;
                if (_at(_selected, gi, fi, first)) grp = _groups[gi].topic;
            }
            snprintf(detail, sizeof(detail), "%-12s %u-%u / %u", grp,
                     (unsigned)(total ? _top + 1 : 0), (unsigned)last, (unsigned)total);
            snprintf(hint, sizeof(hint), "UP/DOWN  <-/->/SPACE: %s", _next_mode_name());
            _draw_mode_line(_footer_row(), detail, hint);
            _blank_line((uint16_t)(_footer_row() + 1));   // blank below the legend
        }

        /** @brief Draw the static chrome (blank top margin + header); the rules and
         *  footer carry live state and are drawn separately each tick. */
        void _draw_structure() {
            _blank_line(TOP_ROW);
            _draw_header(HEADER_ROW);
        }

    };

}}

#endif // PAGES_MONITOR_H
