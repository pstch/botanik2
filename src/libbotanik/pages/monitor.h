// REVIEW_35: TODO
#ifndef PAGES_MONITOR_H
#define PAGES_MONITOR_H

/**
 * @file monitor.h
 * @brief MonitorPage - scrollable table of PLC status variables.
 *
 * The read-only sibling of the CONFIG page: it renders the site's status
 * variable table (the ScalarGroup[] the `status` shell command and MQTT
 * telemetry also read) live off the PLC payload, one row per field, grouped.
 * Generic over any ScalarGroup[] + base pointer, so it has no per-field code.
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

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/pages/scroll_table.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/network/telemetry_types.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen scrollable status-variable table (see file header). */
    class MonitorPage : public ScrollTablePage {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;
        using ScalarGroup  = Network::ScalarGroup;

        /** @name Layout
         *  @{ */
        static constexpr uint16_t MARGIN     = 2;    // blank columns each side
        static constexpr uint16_t TOP_ROW    = 1;    // blank top margin
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t TRULE_ROW  = 3;    // top rule
        static constexpr uint16_t DATA_TOP   = 4;    // first variable row
        static constexpr uint16_t GROUP_W    = 12;   // group-topic column
        static constexpr uint16_t NAME_W     = 18;   // variable-name column
        static constexpr uint16_t VAL_CAP    = 48;   // formatted value buffer
        static constexpr uint16_t LINE_CAP   = 192;  // row string buffer
        /** @brief NAME field span (contents only): the part inverted for the
         *  selection cursor, leaving the border gap each side. */
        static constexpr uint16_t NAME_CELL_L = MARGIN + 2 + GROUP_W + 3;   // 19
        static constexpr uint16_t NAME_CELL_R = NAME_CELL_L + NAME_W;       // 37
        /** @} */

        /** @name State
         *  @{ */
        Console&            _console;
        const ScalarGroup*  _groups;
        size_t              _group_count;
        const void*         _base;
        bool                _active   = false;
        /** @brief A nav happened: redraw on the next tick rather than waiting for
         *  the value cadence, so scrolling/selection feels immediate. */
        bool                _dirty    = true;
        /** @brief Uptime of the last value refresh (see tick()). */
        uint64_t            _last_refresh_ms = 0;
        /** @} */

    public:
        /** @brief @p console owns the content geometry; @p groups + @p base are
         *  the status-variable table and the payload it reads (any may be
         *  null/0 -> empty table). */
        MonitorPage(Console& console, const ScalarGroup* groups, size_t group_count,
                    const void* base)
            : ScrollTablePage(console.terminal()), _console(console),
              _groups(groups), _group_count(group_count), _base(base) {}

        const char* name() const override { return "MONITOR"; }

        /** @name Input
         *  @{ */

        void handle_text(char /*c*/) override {}   // read-only: no text entry

        void handle_command(Command cmd, uint16_t count = 1) override {
            uint16_t total = _total(), win = _window();
            if (total == 0 || win == 0) return;
            switch (cmd) {
                case Command::UP:    case Command::CTRL_P: _move_selection(-1);         break;
                case Command::DOWN:  case Command::CTRL_N: _move_selection(+1);         break;
                case Command::PAGE_UP:   _move_selection(-(int)_page_step());             break;
                case Command::PAGE_DOWN: _move_selection(+(int)_page_step());             break;
                case Command::HOME:  _move_selection(-(int)total);                      break;
                case Command::END:   _move_selection(+(int)total);                      break;
                default: return;
            }
            _dirty = true;   // redraw next tick, not on the value cadence
            (void)count;
        }

        /** @} */

        /** @name Focus & update
         *  @{ */

        void deactivate() override { _active = false; }

        void focus() override {
            _active = true;
            _clamp_selection();
            _follow_selection();
            _dirty = true;
        }

        void tick(bool active) override {
            _active = active;
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

        /** @brief Visible variable rows (between the two rules). */
        uint16_t _window() const override {
            uint16_t brule = (uint16_t)(_console.rows() - 2);
            return (brule > DATA_TOP) ? (uint16_t)(brule - DATA_TOP) : 0;
        }

        /** @name ScrollTablePage geometry
         *  @{ */
        uint16_t _data_top()  const override { return DATA_TOP; }
        uint16_t _row_count() const override { return _total(); }
        /** @} */

        uint16_t _value_w() const {
            uint16_t used = (uint16_t)(2 * MARGIN + 2 + GROUP_W + 3 + NAME_W + 3 + 2);
            uint16_t cols = _terminal.cols();
            uint16_t w = (cols > used) ? (uint16_t)(cols - used) : 8;
            if (w < 8) w = 8;
            if (w > VAL_CAP - 1) w = (uint16_t)(VAL_CAP - 1);
            return w;
        }

        // Rendering
        // --------------------------------------------------------------------

        /** @brief Format variable @p r's value into @p out. */
        void _value_text(uint16_t r, char* out, size_t cap) const {
            size_t gi = 0, fi = 0; bool first = false;
            if (_base == nullptr || !_at(r, gi, fi, first)) { if (cap) out[0] = 0; return; }
            const Network::ScalarField& f = _groups[gi].fields[fi];
            const char* s = Network::scalar_format(f, _base, out, cap);
            if (s != out) { std::strncpy(out, s, cap - 1); out[cap - 1] = 0; }
        }

        /** @brief Build the full "  | group | name | value |" row into @p out. */
        uint16_t _row_text(uint16_t r, char* out, size_t cap) const {
            size_t gi = 0, fi = 0; bool first = false;
            if (!_at(r, gi, fi, first)) { if (cap) out[0] = 0; return 0; }
            const Network::ScalarField& f = _groups[gi].fields[fi];
            char val[VAL_CAP];
            _value_text(r, val, sizeof(val));
            int n = snprintf(out, cap, "%*s| %-*.*s | %-*.*s | %-*.*s |",
                             (int)MARGIN, "",
                             GROUP_W, GROUP_W, first ? _groups[gi].topic : "",
                             NAME_W, NAME_W, f.name ? f.name : "",
                             _value_w(), _value_w(), val);
            return (n < 0) ? 0 : (uint16_t)n;
        }

        /** @brief Draw data row @p r at screen @p row, inverting the NAME field for
         *  the selection cursor when @p selected. The terminal's back-buffer diff
         *  pares it to the cells that actually moved. */
        void _draw_row(uint16_t r, uint16_t row, bool selected) {
            char line[LINE_CAP];
            uint16_t n = _row_text(r, line, sizeof(line));
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            if (selected && n > NAME_CELL_L) {
                _terminal.write_chars(line, NAME_CELL_L);        // margin + | group |
                _terminal.set_inverted(true);
                uint16_t nend = (n < NAME_CELL_R) ? n : NAME_CELL_R;
                _terminal.write_chars(line + NAME_CELL_L, (uint16_t)(nend - NAME_CELL_L));
                _terminal.set_inverted(false);
                if (n > NAME_CELL_R)
                    _terminal.write_chars(line + NAME_CELL_R, (uint16_t)(n - NAME_CELL_R));
            } else {
                _terminal.write_chars(line, n);
            }
            _terminal.clear_to_eol();
        }

        void _draw_header() {
            char line[LINE_CAP];
            int n = snprintf(line, sizeof(line), "%*s| %-*.*s | %-*.*s | %-*.*s |",
                             (int)MARGIN, "", GROUP_W, GROUP_W, "GROUP",
                             NAME_W, NAME_W, "VARIABLE", _value_w(), _value_w(), "VALUE");
            _terminal.move_to({HEADER_ROW, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, (n < 0) ? 0 : (uint16_t)n);
            _terminal.clear_to_eol();
        }

        /** @brief Draw a rule row; @p cap ('^' / 'v' / '+') marks hidden rows. */
        void _draw_rule(uint16_t row, char cap) {
            char line[LINE_CAP];
            uint16_t vw = _value_w();
            size_t k = 0;
            for (uint16_t i = 0; i < MARGIN && k < sizeof(line); i++)     line[k++] = ' ';
            if (k < sizeof(line)) line[k++] = cap;
            for (uint16_t i = 0; i < GROUP_W + 2 && k < sizeof(line); i++) line[k++] = '-';
            if (k < sizeof(line)) line[k++] = '+';
            for (uint16_t i = 0; i < NAME_W + 2 && k < sizeof(line); i++)  line[k++] = '-';
            if (k < sizeof(line)) line[k++] = '+';
            for (uint16_t i = 0; i < vw + 2 && k < sizeof(line); i++)      line[k++] = '-';
            if (k < sizeof(line)) line[k++] = cap;
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, (uint16_t)k);
            _terminal.clear_to_eol();
        }

        /** @brief Draw both rules, capping with '^' / 'v' when rows are hidden
         *  above / below the window. */
        void _draw_rules() {
            uint16_t win = _window(), total = _total();
            _draw_rule(TRULE_ROW, _top > 0 ? '^' : '+');
            _draw_rule((uint16_t)(_console.rows() - 2),
                       (uint16_t)(_top + win) < total ? 'v' : '+');
        }

        void _draw_footer() {
            uint16_t total = _total(), win = _window();
            uint16_t last  = (uint16_t)(_top + (win < total ? win : total));
            char line[LINE_CAP];
            int n = snprintf(line, sizeof(line),
                             " UP/DOWN select   PAGE UP/DOWN page      %u-%u / %u",
                             (unsigned)(total ? _top + 1 : 0), (unsigned)last, (unsigned)total);
            _terminal.move_to({(uint16_t)(_console.rows() - 1), 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, (n < 0) ? 0 : (uint16_t)n);
            _terminal.clear_to_eol();
        }

        /** @brief Draw the static chrome (blank top margin + header); the rules and
         *  footer carry live state and are drawn separately each tick. */
        void _draw_structure() {
            _terminal.move_to({TOP_ROW, 0});
            _terminal.set_inverted(false);
            _terminal.clear_to_eol();
            _draw_header();
        }

        // Selection
        // --------------------------------------------------------------------

    };

}}

#endif // PAGES_MONITOR_H
