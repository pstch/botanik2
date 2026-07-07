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
 *   row N-1          footer: scroll hint + position
 *
 * UP/DOWN scroll a line, PAGE_UP/DOWN a window, HOME/END jump to the ends. Rows
 * repaint only on a value change (per-row diff cache); a scroll triggers a
 * paced full repaint (a few rows per tick under the TX floor, as AlarmsPage) so
 * the status bar keeps updating.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/network/telemetry_types.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen scrollable status-variable table (see file header). */
    class MonitorPage : public Page {
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
        static constexpr uint16_t VAL_CAP    = 48;   // formatted value + diff cache
        static constexpr uint16_t LINE_CAP   = 192;  // row string buffer
        static constexpr uint16_t MAX_CACHE_ROWS = 40;
        static constexpr uint16_t PAINT_IDLE = 0xFFFF;
        /** @} */

        /** @name State
         *  @{ */
        Console&            _console;
        AnsiTerminal&       _terminal;
        const ScalarGroup*  _groups;
        size_t              _group_count;
        const void*         _base;
        bool                _active   = false;
        uint16_t            _top      = 0;           ///< first visible variable
        uint16_t            _paint_i  = PAINT_IDLE;  ///< staged-paint cursor
        /** @brief Per-screen-row value cache (diff gate); a scroll invalidates
         *  it wholesale via the paced repaint. */
        char                _vcache[MAX_CACHE_ROWS][VAL_CAP];
        uint16_t            _painted_top = 0xFFFF;   ///< _top the cache reflects
        /** @} */

    public:
        /** @brief @p console owns the content geometry; @p groups + @p base are
         *  the status-variable table and the payload it reads (any may be
         *  null/0 -> empty table). */
        MonitorPage(Console& console, const ScalarGroup* groups, size_t group_count,
                    const void* base)
            : _console(console), _terminal(console.terminal()),
              _groups(groups), _group_count(group_count), _base(base) {}

        const char* name() const override { return "MONITOR"; }

        /** @name Input
         *  @{ */

        void handle_text(char /*c*/) override {}   // read-only: no text entry

        void handle_command(Command cmd, uint16_t count = 1) override {
            uint16_t total = _total();
            uint16_t win   = _window();
            if (total == 0 || win == 0) return;
            uint16_t maxtop = (total > win) ? (uint16_t)(total - win) : 0;
            switch (cmd) {
                case Command::UP:    case Command::CTRL_P: _scroll(-1, maxtop);       break;
                case Command::DOWN:  case Command::CTRL_N: _scroll(+1, maxtop);       break;
                case Command::PAGE_UP:   _scroll(-(int)win, maxtop);                  break;
                case Command::PAGE_DOWN: _scroll(+(int)win, maxtop);                  break;
                case Command::HOME:  _scroll(-(int)total, maxtop);                    break;
                case Command::END:   _scroll(+(int)total, maxtop);                    break;
                default: break;
            }
            (void)count;
        }

        /** @} */

        /** @name Focus & update
         *  @{ */

        void deactivate() override { _active = false; }

        void focus() override {
            _active = true;
            uint16_t maxtop = _max_top();
            if (_top > maxtop) _top = maxtop;
            _request_repaint();      // switch_to cleared the screen; paint fresh
        }

        void tick(bool active) override {
            _active = active;
            if (!active) return;
            if (_painting()) { _paint_step(); return; }
            _repaint_changes();
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
        uint16_t _window() const {
            uint16_t brule = (uint16_t)(_console.rows() - 2);
            return (brule > DATA_TOP) ? (uint16_t)(brule - DATA_TOP) : 0;
        }

        uint16_t _max_top() const {
            uint16_t total = _total(), win = _window();
            return (total > win) ? (uint16_t)(total - win) : 0;
        }

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

        void _draw_row(uint16_t r, uint16_t row) {
            char line[LINE_CAP];
            uint16_t n = _row_text(r, line, sizeof(line));
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, n);
            _terminal.clear_to_eol();
        }

        void _clear_row(uint16_t row) {
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
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

        void _draw_footer() {
            uint16_t total = _total(), win = _window();
            uint16_t last  = (uint16_t)(_top + (win < total ? win : total));
            char line[LINE_CAP];
            int n = snprintf(line, sizeof(line),
                             " UP/DOWN scroll   PAGE UP/DOWN page      %u-%u / %u",
                             (unsigned)(total ? _top + 1 : 0), (unsigned)last, (unsigned)total);
            _terminal.move_to({(uint16_t)(_console.rows() - 1), 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, (n < 0) ? 0 : (uint16_t)n);
            _terminal.clear_to_eol();
        }

        // Scroll
        // --------------------------------------------------------------------

        void _scroll(int delta, uint16_t maxtop) {
            int32_t t = (int32_t)_top + delta;
            if (t < 0) t = 0;
            if (t > (int32_t)maxtop) t = (int32_t)maxtop;
            if ((uint16_t)t == _top) return;
            _top = (uint16_t)t;
            _request_repaint();      // every visible row remaps: full paced repaint
        }

        // Staged paint (paced, shares AlarmsPage's TX-floor cadence)
        // --------------------------------------------------------------------

        bool _painting() const { return _paint_i != PAINT_IDLE; }
        void _request_repaint() { _paint_i = 0; }

        /** @brief Paint one staged element: 0 = top margin, 1 = header, 2 = top
         *  rule, 3.. = data rows (each seeds its value cache), then bottom rule,
         *  then footer. Order/count must match _paint_step()'s total. */
        void _paint_element(uint16_t e, uint16_t win, uint16_t shown) {
            if (e == 0) {
                _terminal.set_cursor_visible(false);
                _clear_row(TOP_ROW);
            } else if (e == 1) {
                _draw_header();
            } else if (e == 2) {
                _draw_rule(TRULE_ROW, _top > 0 ? '^' : '+');
            } else if (e < (uint16_t)(3 + win)) {
                uint16_t vi  = (uint16_t)(e - 3);        // window row
                uint16_t row = (uint16_t)(DATA_TOP + vi);
                if (vi < shown) {
                    uint16_t r = (uint16_t)(_top + vi);
                    _draw_row(r, row);
                    if (vi < MAX_CACHE_ROWS) _value_text(r, _vcache[vi], VAL_CAP);
                } else {
                    _clear_row(row);
                }
            } else if (e == (uint16_t)(3 + win)) {
                _draw_rule((uint16_t)(_console.rows() - 2),
                           (uint16_t)(_top + win) < _total() ? 'v' : '+');
            } else {
                _draw_footer();
            }
        }

        /** @brief Advance the staged repaint: at most TX_COLD_FIELDS_PER_TICK
         *  elements this tick, only while the ring is shallow. */
        void _paint_step() {
            uint16_t win   = _window();
            uint16_t total = _total();
            uint16_t shown = (total - _top < win) ? (uint16_t)(total - _top) : win;
            uint16_t count = (uint16_t)(win + 5);   // top+header+trule+data+brule+footer
            uint16_t budget = Config::TX_COLD_FIELDS_PER_TICK;
            while (_paint_i < count && budget > 0 && _terminal.tx_below_cold()) {
                _paint_element(_paint_i, win, shown);
                _paint_i++;
                budget--;
            }
            if (_paint_i >= count) {
                _painted_top = _top;
                _paint_i = PAINT_IDLE;
            }
        }

        /** @brief Repaint only rows whose value changed (steady, no scroll). */
        void _repaint_changes() {
            if (_top != _painted_top) { _request_repaint(); return; }
            uint16_t win   = _window();
            uint16_t total = _total();
            uint16_t shown = (total - _top < win) ? (uint16_t)(total - _top) : win;
            char val[VAL_CAP];
            for (uint16_t vi = 0; vi < shown && vi < MAX_CACHE_ROWS; vi++) {
                uint16_t r = (uint16_t)(_top + vi);
                _value_text(r, val, sizeof(val));
                if (strcmp(val, _vcache[vi]) == 0) continue;
                _draw_row(r, (uint16_t)(DATA_TOP + vi));
                memcpy(_vcache[vi], val, VAL_CAP);
            }
        }
    };

}}

#endif // PAGES_MONITOR_H
