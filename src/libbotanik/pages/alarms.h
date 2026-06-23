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
 * never-active alarms sit at the bottom. The top / bottom rules cap their column
 * marks with '^' / 'v' when alarms are hidden above / below the window. SPACE
 * cycles the filter: recent (default) -> all -> active -> active and not masked.
 * A condition's recent window defaults to one week (AlarmCondition::recent_s).
 * UP/DOWN scroll a line at a time (one scroll_region() shift + one painted row);
 * PAGE_UP/DOWN jump a window and ENTER returns to the top, both via a full
 * repaint. The set is re-checked once a second; a repaint is paced a few rows per
 * tick under the shared TX floor, so it interleaves with the bars.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/alarms/alarms.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/terminal/terminal.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen scrollable alarm table (see file header). */
    class AlarmsPage : public Page {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Owning terminal. */
        AnsiTerminal&             _terminal;
        /** @brief Alarm source (may be null). */
        const Alarms::AlarmModel* _model;
        /** @brief Mirrors tick(active). */
        bool                      _active   = false;
        /** @brief Filter mode, cycled by SPACE (see MODE_*). Defaults to RECENT. */
        uint8_t                   _mode     = MODE_RECENT;
        /** @brief First visible data row on screen (scroll offset from the top of
         *  the set); _scroll_pending holds the not-yet-applied delta. */
        uint16_t                  _top      = 0;
        /** @brief Rows still to scroll to reach where the user keyed (+ down,
         *  - up). Applied a row at a time only while the TX ring is shallow (see
         *  _drain_scroll), so a scroll never floods the buffer or falls back to a
         *  full redraw. */
        int16_t                   _scroll_pending = 0;
        /** @brief Staged-repaint cursor: the next table element to paint, or
         *  PAINT_IDLE when fully drawn. A repaint is paced a few rows per tick
         *  under the TX floor (see _paint_step()) so it never starves the status /
         *  alarm bars; _request_repaint() restarts it from the top. */
        static constexpr uint16_t PAINT_IDLE = 0xFFFF;
        uint16_t                  _paint_i  = 0;
        /** @brief Data rows that held content last frame. When a repaint finds
         *  fewer, the vacated rows (and the bottom rule) are cleared up front so
         *  stale rows don't linger through the paced repaint. Reset on focus. */
        uint16_t                  _painted_data = 0;
        /** @brief Uptime of the last once-a-second alarm-set re-check (see
         *  tick()). */
        uint64_t                  _last_refresh_ms = 0;
        /** @brief Effective-active set at the last repaint. Badge / last-event /
         *  order change only on an effective transition, so this gates the repaint
         *  - except RECENT membership, which also expires on time (see
         *  _last_recent_sig). */
        uint64_t                  _last_active_sig = 0;
        /** @brief Conditions in their RECENT window at the last repaint. In RECENT
         *  mode an alarm can age out with no raw transition, so a change here is
         *  the only signal the list must refresh. */
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

        /** @name Paint shadow
         *  One entry per screen row: what that row last showed, so a repaint emits
         *  only what changed. idx tags the row kind; sig is its change signature.
         *  @{ */
        static constexpr uint16_t MAX_ROWS    = 64;   // rows beyond this redraw
        static constexpr uint8_t  ROW_STRUCT  = 0xFE; // header/rule/margin/blank
        static constexpr uint8_t  ROW_BLANK   = 0xFF; // empty data row / unknown
        static constexpr uint32_t EVENT_EMPTY = 0x3FFFFFFF;  // LAST EVENT empty
        /** @brief idx: alarm table index, ROW_STRUCT, or ROW_BLANK. sig: data row
         *  = packed (ON state | event minute); structural row = FNV(line bytes). */
        struct RowShadow { uint8_t idx; uint32_t sig; };
        RowShadow                 _row[MAX_ROWS];
        /** @} */

        /** @name Filter modes (SPACE cycles recent -> all -> active -> unmasked)
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
        /** @brief Screen column where the state cell's text starts, tracking the
         *  layout _draw_grid_row() emits: margin + "| " + CODE(inner) + "| ". The
         *  "ON" badge starts here; _draw_data_row() splits the row write at this
         *  column so only those two chars are reverse-video (no flicker-prone
         *  overdraw). */
        static constexpr uint16_t STATE_TEXT_COL = MARGIN + 2 + (CODE_W - 1) + 2;
        /** @brief ON-cell inner width and the LAST EVENT cell's text column /
         *  inner width, tracking the same layout _build_grid_row() emits. Used by
         *  the per-cell patch path (_patch_cells). */
        static constexpr uint16_t STATE_INNER    = STATE_W - 1;        // 3
        static constexpr uint16_t EVENT_TEXT_COL =
            STATE_TEXT_COL + STATE_INNER + 2;                          // 20
        static constexpr uint16_t EVENT_INNER    = EVENT_W - 1;        // 15
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

        /** @brief Build a '|'-ruled grid row into @p line, returning its length. */
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

        /** @brief Reset the whole paint shadow to "unknown" (forces a redraw). */
        void _invalidate_shadow() {
            for (RowShadow& r : _row) { r.idx = ROW_BLANK; r.sig = 0; }
        }

        /** @brief FNV-1a over s[0..n), masked to 31 bits: a structural row's
         *         fingerprint. */
        static uint32_t _hash(const char* s, uint16_t n) {
            uint32_t h = 2166136261u;
            for (uint16_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
            return h & 0x7FFFFFFFu;
        }

        /** @brief Draw a structural row through the shadow: skip when its bytes are
         *  unchanged on screen, else emit and record the fingerprint. */
        void _emit(uint16_t row, const char* line, uint16_t n) {
            uint32_t sig = _hash(line, n);
            if (row < MAX_ROWS && _row[row].idx == ROW_STRUCT && _row[row].sig == sig)
                return;
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, n);
            _terminal.clear_to_eol();
            if (row < MAX_ROWS) { _row[row].idx = ROW_STRUCT; _row[row].sig = sig; }
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
            uint16_t w[4] = { CODE_W, STATE_W, EVENT_W, _desc_w() };
            char     line[160];
            uint16_t n = 0;
            line[n++] = ' '; line[n++] = ' ';                // left margin
            if (n < sizeof(line) - 1) line[n++] = sep;
            for (uint8_t i = 0; i < 4; i++) {
                for (uint16_t k = 0; k < w[i] && n < sizeof(line) - 1; k++)
                    line[n++] = '-';
                if (n < sizeof(line) - 1) line[n++] = sep;
            }
            _emit(row, line, n);
        }

        /** @brief Blank a structural row (through the paint shadow). */
        void _clear_row(uint16_t row) { _emit(row, "", 0); }

        /** @brief Draw the footer legend: reverse-video mode badge + counts on the
         *  left, SPACE-cycles-mode hint on the right. */
        void _draw_footer(uint16_t row, uint8_t nvis, uint8_t active) {
            const char* mode = _mode_label(_mode);
            const char* next = _mode_label((uint8_t)((_mode + 1) % MODE_COUNT));

            char left[64];
            int ln = snprintf(left, sizeof(left), "  [%s]   %u listed   %u active",
                              mode, (unsigned)nvis, (unsigned)active);
            if (ln < 0) ln = 0;
            uint16_t llen = (uint16_t)std::min((size_t)ln, sizeof(left) - 1);

            // One pass with the mode label (col 3, inside the brackets)
            // reverse-video, so it reads as a badge without a flicker-prone
            // normal-then-inverted overdraw.
            uint16_t ml = (uint16_t)strlen(mode);
            if ((uint16_t)(3 + ml) > llen) ml = (llen > 3) ? (uint16_t)(llen - 3) : 0;
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(left, 3);                       // "  ["
            _terminal.set_inverted(true);
            _terminal.write_chars(left + 3, ml);                  // mode badge
            _terminal.set_inverted(false);
            // "]  ... active"
            _terminal.write_chars(left + 3 + ml, (uint16_t)(llen - 3 - ml));
            _terminal.clear_to_eol();

            // Right-aligned "SPACE: <next mode>" hint, when it clears the left.
            char right[24];
            int rn = snprintf(right, sizeof(right), "SPACE: %s", next);
            if (rn < 0) rn = 0;
            uint16_t cols = _terminal.cols();
            uint16_t rcol = (cols > (uint16_t)(rn + 2)) ? (uint16_t)(cols - rn - 2) : 0;
            if (rcol > llen + 2) {
                _terminal.move_to({row, rcol});
                _terminal.write_chars(right, (uint16_t)rn);
            }
        }

        /** @brief True if @p c belongs in RECENT mode: a last event still within
         *  its window. Active = always recent; inactive expires recent_s after it
         *  cleared (0 = DEFAULT_RECENT_S, <0 = never). */
        bool _is_recent(const Alarms::AlarmCondition& c, uint64_t now) const {
            if (c.edge_ms == 0) return false;     // never fired -> no event
            if (c.active)       return true;      // on -> always recent
            if (c.recent_s < 0) return true;      // never expires
            uint64_t window_ms = (uint64_t)(c.recent_s == 0 ? Alarms::DEFAULT_RECENT_S
                                                            : c.recent_s) * 1000u;
            return (now - c.edge_ms) <= window_ms;
        }

        /** @brief Order two visible entries: unack'd-active first, then
         *  ack'd-active, then inactive; each by most-recent onset. An ack demotes
         *  without removing. */
        bool _before(const Alarms::AlarmCondition& x,
                     const Alarms::AlarmCondition& y) const {
            bool xn = x.active && !x.acknowledged;     // active and unack'd
            bool yn = y.active && !y.acknowledged;
            if (xn != yn) return xn;                   // unack'd-active heads list
            if (x.active != y.active) return x.active; // then ack'd-active first
            return x.started_ms > y.started_ms;        // then most-recent onset
        }

        /** @brief Fill _vis/_nvis with the indices shown in the current mode,
         *  ordered active-first then by most-recent onset. */
        void _collect() {
            _nvis = 0;
            if (_model == nullptr) return;
            uint64_t now = HAL::uptime_ms();
            for (uint8_t i = 0;
                 i < _model->count() && _nvis < Alarms::ALARM_BITS; i++) {
                const Alarms::AlarmCondition& c = _model->condition(i);
                if (c.code == nullptr) continue;             // undefined slot
                bool include;
                switch (_mode) {
                    case MODE_RECENT:   include = _is_recent(c, now);     break;
                    case MODE_ACTIVE:   include = c.active;               break;
                    // UNMASKED mirrors the alarm bar, so an ack'd alarm drops out
                    // of it too (the ALL / ACTIVE / RECENT views keep it, demoted
                    // by _before()).
                    case MODE_UNMASKED:
                        include = c.active && c.unmasked && !c.acknowledged; break;
                    default:            include = true;                   break;
                }
                if (include) _vis[_nvis++] = i;
            }
            // Insertion sort by _before(): runs once per (gated) repaint, _nvis is
            // nearly always tiny, so it beats a general sort and is stable in table
            // order on ties.
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
        }

        /** @brief Bitmask of conditions whose effective (latched) state is active
         *  - what the table shows, so it (not raw()) gates the repaint: a latched
         *  alarm whose raw bit pulses keeps a steady effective state, so the
         *  badges don't flicker. */
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
            uint64_t now = HAL::uptime_ms(), m = 0;
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
                if (_model->condition(i).acknowledged) m |= Alarms::alarm_bit(i);
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
         *  badge/last-event/order; in RECENT mode a row can also expire on time,
         *  so recent membership is compared. */
        bool _changed() const {
            if (_model == nullptr) return false;
            if (_active_sig() != _last_active_sig) return true;
            if (_acked_sig()  != _last_acked_sig)  return true;
            return _mode == MODE_RECENT && _recent_sig() != _last_recent_sig;
        }

        /** @name Window geometry
         *  Bottom-anchored on the content area. From the bottom up: free row,
         *  legend, free row, bottom rule, then the data band up to the top rule.
         *  The rules carry the scroll arrows (see _paint_element()).
         *  @{ */
        // free row below at rows()-1
        uint16_t _legend_row()  const { return (uint16_t)(_terminal.rows() - 2); }
        // bottom rule (free row above legend at rows()-3)
        uint16_t _brule_row()   const { return (uint16_t)(_terminal.rows() - 4); }
        uint16_t _data_bottom() const { return (uint16_t)(_terminal.rows() - 5); }
        /** @brief Number of data rows that fit, or 0 if the screen is too short. */
        uint16_t _window() const {
            uint16_t r = _terminal.rows();
            return (r >= (uint16_t)(DATA_TOP + 5)) ? (uint16_t)(r - DATA_TOP - 4) : 0;
        }
        /** @brief Largest scroll offset that still fills the window (0 = all
         *         fit). */
        uint16_t _max_top(uint16_t window) const {
            return (_nvis > window) ? (uint16_t)(_nvis - window) : 0;
        }
        /** @brief Entries scrolled off the top / bottom of the visible window. */
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

        /** @brief ON-cell render state: 0 blank, 1 "OFF", 2 "ON" normal, 3 "ON"
         *  reverse-video (active and unacknowledged). */
        static uint8_t _on_state(const Alarms::AlarmCondition& c) {
            if (c.active) return c.acknowledged ? 2 : 3;
            return c.edge_ms != 0 ? 1 : 0;
        }

        /** @brief Pack a data row's two mutable cells: ON state in the top 2 bits,
         *  the LAST EVENT minute (edge_ms/60000, the render resolution) in the low
         *  30, or EVENT_EMPTY when blank. */
        static uint32_t _pack(const Alarms::AlarmCondition& c) {
            uint32_t ev = (!c.active && c.edge_ms == 0)
                              ? EVENT_EMPTY
                              : (uint32_t)((c.edge_ms / 60000u) & 0x3FFFFFFFu);
            return ((uint32_t)_on_state(c) << 30) | ev;
        }

        /** @brief True when the fixed CODE/ON/EVENT columns fit, so cell patching
         *  is safe; on a narrower screen the row falls back to a clamped full
         *  redraw. */
        bool _cells_fit() const {
            return _terminal.cols() >= (uint16_t)(EVENT_TEXT_COL + EVENT_INNER + 2);
        }

        /** @brief Blank a data row without touching the structural shadow path. */
        void _clear_raw(uint16_t row) {
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.clear_to_eol();
        }

        /** @brief Rewrite only the ON and LAST EVENT cells (CODE/DESCRIPTION
         *  stay). Both are fully overwritten, so no clear_to_eol. */
        void _patch_cells(uint16_t row, const Alarms::AlarmCondition& c) {
            uint8_t on = _on_state(c);
            _terminal.move_to({row, STATE_TEXT_COL});
            if (on == 3) {                                   // "ON" reverse + pad
                _terminal.set_inverted(true);  _terminal.write_chars("ON", 2);
                _terminal.set_inverted(false); _terminal.write_chars(" ", 1);
            } else {
                _terminal.set_inverted(false);
                _terminal.write_chars(
                    on == 2 ? "ON " : (on == 1 ? "OFF" : "   "), 3);
            }
            char ev[16];
            _fmt_event(ev, sizeof(ev), c);
            char cell[24];
            int m = snprintf(cell, sizeof(cell), "%-*.*s",
                             (int)EVENT_INNER, (int)EVENT_INNER, ev);
            if (m < 0) m = 0;
            _terminal.move_to({row, EVENT_TEXT_COL});
            _terminal.write_chars(
                cell, (uint16_t)std::min((size_t)m, sizeof(cell) - 1));
        }

        /** @brief Draw a full data row (CODE/ON/EVENT/DESCRIPTION). The state cell
         *  shows a reverse-video "ON" badge while active and unacked, "OFF" once
         *  cleared, blank if never fired. One inline pass (no flicker-prone
         *  normal-then-inverted overdraw). */
        void _draw_full_row(uint16_t row, const Alarms::AlarmCondition& c) {
            char ev[16];
            _fmt_event(ev, sizeof(ev), c);
            const char* state = c.active ? "ON" : (c.edge_ms != 0 ? "OFF" : "");
            char     line[160];
            uint16_t n = _build_grid_row(line, sizeof(line), c.code ? c.code : "?",
                                         state, ev, c.desc ? c.desc : "");
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            if (c.active && !c.acknowledged && n > (uint16_t)(STATE_TEXT_COL + 2)) {
                // "ON" sits at STATE_TEXT_COL: split the write so only it inverts.
                _terminal.write_chars(line, STATE_TEXT_COL);
                _terminal.set_inverted(true);
                _terminal.write_chars(line + STATE_TEXT_COL, 2);
                _terminal.set_inverted(false);
                _terminal.write_chars(line + STATE_TEXT_COL + 2,
                                      (uint16_t)(n - STATE_TEXT_COL - 2));
            } else {
                _terminal.write_chars(line, n);
            }
            _terminal.clear_to_eol();
        }

        /** @brief Paint data row at screen @p row for visible entry @p vi: skip
         *  when unchanged, patch only ON/EVENT when the alarm is the same, full
         *  redraw when the identity changed or vi is past the set (blank). Rows
         *  beyond MAX_ROWS always draw. */
        void _draw_data_row(uint16_t row, uint16_t vi) {
            bool    shadowed = row < MAX_ROWS;
            uint8_t new_idx  = (vi < _nvis) ? _vis[vi] : ROW_BLANK;
            if (new_idx == ROW_BLANK) {                       // past the set -> blank
                if (!shadowed || _row[row].idx != ROW_BLANK) {
                    _clear_raw(row);
                    if (shadowed) { _row[row].idx = ROW_BLANK; _row[row].sig = 0; }
                }
                return;
            }
            const Alarms::AlarmCondition& c = _model->condition(_vis[vi]);
            uint32_t sig = _pack(c);
            if (shadowed && _row[row].idx == new_idx) {       // same alarm
                if (_row[row].sig != sig) {
                    if (_cells_fit()) _patch_cells(row, c);
                    else              _draw_full_row(row, c);
                    _row[row].sig = sig;
                }
                return;                                       // sig equal -> skip
            }
            _draw_full_row(row, c);                           // identity changed
            if (shadowed) { _row[row].idx = new_idx; _row[row].sig = sig; }
        }

        /** @brief Request a full (paced) repaint from the top. The repaint
         *  supersedes any deferred scroll - drop it. */
        void _request_repaint() { _scroll_pending = 0; _paint_i = 0; }

        /** @brief Fold a deferred scroll into _top (clamped) and repaint - used
         *  when the screen is stale (set moved or a paint mid-flight) so a one-row
         *  scroll is wrong. */
        void _fold_pending_and_repaint() {
            uint16_t window = _window();
            uint16_t maxtop = _max_top(window);
            int32_t  d = (int32_t)_top + _scroll_pending;
            _top = (uint16_t)(d < 0 ? 0
                                    : (d > (int32_t)maxtop ? (int32_t)maxtop : d));
            _request_repaint();   // clears _scroll_pending
        }

        /** @brief Apply the deferred scroll a row at a time, only while the TX
         *  ring is shallow (cold floor): one scroll_region() shift + the exposed
         *  row fit under the floor, so the buffer never fills and the scroll never
         *  falls back to a full repaint. Stops the moment the ring is no longer
         *  shallow; tick() resumes it. */
        void _drain_scroll() {
            uint16_t window = _window();
            if (window == 0) { _scroll_pending = 0; return; }
            uint16_t data_bottom = _data_bottom();
            uint16_t maxtop = _max_top(window);
            while (_scroll_pending != 0 && _terminal.tx_below_cold()) {
                bool     down    = _scroll_pending > 0;
                uint16_t old_top = _top;
                _terminal.scroll_region(DATA_TOP, data_bottom, /*up=*/down, 1);
                // The scroll shifted the band without redrawing; shift the shadow
                // in lockstep so the next repaint diffs the right rows (the
                // exposed row is repainted just below).
                if (down) {
                    for (uint16_t i = DATA_TOP;
                         i < data_bottom && (uint16_t)(i + 1) < MAX_ROWS; i++)
                        _row[i] = _row[i + 1];
                } else {
                    for (uint16_t i = data_bottom;
                         i > DATA_TOP && i < MAX_ROWS; i--)
                        _row[i] = _row[i - 1];
                }
                _top = down ? (uint16_t)(_top + 1) : (uint16_t)(_top - 1);
                if (down) _draw_data_row(data_bottom,
                                         (uint16_t)(_top + window - 1));
                else      _draw_data_row(DATA_TOP, _top);
                _scroll_pending = (int16_t)(_scroll_pending + (down ? -1 : 1));
                // Repaint a rule only when its arrow flips (top at 0, bottom at
                // maxtop).
                if ((old_top == 0) != (_top == 0))
                    _draw_rule(RULE_ROW, _top ? '^' : '+');
                if ((old_top < maxtop) != (_top < maxtop))
                    _draw_rule(_brule_row(), (_top < maxtop) ? 'v' : '+');
            }
        }

        /** @brief True while a staged repaint is in progress (or pending its first
         *         tick). */
        bool _painting() const { return _paint_i != PAINT_IDLE; }

        /** @brief Paint one staged element by index: 0 = legend (first so a mode
         *  change reads instantly, and only once), then top margin, header, top
         *  rule, data rows, bottom rule, blank above the legend, blank below.
         *  Order/count must match _paint_step()'s total (window + 7). */
        void _paint_element(uint16_t e, uint16_t window) {
            uint16_t legend = _legend_row();
            if      (e == 0)               // legend
                _draw_footer(legend, _nvis,
                             _model ? _model->active_count() : 0);
            else if (e == 1)               _clear_row(1);               // top margin
            else if (e == 2)
                _draw_grid_row(HEADER_ROW, "CODE", "ON",
                               "LAST EVENT", "DESCRIPTION");
            else if (e == 3)
                _draw_rule(RULE_ROW, _hidden_above() ? '^' : '+');
            else if (e < (uint16_t)(4 + window))
                _draw_data_row((uint16_t)(DATA_TOP + (e - 4)),
                               (uint16_t)(_top + (e - 4)));
            else if (e == (uint16_t)(4 + window))                       // bottom rule
                _draw_rule(_brule_row(), _hidden_below(window) ? 'v' : '+');
            else if (e == (uint16_t)(5 + window))     // free above legend
                _clear_row((uint16_t)(legend - 1));
            else                                       // free below legend
                _clear_row((uint16_t)(legend + 1));
        }

        /** @brief Advance the staged repaint: draw at most TX_COLD_FIELDS_PER_TICK
         *  rows this tick, only while the ring is shallow, so a full repaint
         *  spreads over ticks instead of flooding. The view set is collected and
         *  clamped once, on the first element, so the frame stays self-consistent;
         *  a mid-paint change is caught by the next 1 s re-check. */
        void _paint_step() {
            uint16_t window = _window();
            if (window == 0) { _snapshot(); _paint_i = PAINT_IDLE; return; }
            // margin + header + top rule + data + bottom rule + free + legend + free
            uint16_t total = (uint16_t)(window + 7);

            if (_paint_i == 0) {
                _collect();
                uint16_t maxtop = _max_top(window);
                if (_top > maxtop) _top = maxtop;
                // Snapshot the painted set up front, so a change during/after
                // staging reads as _changed() and re-triggers rather than being
                // masked by a completion-time snapshot of a newer set.
                _snapshot();
                // If the set shrank, clear the now-empty rows (and bottom rule) up
                // front so they don't linger as stale content while the redraw
                // walks down.
                uint16_t content = (_nvis < window) ? _nvis : window;
                if (content < _painted_data) {
                    _terminal.clear_rows((uint16_t)(DATA_TOP + content),
                                         _brule_row());
                    // Those rows were blanked without _draw_data_row; invalidate
                    // their shadow so the staged repaint redraws not skips them.
                    for (uint16_t r = (uint16_t)(DATA_TOP + content);
                         r <= _brule_row() && r < MAX_ROWS; r++) {
                        _row[r].idx = ROW_BLANK; _row[r].sig = 0;
                    }
                }
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

        /** @brief Queue a one-line scroll. Scrolling does not change the visible
         *  set, so it reuses _vis/_nvis from the last paint: accumulate the move
         *  into _scroll_pending (clamped) and let _drain_scroll() apply it under
         *  the TX floor. If a repaint is already due, fold the move into a full
         *  repaint instead of scrolling stale rows. */
        void _scroll(bool down) {
            uint16_t window = _window();
            if (window == 0) return;
            uint16_t maxtop = _max_top(window);
            // Where the view sits after this key: top + pending + step, clamped to
            // the set.
            int32_t desired = (int32_t)_top + _scroll_pending + (down ? 1 : -1);
            if (desired < 0) desired = 0;
            else if (desired > (int32_t)maxtop) desired = (int32_t)maxtop;
            int16_t pending = (int16_t)(desired - (int32_t)_top);
            if (pending == _scroll_pending) return;        // already at that end
            _scroll_pending = pending;

            if (_painting() || _changed()) { _fold_pending_and_repaint(); return; }
            // apply now if the ring is shallow; tick() finishes the rest
            _drain_scroll();
        }

        /** @brief Jump the view by a full page and repaint. A large move goes
         *  through the paced full repaint (re-collects/re-clamps _top), not a
         *  burst of one-row shifts. */
        void _page_scroll(bool down) {
            uint16_t window = _window();
            if (window == 0) return;
            uint16_t maxtop = _max_top(window);
            int32_t  cur = (int32_t)_top + _scroll_pending;   // where keyed to
            int32_t  nt  = down ? (cur + window) : (cur - window);
            if (nt < 0) nt = 0; else if (nt > (int32_t)maxtop) nt = (int32_t)maxtop;
            if (nt == cur) return;                            // already at that end
            _top = (uint16_t)nt;
            _request_repaint();                               // clears _scroll_pending
        }

    public:
        /** @brief Bind to a terminal and alarm model. */
        AlarmsPage(AnsiTerminal& terminal, const Alarms::AlarmModel* model)
            : _terminal(terminal), _model(model) { _invalidate_shadow(); }

        /** @name Page interface
         *  @{ */

        const char* name() const override { return "ALARMS"; }

        void handle_text(char c) override {
            if (c == ' ') {                 // cycle filter mode
                _mode = (uint8_t)((_mode + 1) % MODE_COUNT);
                _top  = 0;
                _request_repaint();         // legend is painted first (reactive)
            }
        }

        void handle_command(Command cmd, uint16_t = 1) override {
            switch (cmd) {
                case Command::ENTER:
                    _top = 0; _request_repaint();       // jump to top
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
            _painted_data = 0;    // switch_to cleared the screen; nothing painted
            _invalidate_shadow(); // shadow no longer matches the cleared screen
            _request_repaint();   // repaint from scratch
        }

        void tick(bool active) override {
            _active = active;
            if (!active) return;
            // A staged repaint advances a few rows per tick under the TX floor.
            if (_painting()) { _paint_step(); return; }
            // Finish a deferred scroll as TX drains; if the set moved while it
            // waited, fold the rest into a full repaint instead of scrolling stale
            // rows.
            if (_scroll_pending != 0) {
                if (_changed()) _fold_pending_and_repaint();
                else            _drain_scroll();
                return;
            }
            // Idle: re-check the set once a second (alarms move slowly) and repaint
            // only when it actually moved.
            uint64_t now = HAL::uptime_ms();
            if (now - _last_refresh_ms < Config::PAGE_HOT_MS) return;
            _last_refresh_ms = now;
            if (_changed()) _request_repaint();
        }

        /** @} */
    };

}}

#endif // PAGES_ALARMS_H
