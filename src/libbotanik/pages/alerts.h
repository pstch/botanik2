#ifndef PAGES_ALERTS_H
#define PAGES_ALERTS_H

/**
 * @file alerts.h
 * @brief AlertsPage - scrollable bordered table of alert conditions.
 *
 * Layout (row 0 is the terminal status bar):
 *   row 1            blank top margin
 *   row 2            table header  (| CODE | ON | LAST EVENT | DESCRIPTION |)
 *   row 3            top rule (+----+...+); its corners cap to '^' when more above
 *   rows 4..N-5      data rows
 *   row N-4          bottom rule; corners cap to 'v' when more below
 *   row N-3          blank row above the legend
 *   row N-2          footer legend: reverse-video mode badge + counts, SPACE hint
 *   row N-1          blank row below the legend
 *
 * Rows are ordered active-first (currently-ON alerts head the list) then by most-
 * recent onset (started_ms); never-active alerts sit at the bottom. The scroll
 * position is shown in the table itself: the top / bottom rules cap their column
 * marks with '^' / 'v' when alerts are hidden above / below the window. SPACE cycles
 * the filter: recent (default - has a last event within its window) -> all defined ->
 * active only -> active and not masked (the terminal alert bar's set). A condition's
 * recent window defaults to one week, overridable via AlertCondition::recent_s. The
 * grid is plain ASCII, two-column side margin. UP/DOWN
 * (and Ctrl+P/Ctrl+N) scroll a line at a time (one scroll_region() shift + one painted
 * row); PAGE_UP / PAGE_DOWN jump a window and ENTER returns to the top, both via a full
 * repaint. The page re-checks the alert set once a second and repaints only when it
 * moved, the mode cycles, or on focus; a full repaint is paced a few rows per tick
 * under the shared TX floor, so it interleaves with the status/alert bars.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

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
        /** @brief Filter mode, cycled by SPACE (see MODE_* / _mode_label()). Defaults
         *  to RECENT: alerts with a recent last event. */
        uint8_t                   _mode     = MODE_RECENT;
        /** @brief First visible data row currently on screen (scroll offset from the
         *  top of the set). _scroll_pending below holds the not-yet-applied delta. */
        uint16_t                  _top      = 0;
        /** @brief Rows the view still has to scroll to reach where the user keyed (+ =
         *  down, - = up). A keystroke accumulates here; the actual scroll_region() is
         *  applied a row at a time only while the TX ring is shallow (see _drain_scroll),
         *  so a scroll never floods the buffer and never falls back to a full redraw. */
        int16_t                   _scroll_pending = 0;
        /** @brief Staged-repaint cursor: the next table element to paint (0 = top
         *  margin, then header, rule, the data rows, the blank line, the footer), or
         *  PAINT_IDLE when the table is fully drawn. A full repaint is paced a few rows
         *  per tick under the shared TX floor (see _paint_step()) so it never floods
         *  the ring and starves the status / alert bars; _request_repaint() restarts
         *  it from the top. */
        static constexpr uint16_t PAINT_IDLE = 0xFFFF;
        uint16_t                  _paint_i  = 0;
        /** @brief Data rows that held content in the last frame. When a repaint finds
         *  fewer, the now-vacated rows (and the bottom rule) are cleared up front so
         *  the stale rows don't linger through the paced repaint. Reset on focus (the
         *  screen was cleared on switch). */
        uint16_t                  _painted_data = 0;
        /** @brief Uptime of the last once-a-second alert-set re-check (see tick()). */
        uint64_t                  _last_refresh_ms = 0;
        /** @brief Merged raw bitset at the last repaint. Active state, last-event time
         *  and onset order all change only on a transition (raw moving), so this gates
         *  the repaint - except RECENT membership, which also expires on time; that is
         *  caught by _last_recent_sig below. */
        uint64_t                  _last_raw = 0;
        /** @brief Set of conditions in their RECENT window at the last repaint. In
         *  RECENT mode an alert can age out of the window with no raw transition, so a
         *  change here (and not in raw) is the only signal that the list must refresh. */
        uint64_t                  _last_recent_sig = 0;
        /** @brief Visible alert indices (table index order), filled by _collect(). */
        uint8_t                   _vis[Alerts::ALERT_BITS];
        /** @brief Number of visible entries in _vis. */
        uint8_t                   _nvis     = 0;

        /** @} */

        /** @name Filter modes (SPACE cycles recent -> all -> active -> unmasked)
         *  @{ */
        static constexpr uint8_t MODE_RECENT   = 0;  // has a last event, within its window
        static constexpr uint8_t MODE_ALL      = 1;  // every defined alert
        static constexpr uint8_t MODE_ACTIVE   = 2;  // active (masked or not)
        static constexpr uint8_t MODE_UNMASKED = 3;  // active and not masked
        static constexpr uint8_t MODE_COUNT    = 4;

        /** @brief Short label for the current filter mode. */
        static const char* _mode_label(uint8_t mode) {
            switch (mode) {
                case MODE_RECENT:   return "recent";
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

        /** @brief Draw a horizontal rule "+----+--+...+" matching the column widths.
         *  @p sep is the character at the column-rule positions (the corners and
         *  separators): '+' for a plain rule, or '^'/'v' to cap the table edge with
         *  scroll arrows when alerts are hidden above / below. */
        void _draw_rule(uint16_t row, char sep = '+') {
            uint16_t w[4] = { CODE_W, STATE_W, EVENT_W, _desc_w() };
            char     line[160];
            uint16_t n = 0;
            line[n++] = ' '; line[n++] = ' ';                // left margin
            if (n < sizeof(line) - 1) line[n++] = sep;
            for (uint8_t i = 0; i < 4; i++) {
                for (uint16_t k = 0; k < w[i] && n < sizeof(line) - 1; k++) line[n++] = '-';
                if (n < sizeof(line) - 1) line[n++] = sep;
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

        /** @brief Draw the footer legend: a reverse-video mode badge and counts on the
         *  left, the SPACE-cycles-mode hint on the right. Scrolling is intuitive (UP/DN)
         *  so it is not spelled out; the off-screen counts live in the flanking overflow
         *  hints instead. */
        void _draw_footer(uint16_t row, uint8_t nvis, uint8_t active) {
            const char* mode = _mode_label(_mode);
            const char* next = _mode_label((uint8_t)((_mode + 1) % MODE_COUNT));

            char left[64];
            int ln = snprintf(left, sizeof(left), "  [%s]   %u listed   %u active",
                              mode, (unsigned)nvis, (unsigned)active);
            if (ln < 0) ln = 0;
            uint16_t llen = (uint16_t)std::min((size_t)ln, sizeof(left) - 1);

            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(left, llen);
            _terminal.clear_to_eol();

            // Right-aligned "SPACE: <next mode>" hint, when it clears the left text.
            char right[24];
            int rn = snprintf(right, sizeof(right), "SPACE: %s", next);
            if (rn < 0) rn = 0;
            uint16_t cols = _terminal.cols();
            uint16_t rcol = (cols > (uint16_t)(rn + 2)) ? (uint16_t)(cols - rn - 2) : 0;
            if (rcol > llen + 2) {
                _terminal.move_to({row, rcol});
                _terminal.write_chars(right, (uint16_t)rn);
            }

            // Overdraw the mode label (between the brackets at col 3) reverse-video, so
            // the active filter reads as a badge like the table's "ON" cell.
            _terminal.move_to({row, 3});
            _terminal.set_inverted(true);
            _terminal.write_chars(mode, (uint16_t)strlen(mode));
            _terminal.set_inverted(false);
        }

        /** @brief True if @p c belongs in RECENT mode: it has a last event (edge_ms set)
         *  that is still within its window. An active alert is always recent; an inactive
         *  one expires recent_s seconds after it cleared (0 = DEFAULT_RECENT_S, <0 = never). */
        bool _is_recent(const Alerts::AlertCondition& c, uint64_t now) const {
            if (c.edge_ms == 0) return false;     // never fired -> no last event
            if (c.active)       return true;      // currently on -> always recent
            if (c.recent_s < 0) return true;      // never expires
            uint64_t window_ms = (uint64_t)(c.recent_s == 0 ? Alerts::DEFAULT_RECENT_S
                                                            : c.recent_s) * 1000u;
            return (now - c.edge_ms) <= window_ms;
        }

        /** @brief Order two visible entries: currently-active first (so ON alerts head
         *  the list in the all / recent views), then by most-recent onset. */
        bool _before(const Alerts::AlertCondition& x, const Alerts::AlertCondition& y) const {
            if (x.active != y.active) return x.active;     // active alerts first
            return x.started_ms > y.started_ms;           // then most-recent onset
        }

        /** @brief Fill _vis/_nvis with the indices shown in the current mode, ordered
         *  active-first then by most-recent onset (never-active alerts, started_ms == 0,
         *  sort to the bottom). */
        void _collect() {
            _nvis = 0;
            if (_model == nullptr) return;
            uint64_t now = HAL::uptime_ms();
            for (uint8_t i = 0; i < _model->count() && _nvis < Alerts::ALERT_BITS; i++) {
                const Alerts::AlertCondition& c = _model->condition(i);
                if (c.code == nullptr) continue;                 // undefined table slot
                bool include;
                switch (_mode) {
                    case MODE_RECENT:   include = _is_recent(c, now);     break;
                    case MODE_ACTIVE:   include = c.active;               break;
                    case MODE_UNMASKED: include = c.active && c.unmasked; break;
                    default:            include = true;                   break;  // MODE_ALL
                }
                if (include) _vis[_nvis++] = i;
            }
            // Insertion sort by _before(). The set is small (<= the defined-alert count)
            // and nearly always tiny, so this beats pulling in a general sort, and it is
            // stable in table order on ties.
            for (uint8_t a = 1; a < _nvis; a++) {
                uint8_t v = _vis[a];
                const Alerts::AlertCondition& cv = _model->condition(v);
                int16_t b = (int16_t)a - 1;
                while (b >= 0 && _before(cv, _model->condition(_vis[b]))) {
                    _vis[b + 1] = _vis[b];
                    b--;
                }
                _vis[b + 1] = v;
            }
        }

        /** @brief Snapshot the change signature after a repaint. */
        /** @brief Bitmask of conditions currently within their RECENT window, used to
         *  detect the time-based expiry that leaves raw() unchanged. */
        uint64_t _recent_sig() const {
            if (_model == nullptr) return 0;
            uint64_t now = HAL::uptime_ms(), m = 0;
            for (uint8_t i = 0; i < _model->count(); i++) {
                const Alerts::AlertCondition& c = _model->condition(i);
                if (c.code != nullptr && _is_recent(c, now)) m |= Alerts::alert_bit(i);
            }
            return m;
        }

        void _snapshot() {
            _last_raw = _model ? _model->raw() : 0;
            _last_recent_sig = _recent_sig();
        }

        /** @brief True if the displayed set moved since the last repaint (so a steady,
         *  scrolled view is left untouched). raw() covers active/last-event changes; in
         *  RECENT mode a row can also expire purely on time, with no raw move, so the
         *  recent membership is compared too. */
        bool _changed() const {
            if (_model == nullptr) return false;
            if (_model->raw() != _last_raw) return true;
            return _mode == MODE_RECENT && _recent_sig() != _last_recent_sig;
        }

        /** @name Window geometry
         *  Bottom-anchored on the content area (rows 1..rows()-1, above the terminal
         *  alert bar). From the bottom up: free row, legend, free row, bottom rule,
         *  then the data band up to the top rule. The two free rows give the legend
         *  breathing room; the rules carry the scroll arrows (see _paint_element()).
         *  @{ */
        uint16_t _legend_row()  const { return (uint16_t)(_terminal.rows() - 2); }  // free row below at rows()-1
        uint16_t _brule_row()   const { return (uint16_t)(_terminal.rows() - 4); }  // bottom rule (free row above legend at rows()-3)
        uint16_t _data_bottom() const { return (uint16_t)(_terminal.rows() - 5); }
        /** @brief Number of data rows that fit, or 0 if the screen is too short. */
        uint16_t _window() const {
            uint16_t r = _terminal.rows();
            return (r >= (uint16_t)(DATA_TOP + 5)) ? (uint16_t)(r - DATA_TOP - 4) : 0;
        }
        /** @brief Entries scrolled off the top / bottom of the visible window. */
        uint16_t _hidden_above() const { return _top; }
        uint16_t _hidden_below(uint16_t window) const {
            uint16_t end = (uint16_t)(_top + window);
            return (_nvis > end) ? (uint16_t)(_nvis - end) : 0;
        }
        /** @brief Redraw the top/bottom rules, capping them with '^'/'v' scroll arrows
         *  when alerts are hidden above / below the window (plain '+' otherwise). */
        void _draw_rules(uint16_t window) {
            _draw_rule(RULE_ROW,     _hidden_above()       ? '^' : '+');
            _draw_rule(_brule_row(), _hidden_below(window) ? 'v' : '+');
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
        /** @brief Request a full (paced) table repaint from the top. A repaint draws the
         *  whole table at _top, so any deferred scroll is superseded - drop it. */
        void _request_repaint() { _scroll_pending = 0; _paint_i = 0; }

        /** @brief Fold a deferred scroll into _top (clamped) and repaint - used when the
         *  set moved or a paint is mid-flight, so the screen is stale and a one-row scroll
         *  would be wrong. */
        void _fold_pending_and_repaint() {
            uint16_t window = _window();
            uint16_t maxtop = (_nvis > window) ? (uint16_t)(_nvis - window) : 0;
            int32_t  d = (int32_t)_top + _scroll_pending;
            _top = (uint16_t)(d < 0 ? 0 : (d > (int32_t)maxtop ? (int32_t)maxtop : d));
            _request_repaint();   // clears _scroll_pending
        }

        /** @brief Apply the deferred scroll a row at a time, but only while the TX ring
         *  is shallow (the shared cold floor): one scroll_region() shift + the newly
         *  exposed row fit easily under that floor, so the buffer never fills and the
         *  scroll never has to fall back to a full repaint. Stops (deferring the rest)
         *  the moment the ring is no longer shallow; tick() resumes it as TX drains. */
        void _drain_scroll() {
            uint16_t window = _window();
            if (window == 0) { _scroll_pending = 0; return; }
            uint16_t data_bottom = _data_bottom();
            uint16_t maxtop = (_nvis > window) ? (uint16_t)(_nvis - window) : 0;
            while (_scroll_pending != 0 && _terminal.tx_below_cold()) {
                bool     down    = _scroll_pending > 0;
                uint16_t old_top = _top;
                _terminal.scroll_region(DATA_TOP, data_bottom, /*up=*/down, 1);
                _top = down ? (uint16_t)(_top + 1) : (uint16_t)(_top - 1);
                if (down) _draw_data_row(data_bottom, (uint16_t)(_top + window - 1));
                else      _draw_data_row(DATA_TOP, _top);
                _scroll_pending = (int16_t)(_scroll_pending + (down ? -1 : 1));
                // Repaint a rule only when its scroll arrow actually flips (top arrow at
                // the 0 boundary, bottom arrow at maxtop).
                if ((old_top == 0) != (_top == 0))
                    _draw_rule(RULE_ROW, _top ? '^' : '+');
                if ((old_top < maxtop) != (_top < maxtop))
                    _draw_rule(_brule_row(), (_top < maxtop) ? 'v' : '+');
            }
        }

        /** @brief True while a staged repaint is in progress (or pending its first tick). */
        bool _painting() const { return _paint_i != PAINT_IDLE; }

        /** @brief Paint one table element by its index in the staged sequence: 0 = the
         *  legend (painted first so a mode change reads instantly, and only once - no
         *  flash), then top margin, header, top rule, [4, 4+window) data rows, the bottom
         *  rule, the blank row above the legend, the blank row below it. Element
         *  order/count must match _paint_step()'s total (window + 7). */
        void _paint_element(uint16_t e, uint16_t window) {
            uint16_t legend = _legend_row();
            if      (e == 0)               _draw_footer(legend, _nvis,          // legend first
                                                        _model ? _model->active_count() : 0);
            else if (e == 1)               _clear_row(1);                       // top margin
            else if (e == 2)               _draw_grid_row(HEADER_ROW, "CODE", "ON",
                                                          "LAST EVENT", "DESCRIPTION");
            else if (e == 3)               _draw_rule(RULE_ROW, _hidden_above() ? '^' : '+');
            else if (e < (uint16_t)(4 + window))
                _draw_data_row((uint16_t)(DATA_TOP + (e - 4)), (uint16_t)(_top + (e - 4)));
            else if (e == (uint16_t)(4 + window))
                _draw_rule(_brule_row(), _hidden_below(window) ? 'v' : '+');     // bottom rule
            else if (e == (uint16_t)(5 + window))
                _clear_row((uint16_t)(legend - 1));                             // free row above legend
            else
                _clear_row((uint16_t)(legend + 1));                             // free row below legend
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
            // margin + header + top rule + data + bottom rule + free + legend + free
            uint16_t total = (uint16_t)(window + 7);

            if (_paint_i == 0) {
                _collect();
                uint16_t maxtop = (_nvis > window) ? (uint16_t)(_nvis - window) : 0;
                if (_top > maxtop) _top = maxtop;
                // Snapshot the signature of the set being painted up front: a change
                // during or after staging then reads as _changed() and re-triggers,
                // rather than being masked by a completion-time snapshot of a newer set.
                _snapshot();
                // If the set shrank, clear the rows that held data and are now empty
                // (and the bottom rule) before the paced repaint, so they do not linger
                // as stale content while the redraw walks down to them.
                uint16_t content = (_nvis < window) ? _nvis : window;
                if (content < _painted_data)
                    _terminal.clear_rows((uint16_t)(DATA_TOP + content), _brule_row());
                _painted_data = content;
            }

            uint16_t budget = Config::TX_COLD_FIELDS_PER_TICK;
            while (_paint_i < total && budget > 0 && _terminal.tx_below_cold()) {
                _paint_element(_paint_i, window);
                _paint_i++;
                budget--;
            }
            if (_paint_i >= total) _paint_i = PAINT_IDLE;   // frame complete
        }

        /** @brief Queue a one-line scroll. Scrolling does not change the visible set, so
         *  it reuses _vis/_nvis from the last paint: it just accumulates the move into
         *  _scroll_pending (clamped to the set) and lets _drain_scroll() apply it under
         *  the TX floor. If a repaint is already due (staged, or the set moved/expired),
         *  the pending move is folded into a full repaint instead of scrolling a stale
         *  screen. */
        void _scroll(bool down) {
            uint16_t window = _window();
            if (window == 0) return;
            uint16_t maxtop = (_nvis > window) ? (uint16_t)(_nvis - window) : 0;
            // Where the view will sit after this key: on-screen top + already-pending
            // + this step, clamped to the set.
            int32_t desired = (int32_t)_top + _scroll_pending + (down ? 1 : -1);
            if (desired < 0) desired = 0;
            else if (desired > (int32_t)maxtop) desired = (int32_t)maxtop;
            int16_t pending = (int16_t)(desired - (int32_t)_top);
            if (pending == _scroll_pending) return;        // already at that end
            _scroll_pending = pending;

            if (_painting() || _changed()) { _fold_pending_and_repaint(); return; }
            _drain_scroll();   // apply now if the ring is shallow; tick() finishes the rest
        }

        /** @brief Jump the view by a full page (window height) and repaint. A page is a
         *  large move, so it goes through the paced full repaint (which re-collects and
         *  re-clamps _top) rather than a burst of one-row scroll_region() shifts. */
        void _page_scroll(bool down) {
            uint16_t window = _window();
            if (window == 0) return;
            uint16_t maxtop = (_nvis > window) ? (uint16_t)(_nvis - window) : 0;
            int32_t  cur = (int32_t)_top + _scroll_pending;   // where the user has keyed to
            int32_t  nt  = down ? (cur + window) : (cur - window);
            if (nt < 0) nt = 0; else if (nt > (int32_t)maxtop) nt = (int32_t)maxtop;
            if (nt == cur) return;                            // already at that end
            _top = (uint16_t)nt;
            _request_repaint();                               // clears _scroll_pending
        }

    public:
        /** @brief Bind to a terminal and alert model. */
        AlertsPage(AnsiTerminal& terminal, const Alerts::AlertModel* model)
            : _terminal(terminal), _model(model) {}

        /** @name Page interface
         *  @{ */

        const char* name() const override { return "ALERTS"; }

        void handle_text(char c) override {
            if (c == ' ') {                 // cycle recent -> all -> active -> unmasked
                _mode = (uint8_t)((_mode + 1) % MODE_COUNT);
                _top  = 0;
                _request_repaint();         // legend is the first element painted (reactive)
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
                case Command::PAGE_UP:
                    _page_scroll(/*down=*/false);
                    break;
                case Command::PAGE_DOWN:
                    _page_scroll(/*down=*/true);
                    break;
                default: break;
            }
        }

        void deactivate() override { _active = false; }

        void focus() override {
            _active = true;
            _painted_data = 0;    // switch_to cleared the screen; nothing painted yet
            _request_repaint();   // repaint from scratch
        }

        void tick(bool active) override {
            _active = active;
            if (!active) return;
            // A staged repaint (from focus / mode / a moved set) advances a few rows per
            // tick under the TX floor.
            if (_painting()) { _paint_step(); return; }
            // Finish any deferred scroll as TX drains; if the set moved while it waited,
            // fold the remaining move into a full repaint instead of scrolling stale rows.
            if (_scroll_pending != 0) {
                if (_changed()) _fold_pending_and_repaint();
                else            _drain_scroll();
                return;
            }
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
