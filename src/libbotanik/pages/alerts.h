#ifndef PAGES_ALERTS_H
#define PAGES_ALERTS_H

/**
 * @file alerts.h
 * @brief AlertsPage - scrollable bordered table of alert conditions.
 *
 * Layout (row 0 is the terminal status bar):
 *   row 1            blank top margin
 *   row 2            table header  (| CODE | ON | OFF | DESCRIPTION |)
 *   row 3            header rule   (+------+----+----+ ... +)
 *   rows 4..N-3      data rows (scrolled a line at a time via scroll_region)
 *   row N-2          blank line below the table
 *   row N-1          footer (mode, counts, key hints)
 *
 * Rows are ordered by most-recent onset (started_ms), so the alert that fired most
 * recently is at the top and never-active alerts sit at the bottom. SPACE cycles
 * the filter: all defined alerts -> active only -> active and not masked (the
 * terminal alert bar's set). The grid is plain ASCII: '|' column rules and a
 * '+'/'-' header rule, with a two-column side margin. UP/DOWN and Ctrl+P/Ctrl+N
 * scroll a line at a time. The page repaints only when the alert set / its notify
 * flags change, the mode cycles, or the view scrolls, and only while the TX ring
 * is shallow.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "libbotanik/alerts/alerts.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/terminal/terminal.h"

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
        /** @brief A repaint is due (set on focus/scroll/toggle, cleared by _repaint). */
        bool                      _dirty    = true;
        /** @name Change signature (repaint only when the set / its flags move)
         *  @{ */
        uint64_t                  _last_raw = 0;
        uint64_t                  _last_on  = 0;
        uint64_t                  _last_off = 0;
        /** @} */
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
         *  @{ */
        static constexpr uint16_t MARGIN     = 2;   // blank columns each side
        static constexpr uint16_t CODE_W     = 11;  // cell inner widths (incl. 1 pad)
        static constexpr uint16_t ON_W       = 5;
        static constexpr uint16_t OFF_W      = 5;
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t RULE_ROW   = 3;
        static constexpr uint16_t DATA_TOP   = 4;
        /** @} */

        /** @brief Description-column width: the row remainder, clamped. */
        uint16_t _desc_w() const {
            uint16_t cols  = _terminal.cols();
            uint16_t fixed = (uint16_t)(5 + CODE_W + ON_W + OFF_W);   // 5 rules + fixed cells
            uint16_t avail = (cols > (uint16_t)(2 * MARGIN + fixed))
                                 ? (uint16_t)(cols - 2 * MARGIN - fixed) : 8;
            if (avail < 8)  avail = 8;
            if (avail > 70) avail = 70;
            return avail;
        }

        /** @brief Draw one '|'-ruled grid row: margin, then each cell wrapped in '|'
         *  rules, cell text padded to its column width. */
        void _draw_grid_row(uint16_t row, const char* c0, const char* c1,
                            const char* c2, const char* c3) {
            const char* cell[4] = { c0, c1, c2, c3 };
            uint16_t    w[4]    = { CODE_W, ON_W, OFF_W, _desc_w() };
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
            uint16_t w[4] = { CODE_W, ON_W, OFF_W, _desc_w() };
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
        void _snapshot() {
            _last_raw = _model ? _model->raw() : 0;
            _last_on  = 0;
            _last_off = 0;
            if (_model)
                for (uint8_t i = 0; i < _model->count(); i++) {
                    const Alerts::AlertCondition& c = _model->condition(i);
                    if (!c.active) continue;
                    if (c.on_notified)  _last_on  |= Alerts::alert_bit(i);
                    if (c.off_notified) _last_off |= Alerts::alert_bit(i);
                }
        }

        /** @brief True if the active set or any ON/OFF flag changed since the last
         *  repaint (so a steady, scrolled view is left untouched). */
        bool _changed() const {
            if (_model == nullptr) return false;
            uint64_t on = 0, off = 0;
            for (uint8_t i = 0; i < _model->count(); i++) {
                const Alerts::AlertCondition& c = _model->condition(i);
                if (!c.active) continue;
                if (c.on_notified)  on  |= Alerts::alert_bit(i);
                if (c.off_notified) off |= Alerts::alert_bit(i);
            }
            return _model->raw() != _last_raw || on != _last_on || off != _last_off;
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
         *  _vis), or clear the row when @p vi is past the end of the set. */
        void _draw_data_row(uint16_t row, uint16_t vi) {
            if (vi < _nvis) {
                const Alerts::AlertCondition& c = _model->condition(_vis[vi]);
                char on[2]  = { (char)(c.on_notified  ? 'Y' : '-'), '\0' };
                char off[2] = { (char)(c.off_notified ? 'Y' : '-'), '\0' };
                _draw_grid_row(row, c.code ? c.code : "?", on, off, c.desc ? c.desc : "");
            } else {
                _clear_row(row);
            }
        }

        /** @brief Repaint the whole table (header, rule, data window, footer). */
        void _repaint() {
            uint16_t window = _window();
            if (window == 0) return;                       // screen too short for a row
            uint16_t footer_row = _footer_row();

            _collect();
            uint16_t maxtop = (_nvis > window) ? (uint16_t)(_nvis - window) : 0;
            if (_top > maxtop) _top = maxtop;

            _clear_row(1);                                              // blank top margin
            _draw_grid_row(HEADER_ROW, "CODE", "ON", "OFF", "DESCRIPTION");
            _draw_rule(RULE_ROW);
            for (uint16_t r = 0; r < window; r++)
                _draw_data_row((uint16_t)(DATA_TOP + r), (uint16_t)(_top + r));
            _clear_row((uint16_t)(footer_row - 1));                     // blank line below the table
            _draw_footer(footer_row, _nvis, _model ? _model->active_count() : 0);

            _snapshot();
            _dirty = false;
        }

        /** @brief Scroll the view by one line, painting only the newly exposed data
         *  row via scroll_region() instead of repainting the table. Falls back to a
         *  full repaint (sets _dirty) when one is already due, the set moved, or the
         *  TX ring is busy - any of which would tear a one-row incremental paint. */
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

            if (_dirty || _changed() || !_terminal.tx_below_cold()) { _dirty = true; return; }

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
                _mode  = (uint8_t)((_mode + 1) % MODE_COUNT);
                _top   = 0;
                _dirty = true;
            }
        }

        void handle_command(Command cmd, uint16_t = 1) override {
            switch (cmd) {
                case Command::ENTER:
                    _top = 0; _dirty = true;            // jump to the top (full repaint)
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
            _dirty  = true;   // screen was cleared on switch: repaint from scratch
        }

        void tick(bool active) override {
            _active = active;
            if (!active) return;
            if (!_dirty && !_changed()) return;
            if (!_terminal.tx_below_cold()) return;   // cooperative: wait for a shallow ring
            _repaint();
        }

        /** @} */
    };

}}

#endif // PAGES_ALERTS_H
