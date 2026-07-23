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
 * move it a window at a time and ENTER jumps back to the top. `a` acknowledges
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
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen scrollable alarm table (see file header). */
    class AlarmsPage : public Page {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Owning console (content-area geometry). */
        Console&                  _console;
        /** @brief The VT device it paints through (cached _console.terminal()). */
        AnsiTerminal&             _terminal;
        /** @brief Alarm source (may be null). Non-const: the `a` key acks the
         *  selected condition through it. */
        Alarms::AlarmModel*       _model;
        /** @brief Mirrors tick(active). */
        bool                      _active   = false;
        /** @brief Filter mode, cycled by SPACE (see MODE_*). Defaults to
         *  RECENT. */
        uint8_t                   _mode     = MODE_RECENT;
        /** @brief First visible data row on screen (scroll offset from the top
         *  of the set), moved to keep the selection cursor in the window. */
        uint16_t                  _top      = 0;
        /** @brief Set on focus (a cleared screen): the next tick submits the
         *  structural chrome and repaints every row. */
        bool                      _repaint_all = false;
        /** @brief A nav / mode / ack / set-change happened: the next tick
         *  recollects and repaints the changed rows. The scheduler paces the
         *  resulting transactions, so the page never staged-paces itself. */
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
        /** @brief Selection cursor: index into the visible set (_vis). The
         *  selected row is drawn inverted; `a` acks its condition. Clamped to the
         *  set in _collect(). */
        uint16_t                  _selected = 0;

        /** @} */

        /** @name Paint shadow
         *  One entry per screen row: what that row last showed, so a repaint
         *  emits only what changed. idx tags the row kind; sig is its change
         *  signature. Each paint is a keyed TX_STRUCT transaction (owner 0xA0,
         *  keyed by screen row); the scheduler paces and coalesces them, so the
         *  page never staged-paces itself. The drawn shadow (_row) is promoted
         *  from the submitted one (_row_sub) only once the row's transaction has
         *  drained (_promote), so dropping a still-queued data-row paint on a
         *  scroll can never desync the shadow. A scroll shifts the kept rows'
         *  drawn shadows to match the DL/IL and poisons the revealed ones.
         *  @{ */
        static constexpr uint16_t MAX_ROWS    = 64;   // rows beyond this redraw
        static constexpr uint8_t  ROW_STRUCT  = 0xFE; // header/rule/margin/blank
        static constexpr uint8_t  ROW_BLANK   = 0xFF; // empty data row / unknown
        static constexpr uint8_t  ROW_POISON  = 0xFD; // force a redraw (scroll reveal)
        static constexpr uint32_t EVENT_EMPTY = 0x3FFFFFFF;  // LAST EVENT empty
        /** @brief idx: alarm table index, ROW_STRUCT, ROW_BLANK, or ROW_POISON.
         *  sig: data row = packed (ON state | event minute); structural row =
         *  FNV(line bytes). */
        struct RowShadow { uint8_t idx; uint32_t sig; bool sel; };
        RowShadow                 _row[MAX_ROWS];       ///< last drawn (on the wire)
        RowShadow                 _row_sub[MAX_ROWS];   ///< last submitted (queued)
        bool                      _row_pend[MAX_ROWS];  ///< a paint is outstanding
        /** @brief The outstanding paint for a row is a full row / clear on
         *  TX_STRUCT (true) or a cell patch on TX_VALUE (false). Promote/drop must
         *  check the right lane, and a value patch must not coalesce over a queued
         *  full row (it would strand the CODE/DESCRIPTION cells). */
        bool                      _row_full[MAX_ROWS];
        /** @brief Owner (high 16 bits) of this page's transaction keys. */
        static constexpr uint16_t OWNER = 0x00A0;
        /** @brief Coalescing key for the paint at screen @p row. */
        static uint32_t _row_key(uint16_t row) {
            return ((uint32_t)OWNER << 16) | (uint32_t)(0x1000 + row);
        }
        /** @brief FNV signature of the last-drawn legend, so it repaints only on
         *  a mode / count change, not on every selection move. */
        uint32_t                  _legend_sig = 0;
        bool                      _legend_valid = false;
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
        /** @brief ON-cell inner width, and the LAST EVENT cell's text column and
         *  inner width, tracking the same layout _build_grid_row() emits: used by
         *  the per-cell patch (_patch_cells) so a value change repaints only the
         *  ON + LAST EVENT cells, not the whole row. */
        static constexpr uint16_t STATE_INNER    = STATE_W - 1;               // 3
        static constexpr uint16_t EVENT_TEXT_COL  = STATE_TEXT_COL + STATE_INNER + 2;
        static constexpr uint16_t EVENT_INNER     = EVENT_W - 1;              // 15
        /** @brief CODE field span (contents only, no "| " border): the only part
         *  inverted for the selection cursor, so a selection move patches one
         *  small field, not a whole row. */
        static constexpr uint16_t CODE_CELL_L    = MARGIN + 2;             // 4
        static constexpr uint16_t CODE_CELL_R    = MARGIN + 2 + CODE_W - 1;// 13
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

        /** @brief Reset the whole paint shadow to "unknown" (forces a redraw)
         *  and clear all pending flags (a cleared screen drops queued paints). */
        void _invalidate_shadow() {
            for (uint16_t r = 0; r < MAX_ROWS; r++) {
                _row[r].idx = ROW_BLANK; _row[r].sig = 0; _row[r].sel = false;
                _row_pend[r] = false; _row_full[r] = true;
            }
            _legend_valid = false;
        }

        /** @brief What screen @p row currently shows or is queued to show. */
        const RowShadow& _intended(uint16_t row) const {
            return _row_pend[row] ? _row_sub[row] : _row[row];
        }

        /** @brief Adopt a drained row's submitted shadow as its drawn shadow, so
         *  the shadow reflects the wire (shifting/dropping it stays consistent). */
        void _promote(uint16_t row) {
            if (row >= MAX_ROWS || !_row_pend[row]) return;
            // A full row / clear rides TX_STRUCT; a cell patch rides TX_VALUE.
            uint8_t lane = _row_full[row] ? Terminal::TX_STRUCT : Terminal::TX_VALUE;
            if (!_terminal.tx_has_key(lane, _row_key(row))) {
                _row[row] = _row_sub[row];
                _row_pend[row] = false;
            }
        }

        /** @brief Record that a fresh paint with shadow @p s was just submitted
         *  for @p row (pending until it drains); @p full tags its lane. */
        void _mark_sub(uint16_t row, const RowShadow& s, bool full) {
            if (row >= MAX_ROWS) return;
            _row_sub[row] = s; _row_pend[row] = true; _row_full[row] = full;
        }

        /** @brief Poison a row's drawn shadow (force a redraw) and drop pending
         *  submit intent; the queued transaction is dropped separately. */
        void _poison_shadow(uint16_t row) {
            if (row >= MAX_ROWS) return;
            _row[row].idx = ROW_POISON; _row[row].sig = 0; _row[row].sel = false;
            _row_pend[row] = false;
        }

        /** @brief FNV-1a over s[0..n), masked to 31 bits: a structural row's
         *         fingerprint. */
        static uint32_t _hash(const char* s, uint16_t n) {
            uint32_t h = 2166136261u;
            for (uint16_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
            return h & 0x7FFFFFFFu;
        }

        /** @brief Draw a structural row through the shadow: skip when its bytes
         *  are unchanged (on the wire or queued), else submit a keyed transaction
         *  and record the fingerprint as pending. */
        void _emit(uint16_t row, const char* line, uint16_t n) {
            uint32_t sig = _hash(line, n);
            if (row < MAX_ROWS) {
                _promote(row);
                const RowShadow& it = _intended(row);
                if (it.idx == ROW_STRUCT && it.sig == sig) return;
            }
            _terminal.tx_begin(Terminal::TX_STRUCT, _row_key(row));
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, n);
            _terminal.clear_to_eol();
            _terminal.tx_commit();
            RowShadow s = { ROW_STRUCT, sig, false };
            _mark_sub(row, s, /*full=*/true);
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

        /** @brief Draw the footer legend: reverse-video mode badge + counts on
         *  the left, SPACE-cycles-mode hint on the right. One keyed transaction,
         *  diff-gated on a content signature so it repaints only on a mode / count
         *  change - never on a selection move (the "legend rewritten each row"
         *  cost). The legend row sits below the data band, so it is never dropped
         *  by a scroll; a plain submit-time signature is enough. */
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

            // Diff gate: skip when nothing the legend shows has changed.
            uint32_t sig = _hash(left, llen);
            sig = (sig * 16777619u) ^ _hash(right, (uint16_t)rn);
            sig = (sig ^ ((uint32_t)rcol << 16) ^ ml) & 0x7FFFFFFFu;
            if (_legend_valid && _legend_sig == sig) return;

            _terminal.tx_begin(Terminal::TX_STRUCT, _row_key(row));
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
            _terminal.tx_commit();
            _legend_sig = sig; _legend_valid = true;
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
                    // demoted by _before()).
                    case MODE_UNMASKED:
                        include = c.active && c.unmasked && !c.user_ack; break;
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

        /** @brief Keep the selection cursor inside the (possibly reshaped)
         *  visible set. */
        void _clamp_selection() {
            if (_nvis == 0) _selected = 0;
            else if (_selected >= _nvis) _selected = (uint16_t)(_nvis - 1);
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
        uint16_t _data_bottom() const { return (uint16_t)(_console.rows() - 5); }
        /** @brief Number of data rows that fit, or 0 if the screen is too
         *  short. */
        uint16_t _window() const {
            uint16_t r = _console.rows();
            return (r >= (uint16_t)(DATA_TOP + 5)) ? (uint16_t)(r - DATA_TOP - 4) : 0;
        }
        /** @brief Largest scroll offset that still fills the window (0 = all
         *         fit). */
        uint16_t _max_top(uint16_t window) const {
            return (_nvis > window) ? (uint16_t)(_nvis - window) : 0;
        }
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

        /** @brief ON-cell render state: 0 blank, 1 "OFF", 2 "ON" normal, 3 "ON"
         *  reverse-video (active and unacknowledged). */
        static uint8_t _on_state(const Alarms::AlarmCondition& c) {
            if (c.active) return c.user_ack ? 2 : 3;
            return c.edge_ms != 0 ? 1 : 0;
        }

        /** @brief Pack a data row's two mutable cells: ON state in the top 2
         *  bits, the LAST EVENT minute (edge_ms/60000, the render resolution)
         *  in the low 30, or EVENT_EMPTY when blank. */
        static uint32_t _pack(const Alarms::AlarmCondition& c) {
            uint32_t ev = (!c.active && c.edge_ms == 0)
                              ? EVENT_EMPTY
                              : (uint32_t)((c.edge_ms / 60000u) & 0x3FFFFFFFu);
            return ((uint32_t)_on_state(c) << 30) | ev;
        }

        /** @brief Invert the CODE cell of @p line for the selection cursor: only
         *  the first cell highlights (a whole-row invert would cost bandwidth for
         *  no gain). Writes the margin + CODE cell, leaving the cursor at
         *  CODE_CELL_R. Used inside a data-row transaction. */
        void _write_code_cell(const char* line, bool selected) {
            _terminal.write_chars(line, CODE_CELL_L);              // margin + "| "
            if (selected) _terminal.set_inverted(true);
            _terminal.write_chars(line + CODE_CELL_L, (uint16_t)(CODE_CELL_R - CODE_CELL_L));
            if (selected) _terminal.set_inverted(false);
        }

        /** @brief Submit a full data row (CODE/ON/EVENT/DESCRIPTION) as one keyed
         *  transaction. Only the CODE cell inverts for the selection cursor; the
         *  state cell shows its own reverse-video "ON" badge while active and
         *  unacked (independent of selection). One inline pass, no partial-cell
         *  patch - a partial paint would be unsafe to coalesce over a queued full
         *  row, and the diff gate already skips unchanged rows. */
        void _draw_full_row(uint16_t row, const Alarms::AlarmCondition& c, bool selected) {
            char ev[16];
            _fmt_event(ev, sizeof(ev), c);
            const char* state = c.active ? "ON" : (c.edge_ms != 0 ? "OFF" : "");
            char     line[160];
            uint16_t n = _build_grid_row(line, sizeof(line), c.code ? c.code : "?",
                                         state, ev, c.desc ? c.desc : "");
            _terminal.tx_begin(Terminal::TX_STRUCT, _row_key(row));
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
            _terminal.tx_commit();
        }

        /** @brief Blank a data row as a keyed transaction (past the set). */
        void _submit_clear(uint16_t row) {
            _terminal.tx_begin(Terminal::TX_STRUCT, _row_key(row));
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.clear_to_eol();
            _terminal.tx_commit();
        }

        /** @brief True when the fixed CODE/ON/EVENT columns fit, so a cell patch is
         *  safe; a narrower screen falls back to a clamped full redraw. */
        bool _cells_fit() const {
            return _terminal.cols() >= (uint16_t)(EVENT_TEXT_COL + EVENT_INNER + 2);
        }

        /** @brief Patch only the cells of @p row that changed (@p do_code = the
         *  CODE cell / selection cursor, @p do_state = the ON badge + LAST EVENT),
         *  as one TX_VALUE transaction. The CODE cell inverts when @p sel; the ON
         *  badge shows its own reverse-video "ON" while active-and-unacked. Both
         *  cells are rewritten whole (no per-glyph diff), so re-submitting is
         *  complete and coalescing-safe. Keeps the static columns untouched. */
        void _patch_row(uint16_t row, const Alarms::AlarmCondition& c, bool sel,
                        bool do_code, bool do_state) {
            _terminal.tx_begin(Terminal::TX_VALUE, _row_key(row));
            if (do_code) {
                char cell[CODE_W + 1];
                snprintf(cell, sizeof(cell), "%-*.*s",
                         (int)(CODE_W - 1), (int)(CODE_W - 1), c.code ? c.code : "?");
                _terminal.move_to({row, CODE_CELL_L});
                if (sel) _terminal.set_inverted(true);
                _terminal.write_chars(cell, (uint16_t)(CODE_CELL_R - CODE_CELL_L));
                if (sel) _terminal.set_inverted(false);
            }
            if (do_state) {
                uint8_t on = _on_state(c);
                _terminal.move_to({row, STATE_TEXT_COL});
                if (on == 3) {                                   // "ON" reverse + pad
                    _terminal.set_inverted(true);  _terminal.write_chars("ON", 2);
                    _terminal.set_inverted(false); _terminal.write_chars(" ", 1);
                } else {
                    _terminal.set_inverted(false);
                    _terminal.write_chars(on == 2 ? "ON " : (on == 1 ? "OFF" : "   "), 3);
                }
                char ev[16];
                _fmt_event(ev, sizeof(ev), c);
                char cell[24];
                int m = snprintf(cell, sizeof(cell), "%-*.*s",
                                 (int)EVENT_INNER, (int)EVENT_INNER, ev);
                if (m < 0) m = 0;
                _terminal.move_to({row, EVENT_TEXT_COL});
                _terminal.write_chars(cell, (uint16_t)std::min((size_t)m, sizeof(cell) - 1));
            }
            _terminal.tx_commit();
        }

        /** @brief Paint data row at screen @p row for visible entry @p vi:
         *  promote a drained paint, skip when the intended shadow is unchanged,
         *  else patch just the mutable cells (same alarm, only ON/EVENT/selection
         *  moved) on TX_VALUE, or submit a full row on TX_STRUCT when the identity
         *  changed / the drawn row is stale (poison/blank/full still queued) / the
         *  cells don't fit. Rows beyond MAX_ROWS always full-draw (unshadowed). */
        void _draw_data_row(uint16_t row, uint16_t vi) {
            bool shadowed = row < MAX_ROWS;
            if (shadowed) _promote(row);
            uint8_t new_idx = (vi < _nvis) ? _vis[vi] : ROW_BLANK;
            if (new_idx == ROW_BLANK) {                       // past set -> blank
                if (!shadowed || _intended(row).idx != ROW_BLANK) {
                    _submit_clear(row);
                    RowShadow s = { ROW_BLANK, 0, false };
                    _mark_sub(row, s, /*full=*/true);
                }
                return;
            }
            const Alarms::AlarmCondition& c = _model->condition(_vis[vi]);
            uint32_t sig = _pack(c);
            bool     sel = (vi == _selected);
            if (!shadowed) { _draw_full_row(row, c, sel); return; }
            const RowShadow& it = _intended(row);
            if (it.idx == new_idx && it.sig == sig && it.sel == sel) return;
            RowShadow ns = { new_idx, sig, sel };
            // A patch touches only the CODE/ON/EVENT cells and leaves the static
            // columns alone, so it is only valid when this same alarm is already on
            // the wire (drawn idx matches), no full row is still queued for it (a
            // patch must not coalesce over it and strand the statics), and the cells
            // fit. Otherwise redraw the whole row.
            const RowShadow& drawn = _row[row];
            bool full = drawn.idx != new_idx || (_row_pend[row] && _row_full[row]) ||
                        !_cells_fit();
            if (full) {
                _draw_full_row(row, c, sel);                  // TX_STRUCT
                _mark_sub(row, ns, /*full=*/true);
            } else {
                // Same alarm already drawn: patch the cells that moved vs the wire.
                bool do_state = drawn.sig != sig;             // ON badge / LAST EVENT
                bool do_code  = drawn.sel != sel;             // selection cursor
                _patch_row(row, c, sel, do_code, do_state);   // TX_VALUE
                _mark_sub(row, ns, /*full=*/false);
            }
        }

        /** @brief Scroll _top so the selection cursor stays in the window. */
        void _follow_selection() {
            uint16_t window = _window();
            if (window == 0) return;
            if (_selected < _top)                    _top = _selected;
            else if (_selected >= _top + window)     _top = (uint16_t)(_selected - window + 1);
            uint16_t maxtop = _max_top(window);
            if (_top > maxtop) _top = maxtop;
        }

        /** @brief Move the selection cursor by @p dir (clamped to the set),
         *  auto-scroll to keep it visible, and mark dirty (the highlight moved).
         *  A scroll shifts the kept rows with a DL/IL transaction; the tick's
         *  repaint then only redraws the revealed / re-highlighted rows. */
        void _move_selection(int dir) {
            if (_nvis == 0) return;
            int32_t s = (int32_t)_selected + dir;
            if (s < 0)               s = 0;
            if (s >= (int32_t)_nvis) s = (int32_t)_nvis - 1;
            _selected = (uint16_t)s;
            uint16_t old_top = _top;
            _follow_selection();
            int32_t td = (int32_t)_top - (int32_t)old_top;
            if (td != 0) {
                uint16_t win = _window();
                uint16_t nd  = (uint16_t)(td < 0 ? -td : td);
                _drop_band_pending(win);
                if (nd < win) { _submit_scroll(td > 0, nd, win); _scroll_shadow(td, win); }
                else          { _poison_band(win); }
            }
            _dirty = true;   // repaint the re-highlighted / revealed rows
        }

        /** @brief Drop every queued data-row transaction in the band and clear
         *  its pending flag (the drawn shadow stays the source of truth). */
        void _drop_band_pending(uint16_t win) {
            for (uint16_t vi = 0; vi < win; vi++) {
                uint16_t row = (uint16_t)(DATA_TOP + vi);
                if (row < MAX_ROWS && _row_pend[row]) {
                    // A pending paint sits on TX_STRUCT (full row) or TX_VALUE
                    // (cell patch); drop from the lane it was queued on.
                    _terminal.tx_drop_key(
                        _row_full[row] ? Terminal::TX_STRUCT : Terminal::TX_VALUE,
                        _row_key(row));
                    _row_pend[row] = false;
                }
            }
        }

        /** @brief Submit the band's DL/IL shift as one atomic TX_STRUCT
         *  transaction (key 0: scrolls never coalesce). */
        void _submit_scroll(bool down, uint16_t nd, uint16_t win) {
            uint16_t band_bot = (uint16_t)(DATA_TOP + win - 1);
            _terminal.tx_begin(Terminal::TX_STRUCT, 0);
            _terminal.scroll_region(DATA_TOP, band_bot, /*up=*/down, nd);
            _terminal.tx_commit();
        }

        /** @brief Poison every row in the band (no DL/IL overlap worth keeping,
         *  e.g. a full-page jump), so the tick's repaint redraws them all. */
        void _poison_band(uint16_t win) {
            for (uint16_t vi = 0; vi < win; vi++) _poison_shadow((uint16_t)(DATA_TOP + vi));
        }

        /** @brief Shift the drawn shadow by @p td rows to track the DL/IL,
         *  poisoning the revealed rows so the tick's repaint redraws them (and
         *  skips the kept rows). The band's pending flags were cleared already. */
        void _scroll_shadow(int32_t td, uint16_t win) {
            uint16_t band_top = DATA_TOP;
            uint16_t band_bot = (uint16_t)(DATA_TOP + win - 1);
            uint16_t n = (uint16_t)(td < 0 ? -td : td);
            if (td > 0) {   // scrolled up: screen row s now shows what row s+n did
                for (uint16_t s = band_top; (uint16_t)(s + n) <= band_bot &&
                                            (uint16_t)(s + n) < MAX_ROWS; s++)
                    _row[s] = _row[s + n];
                for (uint16_t s = (uint16_t)(band_bot - n + 1);
                     s <= band_bot && s < MAX_ROWS; s++) _poison_shadow(s);
            } else {        // scrolled down: screen row s now shows what row s-n did
                for (int s = (int)band_bot; s - (int)n >= (int)band_top &&
                                            s < (int)MAX_ROWS; s--)
                    _row[s] = _row[s - n];
                for (uint16_t s = band_top; s < (uint16_t)(band_top + n) &&
                                            s < MAX_ROWS; s++) _poison_shadow(s);
            }
        }

        /** @brief Acknowledge the selected condition through the model. */
        void _ack_selected() {
            if (_model == nullptr || _selected >= _nvis) return;
            const Alarms::AlarmCondition& c = _model->condition(_vis[_selected]);
            if (c.code != nullptr) _model->ack(c.code);
            _dirty = true;   // ack changes the badge / sort
        }

        /** @brief Submit the static chrome: top margin, header, and the two blank
         *  rows around the legend. The rules and legend carry live state, so they
         *  are submitted (diff-gated) by the per-tick _repaint(). */
        void _submit_structure() {
            uint16_t legend = _legend_row();
            _clear_row(1);                                      // top margin
            _draw_grid_row(HEADER_ROW, "CODE", "ON", "LAST EVENT", "DESCRIPTION");
            _clear_row((uint16_t)(legend - 1));                 // blank above legend
            _clear_row((uint16_t)(legend + 1));                 // blank below legend
        }

        /** @brief Submit the rules, legend, and every changed data row (diff-gated
         *  through the shadow). The full window is walked so a shrunk set blanks
         *  its vacated rows. */
        void _repaint(uint16_t window) {
            _draw_rules(window);
            _draw_footer(_legend_row(), _nvis, _model ? _model->active_count() : 0);
            for (uint16_t vi = 0; vi < window; vi++)
                _draw_data_row((uint16_t)(DATA_TOP + vi), (uint16_t)(_top + vi));
        }

    public:
        /** @brief Bind to a terminal and alarm model. */
        AlarmsPage(Console& console, Alarms::AlarmModel* model)
            : _console(console), _terminal(console.terminal()), _model(model) { _invalidate_shadow(); }

        /** @name Page interface
         *  @{ */

        const char* name() const override { return "ALARMS"; }

        void handle_text(char c) override {
            if (c == ' ') {                 // cycle filter mode
                _mode = (uint8_t)((_mode + 1) % MODE_COUNT);
                _top  = 0;
                _selected = 0;
                _dirty = true;              // reorders the list + legend
            } else if (c == 'a' || c == 'A') {   // ack the selected condition
                _ack_selected();
            }
        }

        void handle_command(Command cmd, uint16_t = 1) override {
            switch (cmd) {
                case Command::ENTER:
                    _top = 0; _selected = 0; _dirty = true;   // jump to top
                    break;
                case Command::UP:
                case Command::CTRL_P:
                    _move_selection(-1);
                    break;
                case Command::DOWN:
                case Command::CTRL_N:
                    _move_selection(+1);
                    break;
                case Command::PAGE_UP:
                    _move_selection(-(int)_window());
                    break;
                case Command::PAGE_DOWN:
                    _move_selection(+(int)_window());
                    break;
                default: break;
            }
        }

        void deactivate() override { _active = false; }

        void focus() override {
            _active = true;
            // switch_to cleared the screen; the shadow no longer matches it.
            _invalidate_shadow();
            _repaint_all = true;   // repaint from scratch
        }

        void tick(bool active) override {
            _active = active;
            if (!active) return;
            // Re-check the set on a slow cadence (alarms move slowly); mark dirty
            // only when the displayed set actually moved.
            uint64_t now = HAL::Clock::uptime_ms();
            if (now - _last_refresh_ms >= Config::PAGE_HOT_MS) {
                _last_refresh_ms = now;
                if (_changed()) _dirty = true;
            }
            if (!_repaint_all && !_dirty) return;
            uint16_t window = _window();
            if (window == 0) { _snapshot(); _repaint_all = false; _dirty = false; return; }
            // Rebuild the view set and re-clamp the window before painting.
            _collect();
            uint16_t maxtop = _max_top(window);
            if (_top > maxtop) _top = maxtop;
            if (_repaint_all) {
                _submit_structure();
                _poison_band(window);   // force every data row to redraw
                _repaint_all = false;
            }
            // Submit the rules, legend, and changed rows; the scheduler paces them.
            _repaint(window);
            _snapshot();
            _dirty = false;
        }

        /** @} */
    };

}}

#endif // PAGES_ALARMS_H
