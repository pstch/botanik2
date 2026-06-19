#ifndef PAGES_ALERTS_H
#define PAGES_ALERTS_H

/**
 * @file alerts.h
 * @brief AlertsPage - scrollable bordered table of alert conditions.
 *
 * Layout (row 0 is the terminal status bar):
 *   row 1            blank top margin
 *   row 2            table header  (| CODE | ON | LAST EVENT | DESCRIPTION |)
 *   row 3            header rule   (+------+----+------------+ ... +)
 *   rows 4..N-3      data rows (scrolled a line at a time via scroll_region)
 *   row N-2          blank line below the table
 *   row N-1          footer (mode, counts, key hints)
 *
 * Rows are ordered by most-recent onset (started_ms), so the alert that fired most
 * recently is at the top and never-active alerts sit at the bottom. SPACE cycles
 * the filter: all defined alerts -> active only -> active and not masked (the
 * terminal alert bar's set). The grid is plain ASCII: '|' column rules and a
 * '+'/'-' header rule, with a two-column side margin. UP/DOWN and Ctrl+P/Ctrl+N
 * scroll a line at a time (one scroll_region() shift + one painted row). The page
 * re-checks the alert set once a second and repaints only when it moved, the mode
 * cycles, or on focus; a full repaint is paced a few rows per tick under the shared
 * TX floor, so it interleaves with the status/alert bars instead of flooding TX.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "libbotanik_config.h"
#include "libbotanik/alerts/alerts.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/terminal/terminal.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen scrollable alert table (see file header). */
    class AlertsPage : public Page {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Owning terminal. */
        AnsiTerminal&             _terminal;
        /** @brief Alert source (may be null). */
        const Alerts::AlertModel* _model;
        /** @brief Mirrors the latest tick(active). */
        bool                      _active   = false;
        /** @brief Filter mode, cycled by SPACE: 0 = every defined alert, 1 = active
         *  only, 2 = active and not masked. See MODE_* and _mode_label(). */
        uint8_t                   _mode     = MODE_ALL;
        /** @brief First visible data row (scroll offset from the top of the set). */
        uint16_t                  _top      = 0;
        /** @brief Staged-repaint cursor: the next table element to paint (0 = top
         *  margin, then header, rule, the data rows, the blank line, the footer), or
         *  PAINT_IDLE when the table is fully drawn. A full repaint is paced a few rows
         *  per tick under the shared TX floor (see _paint_step()) so it never floods
         *  the ring and starves the status / alert bars; _request_repaint() restarts
         *  it from the top. */
        static constexpr uint16_t PAINT_IDLE = 0xFFFF;
        uint16_t                  _paint_i  = 0;
        /** @brief Uptime of the last once-a-second alert-set re-check (see tick()). */
        uint64_t                  _last_refresh_ms = 0;
        /** @brief Merged raw bitset at the last repaint. Every displayed field (active
         *  state, last-event time, onset order) changes only on a transition, i.e. when
         *  raw moves, so this one signature gates the repaint. */
        uint64_t                  _last_raw = 0;
        /** @brief Visible alert indices (table index order), filled by _collect(). */
        uint8_t                   _vis[Alerts::ALERT_BITS];
        /** @brief Number of visible entries in _vis. */
        uint8_t                   _nvis     = 0;

        /** @} */

        /** @name Filter modes (SPACE cycles MODE_ALL -> MODE_ACTIVE -> MODE_UNMASKED)
         *  @{ */
        static constexpr uint8_t MODE_ALL      = 0;  // every defined alert
        static constexpr uint8_t MODE_ACTIVE   = 1;  // active (masked or not)
        static constexpr uint8_t MODE_UNMASKED = 2;  // active and not masked
        static constexpr uint8_t MODE_COUNT    = 3;

        /** @brief Short label for the current filter mode. */
        static const char* _mode_label(uint8_t mode) {
            switch (mode) {
                case MODE_ACTIVE:   return "active";
                case MODE_UNMASKED: return "unmasked";
                default:            return "all";
            }
        }
        /** @} */

        /** @name Layout
         *  Columns: CODE | ON (reverse-video badge when active) | LAST EVENT
         *  (the last transition's timestamp, "YY/MM/DD HH:MM") | DESCRIPTION.
         *  @{ */
        static constexpr uint16_t MARGIN     = 2;   // blank columns each side
        static constexpr uint16_t CODE_W     = 10;  // cell inner widths (incl. 1 pad)
        static constexpr uint16_t STATE_W    = 4;   // " ON" badge / "ON" header
        static constexpr uint16_t EVENT_W    = 16;  // " YY/MM/DD HH:MM"
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t RULE_ROW   = 3;
        static constexpr uint16_t DATA_TOP   = 4;
        /** @brief Screen column where the state cell's text starts, tracking the
         *  layout _draw_grid_row() emits: margin + "| " + CODE(inner) + "| ". The
         *  reverse-video "ON" badge is overdrawn here on the otherwise-normal row. */
        static constexpr uint16_t STATE_TEXT_COL = MARGIN + 2 + (CODE_W - 1) + 2;
        /** @} */

        /** @brief Description-column width: the row remainder, clamped. */
        uint16_t _desc_w() const {
            uint16_t cols  = _terminal.cols();
            uint16_t fixed = (uint16_t)(5 + CODE_W + STATE_W + EVENT_W);  // 5 rules + fixed cells
            uint16_t avail = (cols > (uint16_t)(2 * MARGIN + fixed))
                                 ? (uint16_t)(cols - 2 * MARGIN - fixed) : 8;
            if (avail < 8)  avail = 8;
            if (avail > 70) avail = 70;
            return avail;
        }

        /** @brief Format the last-event cell for @p c: the wall-clock timestamp of the
         *  last transition (edge_ms) as "YY/MM/DD HH:MM", or empty if it never fired.
         *  Whether that event was an ON or OFF transition is shown by the ON column. */
        static void _fmt_event(char* buf, size_t cap, const Alerts::AlertCondition& c) {
            if (!c.active && c.edge_ms == 0) { if (cap) buf[0] = '\0'; return; }
            Utils::CalendarTime t = Utils::cal_time_ms(c.edge_ms);
            snprintf(buf, cap, "%02u/%02u/%02u %02u:%02u",
                     (unsigned)(t.year % 100), (unsigned)t.month, (unsigned)t.day,
                     (unsigned)t.hour, (unsigned)t.minute);
        }

        /** @brief Draw one '|'-ruled grid row: margin, then each cell wrapped in '|'
         *  rules, cell text padded to its column width. */
        void _draw_grid_row(uint16_t row, const char* c0, const char* c1,
                            const char* c2, const char* c3) {
            const char* cell[4] = { c0, c1, c2, c3 };
            uint16_t    w[4]    = { CODE_W, STATE_W, EVENT_W, _desc_w() };
            char     line[160];
            uint16_t n = 0;
            line[n++] = ' '; line[n++] = ' ';                // left margin
            for (uint8_t i = 0; i < 4; i++) {
                if (n < sizeof(line) - 1) line[n++] = '|';
                int cw = (int)w[i] - 1;
                int m  = snprintf(line + n, sizeof(line) - n, " %-*.*s",
                                  cw, cw, cell[i] ? cell[i] : "");
                if (m < 0) m = 0;
                n = (uint16_t)(n + m);
                if (n > sizeof(line) - 1) { n = (uint16_t)(sizeof(line) - 1); break; }
            }
            if (n < sizeof(line) - 1) line[n++] = '|';        // closing rule
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, n);
            _terminal.clear_to_eol();
        }

        /** @brief Draw the header rule: "+----+--+...+" matching the column widths. */
        void _draw_rule(uint16_t row) {
            uint16_t w[4] = { CODE_W, STATE_W, EVENT_W, _desc_w() };
            char     line[160];
            uint16_t n = 0;
            line[n++] = ' '; line[n++] = ' ';                // left margin
            if (n < sizeof(line) - 1) line[n++] = '+';
            for (uint8_t i = 0; i < 4; i++) {
                for (uint16_t k = 0; k < w[i] && n < sizeof(line) - 1; k++) line[n++] = '-';
                if (n < sizeof(line) - 1) line[n++] = '+';
            }
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, n);
            _terminal.clear_to_eol();
        }

        /** @brief Blank a row. */
        void _clear_row(uint16_t row) {
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.clear_to_eol();
        }

        /** @brief Draw the footer line: mode, counts, and key hints. */
        void _draw_footer(uint16_t row, uint8_t nvis, uint8_t active) {
            char buf[96];
            uint8_t next = (uint8_t)((_mode + 1) % MODE_COUNT);
            int n = snprintf(buf, sizeof(buf),
                             "  [%s]  %u shown / %u active   SPACE: %s   UP/DN: scroll",
                             _mode_label(_mode), (unsigned)nvis, (unsigned)active,
                             _mode_label(next));
            if (n < 0) n = 0;
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(buf, (uint16_t)std::min((size_t)n, sizeof(buf) - 1));
            _terminal.clear_to_eol();
        }

        /** @brief Fill _vis/_nvis with the indices shown in the current mode, ordered
         *  by most-recent onset (started_ms descending; never-active alerts, which
         *  keep started_ms == 0, sort to the bottom). */
        void _collect() {
            _nvis = 0;
            if (_model == nullptr) return;
            for (uint8_t i = 0; i < _model->count() && _nvis < Alerts::ALERT_BITS; i++) {
                const Alerts::AlertCondition& c = _model->condition(i);
                if (c.code == nullptr) continue;                 // undefined table slot
                if (_mode >= MODE_ACTIVE   && !c.active)   continue;
                if (_mode >= MODE_UNMASKED && !c.unmasked) continue;
                _vis[_nvis++] = i;
            }
            // Insertion sort by onset, most recent first. The set is small (<= the
            // defined-alert count) and nearly always tiny, so this is cheaper than
            // pulling in a general sort, and it is stable in table order on ties.
            for (uint8_t a = 1; a < _nvis; a++) {
                uint8_t  v  = _vis[a];
                uint64_t sa = _model->condition(v).started_ms;
                int16_t  b  = (int16_t)a - 1;
                while (b >= 0 && _model->condition(_vis[b]).started_ms < sa) {
                    _vis[b + 1] = _vis[b];
                    b--;
                }
                _vis[b + 1] = v;
            }
        }

        /** @brief Snapshot the change signature after a repaint. */
        void _snapshot() { _last_raw = _model ? _model->raw() : 0; }

        /** @brief True if the alert set moved since the last repaint (so a steady,
         *  scrolled view is left untouched). */
        bool _changed() const {
            return _model != nullptr && _model->raw() != _last_raw;
        }

        /** @name Window geometry
         *  Data rows occupy [DATA_TOP .. _data_bottom()]; one blank line sits below
         *  the table, then the footer on the last content row.
         *  @{ */
        uint16_t _footer_row()  const { return (uint16_t)(_terminal.rows() - 1); }
        uint16_t _data_bottom() const { return (uint16_t)(_footer_row() - 2); }  // blank line above footer
        /** @brief Number of data rows that fit, or 0 if the screen is too short. */
        uint16_t _window() const {
            uint16_t fr = _footer_row();
            return (fr > (uint16_t)(DATA_TOP + 1)) ? (uint16_t)(fr - 1 - DATA_TOP) : 0;
        }
        /** @} */

        /** @brief Draw data row at screen @p row for visible entry @p vi (index into
         *  _vis), or clear the row when @p vi is past the end of the set. The state
         *  cell shows a reverse-video "ON" badge while active (blank otherwise): the
         *  row is drawn normal, then the badge overdrawn inverted in place. */
        void _draw_data_row(uint16_t row, uint16_t vi) {
            if (vi >= _nvis) { _clear_row(row); return; }
            const Alerts::AlertCondition& c = _model->condition(_vis[vi]);
            char ev[16];
            _fmt_event(ev, sizeof(ev), c);
            _draw_grid_row(row, c.code ? c.code : "?", c.active ? "ON" : "",
                           ev, c.desc ? c.desc : "");
            if (c.active) {
                _terminal.move_to({row, STATE_TEXT_COL});
                _terminal.set_inverted(true);
                _terminal.write_chars("ON", 2);
                _terminal.set_inverted(false);
            }
        }

        /** @brief Request a full (paced) table repaint from the top. */
        void _request_repaint() { _paint_i = 0; }

        /** @brief True while a staged repaint is in progress (or pending its first tick). */
        bool _painting() const { return _paint_i != PAINT_IDLE; }

        /** @brief Paint one table element by its index in the staged sequence: 0 = top
         *  margin, 1 = header, 2 = rule, [3, 3+window) = data rows, then the blank line
         *  and the footer. Element order/count must match _paint_step()'s total. */
        void _paint_element(uint16_t e, uint16_t window, uint16_t footer_row) {
            if      (e == 0)               _clear_row(1);                       // top margin
            else if (e == 1)               _draw_grid_row(HEADER_ROW, "CODE", "ON",
                                                          "LAST EVENT", "DESCRIPTION");
            else if (e == 2)               _draw_rule(RULE_ROW);
            else if (e < (uint16_t)(3 + window))
                _draw_data_row((uint16_t)(DATA_TOP + (e - 3)), (uint16_t)(_top + (e - 3)));
            else if (e == (uint16_t)(3 + window))
                _clear_row((uint16_t)(footer_row - 1));                         // blank line below table
            else
                _draw_footer(footer_row, _nvis, _model ? _model->active_count() : 0);
        }

        /** @brief Advance the staged repaint: draw at most TX_COLD_FIELDS_PER_TICK
         *  table rows this tick, and only while the TX ring is shallow, so a full
         *  repaint spreads over several ticks instead of flooding the ring (the same
         *  cooperative scheme the status-page fields use). The view set is collected
         *  and the offset clamped once, on the first element, so the staged frame stays
         *  self-consistent; a mid-paint set change is caught by the next 1 s re-check. */
        void _paint_step() {
            uint16_t window = _window();
            if (window == 0) { _snapshot(); _paint_i = PAINT_IDLE; return; }
            uint16_t footer_row = _footer_row();
            uint16_t total      = (uint16_t)(window + 5);   // margin+header+rule+data+blank+footer

            if (_paint_i == 0) {
                _collect();
                uint16_t maxtop = (_nvis > window) ? (uint16_t)(_nvis - window) : 0;
                if (_top > maxtop) _top = maxtop;
                // Snapshot the signature of the set being painted up front: a change
                // during or after staging then reads as _changed() and re-triggers,
                // rather than being masked by a completion-time snapshot of a newer set.
                _snapshot();
            }

            uint16_t budget = Config::TX_COLD_FIELDS_PER_TICK;
            while (_paint_i < total && budget > 0 && _terminal.tx_below_cold()) {
                _paint_element(_paint_i, window, footer_row);
                _paint_i++;
                budget--;
            }
            if (_paint_i >= total) _paint_i = PAINT_IDLE;   // frame complete
        }

        /** @brief Scroll the view by one line, painting only the newly exposed data
         *  row via scroll_region() instead of repainting the table. Defers to a full
         *  repaint only when one is already due (the set moved, or focus/mode cleared
         *  the screen) - scrolling a stale screen would be wrong. The single-line emit
         *  is tiny and the ring is shallow at a keystroke, so it is not gated on TX
         *  depth (a full repaint would be more work, not less). */
        void _scroll(bool down) {
            uint16_t window = _window();
            if (window == 0) return;
            uint16_t data_bottom = _data_bottom();

            _collect();   // current set: clamps _top and supplies the exposed row
            uint16_t maxtop  = (_nvis > window) ? (uint16_t)(_nvis - window) : 0;
            uint16_t old_top = _top;
            if (down) { if (_top < maxtop) _top++; }
            else      { if (_top > 0)      _top--; }
            if (_top == old_top) return;                   // already at that end

            // A staged repaint in progress, or a moved set, means the on-screen table
            // is stale or about to be redrawn - fold the new offset into a full repaint
            // rather than scroll a stale screen.
            if (_painting() || _changed()) { _request_repaint(); return; }

            if (down) {
                // View moved down: shift the band up, paint the new bottom row.
                _terminal.scroll_region(DATA_TOP, data_bottom, /*up=*/true, 1);
                _draw_data_row(data_bottom, (uint16_t)(_top + window - 1));
            } else {
                // View moved up: shift the band down, paint the new top row.
                _terminal.scroll_region(DATA_TOP, data_bottom, /*up=*/false, 1);
                _draw_data_row(DATA_TOP, _top);
            }
        }

    public:
        /** @brief Bind to a terminal and alert model. */
        AlertsPage(AnsiTerminal& terminal, const Alerts::AlertModel* model)
            : _terminal(terminal), _model(model) {}

        /** @name Page interface
         *  @{ */

        const char* name() const override { return "ALERTS"; }

        void handle_text(char c) override {
            if (c == ' ') {                 // cycle all -> active -> unmasked
                _mode = (uint8_t)((_mode + 1) % MODE_COUNT);
                _top  = 0;
                _request_repaint();
            }
        }

        void handle_command(Command cmd, uint16_t = 1) override {
            switch (cmd) {
                case Command::ENTER:
                    _top = 0; _request_repaint();       // jump to the top (full repaint)
                    break;
                case Command::UP:
                case Command::CTRL_P:
                    _scroll(/*down=*/false);            // incremental scroll_region
                    break;
                case Command::DOWN:
                case Command::CTRL_N:
                    _scroll(/*down=*/true);
                    break;
                default: break;
            }
        }

        void deactivate() override { _active = false; }

        void focus() override {
            _active = true;
            _request_repaint();   // screen was cleared on switch: repaint from scratch
        }

        void tick(bool active) override {
            _active = active;
            if (!active) return;
            // A staged repaint (from focus / mode / a moved set) advances a few rows
            // per tick under the TX floor; a scroll paints itself in handle_command.
            if (_painting()) { _paint_step(); return; }
            // Idle: re-check the alert set once a second - alerts move slowly, so
            // polling every tick would just burn cycles - and start a repaint only when
            // it actually moved (usually a no-op).
            uint64_t now = HAL::uptime_ms();
            if (now - _last_refresh_ms < Config::PAGE_HOT_MS) return;
            _last_refresh_ms = now;
            if (_changed()) _request_repaint();
        }

        /** @} */
    };

}}

#endif // PAGES_ALERTS_H
