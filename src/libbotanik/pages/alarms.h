// REVIEW_35: TODO
#ifndef PAGES_ALARMS_H
#define PAGES_ALARMS_H

/**
 * @file alarms.h
 * @brief AlarmsPage - scrollable bordered table of alarm conditions.
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
 * Rows are ordered active-first then by most-recent onset (started_ms);
 * never-active alarms sit at the bottom. The exception is the "all" filter,
 * which keeps the fixed definition (table) order so rows never reorder as
 * alarms toggle. The top / bottom rules cap their
 * column marks with '^' / 'v' when alarms are hidden above / below the window.
 * SPACE cycles the filter: recent (default) -> all -> active -> active and not
 * masked. A condition's recent window defaults to one week
 * (AlarmCondition::recent_s). UP/DOWN move a selection cursor (drawn inverted),
 * auto-scrolling the window only when the cursor would leave it; PAGE_UP/DOWN
 * move it half a window at a time and ENTER jumps back to the top. `a` acknowledges
 * the selected condition. The set is re-checked once a second; a repaint is
 * paced a few rows per tick under the shared TX floor, so it interleaves with
 * the bars.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/alarms/alarms.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/pages/scroll_table.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen scrollable alarm table (see file header). */
    class AlarmsPage : public ScrollTablePage {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Owning console (content-area geometry). */
        Console&                  _console;
        /** @brief Alarm source (may be null). Non-const: the `a` key acks the
         *  selected condition through it. */
        Alarms::AlarmModel*       _model;
        /** @brief Mirrors tick(active). */
        /** @brief Filter mode, cycled by SPACE (see MODE_*). Defaults to
         *  RECENT. */
        uint8_t                   _mode     = MODE_RECENT;
        /** @brief A nav / mode / ack / set-change happened: the next tick
         *  recollects and redraws the table (the terminal diff pares the bytes). */
        bool                      _dirty = false;
        /** @brief Uptime of the last once-a-second alarm-set re-check (see
         *  tick()). */
        uint64_t                  _last_refresh_ms = 0;
        /** @brief Effective-active set at the last repaint. Badge / last-event
         *  / order change only on an effective transition, so this gates the
         *  repaint - except RECENT membership, which also expires on time (see
         *  _last_recent_sig). */
        uint64_t                  _last_active_sig = 0;
        /** @brief Conditions in their RECENT window at the last repaint. In
         *  RECENT mode an alarm can age out with no raw transition, so a change
         *  here is the only signal the list must refresh. */
        uint64_t                  _last_recent_sig = 0;
        /** @brief Acknowledged set at the last repaint. An ack changes the
         *  sort/badge without moving the active set, so it gates the repaint
         *  too. */
        uint64_t                  _last_acked_sig = 0;
        /** @brief Visible alarm indices (table order), filled by _collect(). */
        uint8_t                   _vis[Alarms::ALARM_BITS];
        /** @brief Number of visible entries in _vis. */
        uint8_t                   _nvis     = 0;

        /** @} */

        /** @name Filter modes
         *  (SPACE cycles recent -> all -> active -> unmasked)
         *  @{ */
        static constexpr uint8_t MODE_RECENT   = 0;  // last event, within window
        static constexpr uint8_t MODE_ALL      = 1;  // every defined alarm
        static constexpr uint8_t MODE_ACTIVE   = 2;  // active (masked or not)
        static constexpr uint8_t MODE_UNMASKED = 3;  // active and not masked
        static constexpr uint8_t MODE_COUNT    = 4;

        /** @brief Short label for the current filter mode. */
        static const char* _mode_label(uint8_t mode) {
            static const char* const labels[MODE_COUNT] =
                {"recent", "all", "active", "unmasked"};
            return mode < MODE_COUNT ? labels[mode] : "all";
        }
        /** @} */

        /** @name Layout
         *  Columns: CODE | ON (reverse-video badge when active) | LAST EVENT
         *  (the last transition's timestamp, "YY/MM/DD HH:MM") | DESCRIPTION.
         *  @{ */
        static constexpr uint16_t MARGIN     = 2;   // blank columns each side
        static constexpr uint16_t CODE_W     = 10;  // cell inner width (1 pad)
        static constexpr uint16_t STATE_W    = 4;   // " ON" badge / "ON" header
        static constexpr uint16_t EVENT_W    = 16;  // " YY/MM/DD HH:MM"
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t RULE_ROW   = 3;
        static constexpr uint16_t DATA_TOP   = 4;
        /** @brief Screen column where the state cell's text starts, tracking
         *  the layout _draw_grid_row() emits: margin + "| " + CODE(inner) +
         *  "| ". The "ON" badge starts here; _draw_data_row() splits the row
         *  write at this column so only those two chars are reverse-video (no
         *  flicker-prone overdraw). */
        static constexpr uint16_t STATE_TEXT_COL = MARGIN + 2 + (CODE_W - 1) + 2;
        /** @brief CODE field span (contents only, no "| " border): the only part
         *  inverted for the selection cursor, so a selection move patches one
         *  small field, not a whole row. The span stops one column short of the
         *  right border, leaving the cell's trailing pad plain - so the highlight
         *  reads as a bordered cell with a gap each side (mirrors the CONFIG page),
         *  not a block butting the "|". Safe because codes are at most CODE_W-2
         *  wide, so that last column is always padding. */
        static constexpr uint16_t CODE_CELL_L    = MARGIN + 2;             // 4
        static constexpr uint16_t CODE_CELL_R    = MARGIN + 2 + CODE_W - 2;// 12
        /** @} */

        /** @brief Description-column width: the row remainder, clamped. */
        uint16_t _desc_w() const {
            uint16_t cols  = _terminal.cols();
            // 5 rules + fixed cells
            uint16_t fixed = (uint16_t)(5 + CODE_W + STATE_W + EVENT_W);
            uint16_t avail = (cols > (uint16_t)(2 * MARGIN + fixed))
                                 ? (uint16_t)(cols - 2 * MARGIN - fixed) : 8;
            if (avail < 8)  avail = 8;
            if (avail > 70) avail = 70;
            return avail;
        }

        /** @brief Format the last-event cell: edge_ms as "YY/MM/DD HH:MM", or
         *  empty if it never fired (ON vs OFF is shown by the ON column). */
        static void _fmt_event(char* buf, size_t cap, const Alarms::AlarmCondition& c) {
            if (!c.active && c.edge_ms == 0) { if (cap) buf[0] = '\0'; return; }
            Utils::CalendarTime t = Utils::cal_time_ms(c.edge_ms);
            snprintf(buf, cap, "%02u/%02u/%02u %02u:%02u",
                     (unsigned)(t.year % 100), (unsigned)t.month, (unsigned)t.day,
                     (unsigned)t.hour, (unsigned)t.minute);
        }

        /** @brief Build a '|'-ruled grid row into @p line, returning its
         *  length. */
        uint16_t _build_grid_row(char* line, size_t cap, const char* c0,
                                 const char* c1, const char* c2, const char* c3) {
            const char* cell[4] = { c0, c1, c2, c3 };
            uint16_t    w[4]    = { CODE_W, STATE_W, EVENT_W, _desc_w() };
            uint16_t n = 0;
            line[n++] = ' '; line[n++] = ' ';                // left margin
            for (uint8_t i = 0; i < 4; i++) {
                if (n < cap - 1) line[n++] = '|';
                int cw = (int)w[i] - 1;
                int m  = snprintf(line + n, cap - n, " %-*.*s", cw, cw,
                                  cell[i] ? cell[i] : "");
                if (m < 0) m = 0;
                n = (uint16_t)(n + m);
                if (n > cap - 1) { n = (uint16_t)(cap - 1); break; }
            }
            if (n < cap - 1) line[n++] = '|';                 // closing rule
            return n;
        }

        /** @brief Draw a structural row (all normal video); the terminal's diff
         *  drops it when unchanged. */
        void _emit(uint16_t row, const char* line, uint16_t n) {
            _draw_line(row, line, n);   // shared bordered-table line painter
        }

        /** @brief Draw a '|'-ruled grid row (all normal video). */
        void _draw_grid_row(uint16_t row, const char* c0, const char* c1,
                            const char* c2, const char* c3) {
            char     line[160];
            uint16_t n = _build_grid_row(line, sizeof(line), c0, c1, c2, c3);
            _emit(row, line, n);
        }

        /** @brief Draw a horizontal rule "+----+--+...+". @p sep is the
         *  corner/separator char: '+' plain, or '^'/'v' to cap the edge with
         *  scroll arrows when alarms are hidden above / below. */
        void _draw_rule(uint16_t row, char sep = '+') {
            // The alarms grid's column widths already include the cell padding,
            // so the spans go in as-is (unlike MONITOR/CONFIG's +2).
            uint16_t spans[4] = { CODE_W, STATE_W, EVENT_W, _desc_w() };
            ScrollTablePage::_draw_rule(row, MARGIN, spans, 4, sep, sep);
        }

        /** @brief Blank a structural row. */
        void _clear_row(uint16_t row) { _emit(row, "", 0); }

        /** @brief Draw the footer legend: reverse-video mode badge + counts on
         *  the left, SPACE-cycles-mode hint on the right. Drawn immediate-mode; the
         *  terminal's back-buffer diff repaints it only on an actual mode / count
         *  change. */
        void _draw_footer(uint16_t row, uint8_t nvis, uint8_t active) {
            const char* mode = _mode_label(_mode);
            const char* next = _mode_label((uint8_t)((_mode + 1) % MODE_COUNT));

            char left[64];
            int ln = snprintf(left, sizeof(left), "  [%s]   %u listed   %u active",
                              mode, (unsigned)nvis, (unsigned)active);
            if (ln < 0) ln = 0;
            uint16_t llen = (uint16_t)std::min((size_t)ln, sizeof(left) - 1);

            uint16_t ml = (uint16_t)strlen(mode);
            if ((uint16_t)(3 + ml) > llen) ml = (llen > 3) ? (uint16_t)(llen - 3) : 0;

            char right[32];
            int rn = snprintf(right, sizeof(right), "a:ack  SPACE: %s", next);
            if (rn < 0) rn = 0;
            uint16_t cols = _terminal.cols();
            uint16_t rcol = (cols > (uint16_t)(rn + 2)) ? (uint16_t)(cols - rn - 2) : 0;

            // One pass with the mode label (col 3, inside the brackets)
            // reverse-video, so it reads as a badge without a flicker-prone
            // normal-then-inverted overdraw.
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(left, 3);                       // "  ["
            _terminal.set_inverted(true);
            _terminal.write_chars(left + 3, ml);                  // mode badge
            _terminal.set_inverted(false);
            _terminal.write_chars(left + 3 + ml, (uint16_t)(llen - 3 - ml));
            _terminal.clear_to_eol();
            // Right-aligned hint (ack + next filter), when it clears the left.
            if (rcol > llen + 2) {
                _terminal.move_to({row, rcol});
                _terminal.write_chars(right, (uint16_t)rn);
            }
        }

        /** @brief True if @p c belongs in RECENT mode: a last event still
         *  within its window. Active = always recent; inactive expires
         *  recent_s after it cleared (0 = DEFAULT_RECENT_S, <0 = never). */
        bool _is_recent(const Alarms::AlarmCondition& c, uint64_t now) const {
            if (c.edge_ms == 0) return false;     // never fired -> no event
            if (c.active)       return true;      // on -> always recent
            if (c.recent_s < 0) return true;      // never expires
            uint64_t window_ms = (uint64_t)(c.recent_s == 0 ? Alarms::DEFAULT_RECENT_S
                                                            : c.recent_s) * 1000u;
            return (now - c.edge_ms) <= window_ms;
        }

        /** @brief Order two visible entries: unack'd-active first, then
         *  ack'd-active, then inactive; each by most-recent onset. An ack
         *  demotes without removing. */
        bool _before(const Alarms::AlarmCondition& x,
                     const Alarms::AlarmCondition& y) const {
            bool xn = x.active && !x.user_ack;     // active and unack'd
            bool yn = y.active && !y.user_ack;
            // unack'd-active heads list
            if (xn != yn) return xn;
            // then ack'd-active first
            if (x.active != y.active) return x.active;
            return x.started_ms > y.started_ms;        // then most-recent onset
        }

        /** @brief Fill _vis/_nvis with the indices shown in the current mode,
         *  ordered active-first then by most-recent onset. */
        void _collect() {
            _nvis = 0;
            if (_model == nullptr) return;
            uint64_t now = HAL::Clock::uptime_ms();
            for (uint8_t i = 0;
                 i < _model->count() && _nvis < Alarms::ALARM_BITS; i++) {
                const Alarms::AlarmCondition& c = _model->condition(i);
                if (c.code == nullptr) continue;             // undefined slot
                bool include;
                switch (_mode) {
                    case MODE_RECENT:   include = _is_recent(c, now);     break;
                    case MODE_ACTIVE:   include = c.active;               break;
                    // UNMASKED mirrors the alarm bar, so an ack'd alarm drops
                    // out of it too (the ALL / ACTIVE / RECENT views keep it,
                    // demoted by _before()). With masking removed this is the
                    // active-and-un-ack'd set.
                    case MODE_UNMASKED:
                        include = c.active && !c.user_ack; break;
                    default:            include = true;                   break;
                }
                if (include) _vis[_nvis++] = i;
            }
            // ALL mode lists every alarm in definition (table) order, so the
            // rows never reorder as alarms come and go - only the ON badge
            // changes in place. The other modes order active-first (_before()).
            if (_mode == MODE_ALL) { _clamp_selection(); return; }
            // Insertion sort by _before(): runs once per (gated) repaint,
            // _nvis is nearly always tiny, so it beats a general sort and is
            // stable in table order on ties.
            for (uint8_t a = 1; a < _nvis; a++) {
                uint8_t v = _vis[a];
                const Alarms::AlarmCondition& cv = _model->condition(v);
                int b = (int)a - 1;
                while (b >= 0 && _before(cv, _model->condition(_vis[b]))) {
                    _vis[b + 1] = _vis[b];
                    b--;
                }
                _vis[b + 1] = v;
            }
            _clamp_selection();
        }

        /** @brief Bitmask of conditions whose effective (latched) state is
         *  active - what the table shows, so it (not raw()) gates the repaint:
         *  a latched alarm whose raw bit pulses keeps a steady effective state,
         *  so the badges don't flicker. */
        uint64_t _active_sig() const {
            if (_model == nullptr) return 0;
            uint64_t m = 0;
            for (uint8_t i = 0; i < _model->count(); i++)
                if (_model->condition(i).active) m |= Alarms::alarm_bit(i);
            return m;
        }

        /** @brief Bitmask of conditions within their RECENT window, to detect a
         *  time-based expiry that leaves the active set unchanged. */
        uint64_t _recent_sig() const {
            if (_model == nullptr) return 0;
            uint64_t now = HAL::Clock::uptime_ms(), m = 0;
            for (uint8_t i = 0; i < _model->count(); i++) {
                const Alarms::AlarmCondition& c = _model->condition(i);
                if (c.code != nullptr && _is_recent(c, now)) m |= Alarms::alarm_bit(i);
            }
            return m;
        }

        /** @brief Bitmask of acknowledged conditions, to detect an ack that
         *  changes the sort/badge without moving the active set. */
        uint64_t _acked_sig() const {
            if (_model == nullptr) return 0;
            uint64_t m = 0;
            for (uint8_t i = 0; i < _model->count(); i++)
                if (_model->condition(i).user_ack) m |= Alarms::alarm_bit(i);
            return m;
        }

        /** @brief Snapshot the change signature after a repaint. */
        void _snapshot() {
            _last_active_sig = _active_sig();
            _last_recent_sig = _recent_sig();
            _last_acked_sig  = _acked_sig();
        }

        /** @brief True if the displayed set moved since the last repaint (so a
         *  steady, scrolled view is left untouched). The active set covers
         *  badge/last-event/order; in RECENT mode a row can also expire on
         *  time, so recent membership is compared. */
        bool _changed() const {
            if (_model == nullptr) return false;
            if (_active_sig() != _last_active_sig) return true;
            if (_acked_sig()  != _last_acked_sig)  return true;
            return _mode == MODE_RECENT && _recent_sig() != _last_recent_sig;
        }

        /** @name Window geometry
         *  Bottom-anchored on the content area. From the bottom up: free row,
         *  legend, free row, bottom rule, then the data band up to the top
         *  rule. The rules carry the scroll arrows (see _draw_rules()).
         *  @{ */
        // free row below at rows()-1
        uint16_t _legend_row()  const { return (uint16_t)(_console.rows() - 2); }
        // bottom rule (free row above legend at rows()-3)
        uint16_t _brule_row()   const { return (uint16_t)(_console.rows() - 4); }
        /** @brief Number of data rows that fit, or 0 if the screen is too
         *  short. */
        uint16_t _window() const override {
            uint16_t r = _console.rows();
            return (r >= (uint16_t)(DATA_TOP + 5)) ? (uint16_t)(r - DATA_TOP - 4) : 0;
        }
        /** @name ScrollTablePage geometry
         *  @{ */
        uint16_t _data_top()  const override { return DATA_TOP; }
        uint16_t _row_count() const override { return _nvis; }
        /** @} */
        /** @brief Entries scrolled off the top / bottom of the visible
         *  window. */
        uint16_t _hidden_above() const { return _top; }
        uint16_t _hidden_below(uint16_t window) const {
            uint16_t end = (uint16_t)(_top + window);
            return (_nvis > end) ? (uint16_t)(_nvis - end) : 0;
        }
        /** @brief Redraw the top/bottom rules, capping with '^'/'v' when alarms
         *  are hidden above / below the window. */
        void _draw_rules(uint16_t window) {
            _draw_rule(RULE_ROW,     _hidden_above()       ? '^' : '+');
            _draw_rule(_brule_row(), _hidden_below(window) ? 'v' : '+');
        }
        /** @} */

        /** @brief Write the margin + CODE cell of @p line, inverting the CODE field
         *  for the selection cursor (only the first cell highlights). Leaves the
         *  cursor at CODE_CELL_R. */
        void _write_code_cell(const char* line, bool selected) {
            _terminal.write_chars(line, CODE_CELL_L);              // margin + "| "
            if (selected) _terminal.set_inverted(true);
            _terminal.write_chars(line + CODE_CELL_L, (uint16_t)(CODE_CELL_R - CODE_CELL_L));
            if (selected) _terminal.set_inverted(false);
        }

        /** @brief Draw a full data row (CODE/ON/EVENT/DESCRIPTION) immediate-mode.
         *  Only the CODE cell inverts for the selection cursor; the state cell shows
         *  its own reverse-video "ON" badge while active and unacked. The terminal's
         *  back-buffer diff pares this to the cells that actually moved. */
        void _draw_full_row(uint16_t row, const Alarms::AlarmCondition& c, bool selected) {
            char ev[16];
            _fmt_event(ev, sizeof(ev), c);
            const char* state = c.active ? "ON" : (c.edge_ms != 0 ? "OFF" : "");
            char     line[160];
            uint16_t n = _build_grid_row(line, sizeof(line), c.code ? c.code : "?",
                                         state, ev, c.desc ? c.desc : "");
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _write_code_cell(line, selected);
            if (c.active && !c.user_ack && n > (uint16_t)(STATE_TEXT_COL + 2)) {
                // "ON" sits at STATE_TEXT_COL: split so only it inverts.
                _terminal.write_chars(line + CODE_CELL_R,
                                      (uint16_t)(STATE_TEXT_COL - CODE_CELL_R));
                _terminal.set_inverted(true);
                _terminal.write_chars(line + STATE_TEXT_COL, 2);
                _terminal.set_inverted(false);
                _terminal.write_chars(line + STATE_TEXT_COL + 2,
                                      (uint16_t)(n - STATE_TEXT_COL - 2));
            } else {
                _terminal.write_chars(line + CODE_CELL_R, (uint16_t)(n - CODE_CELL_R));
            }
            _terminal.clear_to_eol();
        }

        /** @brief Paint data row at screen @p row for visible entry @p vi: the full
         *  row when it holds an alarm, else a blanked row (past the set). Drawn every
         *  time; the terminal's back-buffer diff drops the cells that did not move. */
        void _draw_data_row(uint16_t row, uint16_t vi) {
            if (vi >= _nvis) { _clear_row(row); return; }     // past the set: blank it
            const Alarms::AlarmCondition& c = _model->condition(_vis[vi]);
            _draw_full_row(row, c, vi == _selected);
        }


        /** @brief Acknowledge the selected condition through the model. */
        void _ack_selected() {
            if (_model == nullptr || _selected >= _nvis) return;
            const Alarms::AlarmCondition& c = _model->condition(_vis[_selected]);
            if (c.code != nullptr) _model->ack(c.code);
            _dirty = true;   // ack changes the badge / sort
        }

        /** @brief Draw the static chrome: top margin, header, and the two blank
         *  rows around the legend. */
        void _draw_structure() {
            uint16_t legend = _legend_row();
            _clear_row(1);                                      // top margin
            _draw_grid_row(HEADER_ROW, "CODE", "ON", "LAST EVENT", "DESCRIPTION");
            _clear_row((uint16_t)(legend - 1));                 // blank above legend
            _clear_row((uint16_t)(legend + 1));                 // blank below legend
        }

    public:
        /** @brief Bind to a terminal and alarm model. */
        AlarmsPage(Console& console, Alarms::AlarmModel* model)
            : ScrollTablePage(console.terminal()), _console(console), _model(model) {}

        /** @name Page interface
         *  @{ */

        const char* name() const override { return "ALARMS"; }

        void handle_text(char c) override {
            if (c == ' ') {                 // cycle filter mode
                _mode = (uint8_t)((_mode + 1) % MODE_COUNT);
                _top  = 0;
                _selected = 0;
                _dirty = true;              // reorders the list + legend
                // The legend row already outranks the table (declared in
                // focus()), so the badge goes out ahead of the repaint this same
                // keypress triggered.
            } else if (c == 'a' || c == 'A') {   // ack the selected condition
                _ack_selected();
            }
        }

        void handle_command(Command cmd, uint16_t = 1) override {
            if (_nav_command(cmd)) { _dirty = true; return; }
            if (cmd == Command::ENTER) { _top = 0; _selected = 0; _dirty = true; }  // jump to top
        }

        void deactivate() override {}

        void focus() override {
            // The legend carries the mode badge, which acknowledges a keypress and
            // so must beat the table rows it sits under. Declared here because the
            // Console hands the content band back on every page switch.
            _terminal.set_row_priority(_legend_row(), Terminal::PRIO_CHROME);
            _dirty = true;   // draw the whole page fresh (the terminal diff pares it)
        }

        void tick(bool active) override {
            if (!active) return;
            // Re-check the set on a slow cadence (alarms move slowly); mark dirty
            // only when the displayed set actually moved.
            uint64_t now = HAL::Clock::uptime_ms();
            if (now - _last_refresh_ms >= Config::PAGE_HOT_MS) {
                _last_refresh_ms = now;
                if (_changed()) _dirty = true;
            }
            if (!_dirty) return;
            uint16_t window = _window();
            if (window == 0) { _snapshot(); _dirty = false; return; }
            // Rebuild the view set and re-clamp the window, then redraw the whole
            // table immediate-mode: the terminal's back-buffer diff emits only the
            // cells that actually changed, so the page keeps no paint shadow.
            _collect();
            uint16_t maxtop = _max_top();
            if (_top > maxtop) _top = maxtop;
            _draw_structure();
            _draw_rules(window);
            _draw_footer(_legend_row(), _nvis, _model ? _model->active_count() : 0);
            for (uint16_t vi = 0; vi < window; vi++)
                _draw_data_row((uint16_t)(DATA_TOP + vi), (uint16_t)(_top + vi));
            _snapshot();
            _dirty = false;
        }

        /** @} */
    };

}}

#endif // PAGES_ALARMS_H
