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
#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/console/field.h"
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
        /** @} */

        /** @name Transaction keys
         *  Every paint is a keyed transaction (owner 0xE0). All of the page's
         *  work rides one lane - TX_STRUCT - so a scroll (submitted while input
         *  is handled) and the value rows (submitted from tick()) stay in strict
         *  FIFO order: the scroll's DL/IL always drains before any row paints over
         *  the shifted band, whatever lane the weighted drain happens to lead
         *  with. Data rows are keyed by screen row so a fresh paint replaces a
         *  still-queued one; the scroll itself carries key 0 (never coalesced -
         *  two scrolls must both apply).
         *  @{ */
        static constexpr uint16_t OWNER      = 0x00E0;
        static constexpr uint32_t KEY_MARGIN = ((uint32_t)OWNER << 16) | 1;
        static constexpr uint32_t KEY_HEADER = ((uint32_t)OWNER << 16) | 2;
        static constexpr uint32_t KEY_TRULE  = ((uint32_t)OWNER << 16) | 3;
        static constexpr uint32_t KEY_BRULE  = ((uint32_t)OWNER << 16) | 4;
        static constexpr uint32_t KEY_FOOTER = ((uint32_t)OWNER << 16) | 5;
        static uint32_t _row_key(uint16_t row) {
            return ((uint32_t)OWNER << 16) | (uint32_t)(0x1000 + row);
        }
        /** @brief Screen column where the VALUE cell's text starts, tracking the
         *  layout _row_text() emits: margin + "| " + group + " | " + name + " | ".
         *  A value change repaints only this cell, never the static group/name. */
        static constexpr uint16_t VALUE_COL = MARGIN + 2 + GROUP_W + 3 + NAME_W + 3;
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
        /** @brief Set on focus (a cleared screen): the next tick submits the
         *  chrome and repaints every row. */
        bool                _repaint_all = false;
        /** @brief Per-window-row cache of the value last *drawn* at that screen
         *  row: a value string for a data row, or the sentinels below for a
         *  poisoned (force-redraw) or blank (past the set) row. Promoted from
         *  _vsub only once the row's transaction has drained (see _repaint_changes),
         *  so it always reflects the wire - dropping a still-queued row paint on a
         *  scroll can never desync it. A scroll shifts the kept rows' entries to
         *  match the DL/IL and poisons the revealed ones. */
        char                _vcache[MAX_CACHE_ROWS][VAL_CAP];
        /** @brief Value of the last *submitted* (queued, not-yet-drained) paint
         *  for each row, and whether one is outstanding. The diff gate compares
         *  the live value against this (when pending) or the drawn cache, so a
         *  coalesced or dropped paint never leaves a value un-redrawn. */
        char                _vsub[MAX_CACHE_ROWS][VAL_CAP];
        bool                _vpend[MAX_CACHE_ROWS] = {};
        /** @brief The pending paint for a row is a whole-row draw (group/name/
         *  borders + value), not just a value cell: a value-cell patch must not
         *  coalesce over it (it would strand the static columns), so while this is
         *  set the row keeps repainting in full until the full draw drains. */
        bool                _vfull[MAX_CACHE_ROWS] = {};
        static constexpr char POISON = '\xFF';   // force a redraw (never a value)
        static constexpr char BLANK  = '\x7F';   // row is past the set (cleared)
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
            _repaint_all = true;     // switch_to cleared the screen; paint fresh
        }

        void tick(bool active) override {
            _active = active;
            if (!active) return;
            if (_repaint_all) {
                _submit_structure();
                _poison_cache();     // force every row to redraw
                _repaint_all = false;
            }
            // Submit any row whose value changed. No throttle: the diff gate only
            // submits changes and coalescing collapses churn on the same row, so
            // the scheduler (not a timer) paces the wire.
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

        /** @brief Diff width of the VALUE cell: the display width, clamped to the
         *  31-column per-cell mask (a monitor value is short, so the cells beyond
         *  it stay padding either way). */
        uint16_t _vw() const {
            uint16_t w = _value_w();
            return (w > 31) ? 31 : w;
        }

        /** @brief Render variable @p r's value into @p cf, space-padded to _vw()
         *  (so a byte index maps to a fixed cell for the per-cell diff). */
        void _value_cf(uint16_t r, char* cf) const {
            char raw[VAL_CAP];
            _value_text(r, raw, sizeof(raw));
            Terminal::Field::render(cf, raw, _vw());
        }

        /** @brief Repaint only the @p emit cells of the VALUE cell (@p cf) at
         *  screen @p row, as one transaction keyed by the row. Ongoing value
         *  churn rides the lower TX_VALUE lane, so a scroll and its revealed rows
         *  (TX_STRUCT) always drain first - the new line paints right after the
         *  scroll, not behind a scatter of value updates. */
        void _draw_value(uint16_t row, const char* cf, uint32_t emit) {
            _terminal.tx_begin(Terminal::TX_VALUE, _row_key(row));
            _terminal.paint_field_runs(row, VALUE_COL, cf, _vw(), emit, 0);
            _terminal.tx_commit();
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

        /** @brief Submit data row @p r at screen @p row as a TX_STRUCT
         *  transaction keyed by the screen row. */
        void _draw_row(uint16_t r, uint16_t row) {
            char line[LINE_CAP];
            uint16_t n = _row_text(r, line, sizeof(line));
            _terminal.tx_begin(Terminal::TX_STRUCT, _row_key(row));
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, n);
            _terminal.clear_to_eol();
            _terminal.tx_commit();
        }

        /** @brief Blank screen @p row (a data row past the end of the set),
         *  keyed like the data row so it coalesces with a stale paint. */
        void _clear_row(uint16_t row) {
            _terminal.tx_begin(Terminal::TX_STRUCT, _row_key(row));
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.clear_to_eol();
            _terminal.tx_commit();
        }

        void _draw_header() {
            char line[LINE_CAP];
            int n = snprintf(line, sizeof(line), "%*s| %-*.*s | %-*.*s | %-*.*s |",
                             (int)MARGIN, "", GROUP_W, GROUP_W, "GROUP",
                             NAME_W, NAME_W, "VARIABLE", _value_w(), _value_w(), "VALUE");
            _terminal.tx_begin(Terminal::TX_STRUCT, KEY_HEADER);
            _terminal.move_to({HEADER_ROW, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, (n < 0) ? 0 : (uint16_t)n);
            _terminal.clear_to_eol();
            _terminal.tx_commit();
        }

        /** @brief Draw a rule row; @p cap ('^' / 'v' / '+') marks hidden rows. */
        void _draw_rule(uint16_t row, char cap, uint32_t key) {
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
            _terminal.tx_begin(Terminal::TX_STRUCT, key);
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, (uint16_t)k);
            _terminal.clear_to_eol();
            _terminal.tx_commit();
        }

        /** @brief Submit both rules, capping with '^' / 'v' when rows are hidden
         *  above / below the window. */
        void _draw_rules() {
            uint16_t win = _window(), total = _total();
            _draw_rule(TRULE_ROW, _top > 0 ? '^' : '+', KEY_TRULE);
            _draw_rule((uint16_t)(_console.rows() - 2),
                       (uint16_t)(_top + win) < total ? 'v' : '+', KEY_BRULE);
        }

        void _draw_footer() {
            uint16_t total = _total(), win = _window();
            uint16_t last  = (uint16_t)(_top + (win < total ? win : total));
            char line[LINE_CAP];
            int n = snprintf(line, sizeof(line),
                             " UP/DOWN scroll   PAGE UP/DOWN page      %u-%u / %u",
                             (unsigned)(total ? _top + 1 : 0), (unsigned)last, (unsigned)total);
            _terminal.tx_begin(Terminal::TX_STRUCT, KEY_FOOTER);
            _terminal.move_to({(uint16_t)(_console.rows() - 1), 0});
            _terminal.set_inverted(false);
            _terminal.write_chars(line, (n < 0) ? 0 : (uint16_t)n);
            _terminal.clear_to_eol();
            _terminal.tx_commit();
        }

        // Structure + cache helpers
        // --------------------------------------------------------------------

        /** @brief Submit the static chrome (margin, header, both rules, footer). */
        void _submit_structure() {
            _terminal.tx_begin(Terminal::TX_STRUCT, KEY_MARGIN);
            _terminal.move_to({TOP_ROW, 0});
            _terminal.set_inverted(false);
            _terminal.clear_to_eol();
            _terminal.tx_commit();
            _draw_header();
            _draw_rules();
            _draw_footer();
        }

        /** @brief What screen row @p vi currently shows or is queued to show: the
         *  submitted value while a paint is outstanding, else the drawn value. */
        const char* _intended(uint16_t vi) const {
            return _vpend[vi] ? _vsub[vi] : _vcache[vi];
        }

        /** @brief Adopt a drained row's submitted value as its drawn value, so the
         *  cache reflects the wire (and shifting/dropping it stays consistent). */
        void _promote(uint16_t vi, uint16_t row) {
            // A full row / blank rides TX_STRUCT; a value patch rides TX_VALUE.
            uint8_t lane = _vfull[vi] ? Terminal::TX_STRUCT : Terminal::TX_VALUE;
            if (_vpend[vi] && !_terminal.tx_has_key(lane, _row_key(row))) {
                memcpy(_vcache[vi], _vsub[vi], VAL_CAP);
                _vpend[vi] = false;
            }
        }

        /** @brief Record that a fresh paint carrying @p val was just submitted for
         *  row @p vi (pending until it drains). */
        void _mark_sub(uint16_t vi, const char* val) {
            memcpy(_vsub[vi], val, VAL_CAP);
            _vpend[vi] = true;
        }

        /** @brief Poison a row's drawn cache (force a redraw) and drop any pending
         *  submit intent; the queued transaction itself is dropped separately. */
        void _poison_row(uint16_t vi) {
            if (vi >= MAX_CACHE_ROWS) return;
            _vcache[vi][0] = POISON; _vcache[vi][1] = '\0';
            _vpend[vi] = false;
        }

        void _poison_cache() {
            for (uint16_t vi = 0; vi < MAX_CACHE_ROWS; vi++) _poison_row(vi);
        }

        // Scroll
        // --------------------------------------------------------------------

        void _scroll(int delta, uint16_t maxtop) {
            int32_t t = (int32_t)_top + delta;
            if (t < 0) t = 0;
            if (t > (int32_t)maxtop) t = (int32_t)maxtop;
            int32_t d = t - (int32_t)_top;
            if (d == 0) return;
            _top = (uint16_t)t;
            uint16_t win = _window();
            uint16_t nd  = (uint16_t)(d < 0 ? -d : d);
            // Drop the band's queued row paints: they were keyed to the old
            // content and would land on the wrong variable after the shift. Safe
            // because the drawn cache (not the dropped submits) is authoritative.
            _drop_band_pending(win);
            if (nd < win) {
                // Overlap kept: shift it with a DL/IL scroll transaction and let
                // _repaint_changes() (this tick) paint the poisoned revealed rows.
                _submit_scroll(d > 0, nd, win);
                _shift_cache(d > 0, nd, win);
            } else {
                // No overlap (a full page / ends jump): every row is revealed.
                _poison_cache();
            }
            // The scroll arrows and the position counter moved.
            _draw_rules();
            _draw_footer();
        }

        /** @brief Submit the band's DL/IL shift as one atomic TX_STRUCT
         *  transaction (key 0: scrolls never coalesce). */
        void _submit_scroll(bool down, uint16_t nd, uint16_t win) {
            uint16_t band_bot = (uint16_t)(DATA_TOP + win - 1);
            _terminal.tx_begin(Terminal::TX_STRUCT, 0);
            _terminal.scroll_region(DATA_TOP, band_bot, /*up=*/down, nd);
            _terminal.tx_commit();
        }

        /** @brief Drop every queued data-row transaction in the band and clear its
         *  pending flag (the drawn cache stays the source of truth). */
        void _drop_band_pending(uint16_t win) {
            for (uint16_t vi = 0; vi < win && vi < MAX_CACHE_ROWS; vi++) {
                if (!_vpend[vi]) continue;
                // A band row's pending paint may be a full row (TX_STRUCT) or a
                // value patch (TX_VALUE); drop whichever is queued.
                uint32_t key = _row_key((uint16_t)(DATA_TOP + vi));
                _terminal.tx_drop_key(Terminal::TX_STRUCT, key);
                _terminal.tx_drop_key(Terminal::TX_VALUE, key);
                _vpend[vi] = false;
            }
        }

        /** @brief Shift the drawn-value cache by @p nd rows to track the DL/IL,
         *  poisoning the newly-revealed rows so _repaint_changes() redraws them.
         *  The band's pending flags were already cleared by _drop_band_pending. */
        void _shift_cache(bool down, uint16_t nd, uint16_t win) {
            if (down) {                                   // content up, reveal bottom
                for (uint16_t vi = 0; (uint16_t)(vi + nd) < win &&
                                      (uint16_t)(vi + nd) < MAX_CACHE_ROWS; vi++)
                    memcpy(_vcache[vi], _vcache[vi + nd], VAL_CAP);
                for (uint16_t vi = (uint16_t)(win - nd); vi < win; vi++) _poison_row(vi);
            } else {                                      // content down, reveal top
                for (int vi = (int)win - 1; vi >= (int)nd && vi < (int)MAX_CACHE_ROWS; vi--)
                    memcpy(_vcache[vi], _vcache[vi - nd], VAL_CAP);
                for (uint16_t vi = 0; vi < nd; vi++) _poison_row(vi);
            }
        }

        /** @brief Submit any window row whose value no longer matches what is (or
         *  is queued to be) on screen: a data row that changed or was poisoned by a
         *  scroll, or a row that fell past the end of the set (blanked). Promotes a
         *  drained row's cache first, and diffs against the intended value so a
         *  coalesced/dropped paint is re-submitted rather than lost. */
        void _repaint_changes() {
            uint16_t win = _window(), total = _total();
            uint16_t vw = _vw();
            char cf[VAL_CAP];
            for (uint16_t vi = 0; vi < win; vi++) {
                uint16_t r    = (uint16_t)(_top + vi);
                uint16_t row  = (uint16_t)(DATA_TOP + vi);
                bool cached   = vi < MAX_CACHE_ROWS;
                if (cached) _promote(vi, row);
                if (r < total) {
                    _value_cf(r, cf);
                    if (!cached) { _draw_row(r, row); continue; }
                    const char* intended = _intended(vi);
                    // No change vs what is drawn / queued: skip.
                    if (intended[0] != POISON && intended[0] != BLANK &&
                        memcmp(cf, intended, vw) == 0) continue;
                    // A whole-row draw is needed when the static columns aren't on
                    // screen (a poisoned/blanked row) or a whole-row draw is still
                    // queued (a value patch would strand them by coalescing over it);
                    // otherwise repaint only the value cells that moved.
                    bool need_full = _vcache[vi][0] == POISON ||
                                     _vcache[vi][0] == BLANK ||
                                     (_vpend[vi] && _vfull[vi]);
                    if (need_full) {
                        _draw_row(r, row);
                        _mark_sub(vi, cf); _vfull[vi] = true;
                    } else {
                        // Emit the cells changed since the DRAWN value (complete,
                        // so a coalescing re-submit never strands a cell).
                        uint32_t emit = Terminal::Field::changed(cf, _vcache[vi], vw, false);
                        if (emit == 0) continue;
                        _draw_value(row, cf, emit);
                        _mark_sub(vi, cf); _vfull[vi] = false;
                    }
                } else if (!cached || _intended(vi)[0] != BLANK) {
                    _clear_row(row);   // TX_STRUCT, like a full row
                    if (cached) {
                        _vsub[vi][0] = BLANK; _vsub[vi][1] = '\0';
                        _vpend[vi] = true; _vfull[vi] = true;
                    }
                }
            }
        }
    };

}}

#endif // PAGES_MONITOR_H
