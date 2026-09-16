/**
 * @file reconcile.cpp
 * @brief AnsiTerminal::present() - FRONT reconciled to the wire: the row slides
 *        it finds, then each row's sideways slide and changed cells, paced,
 *        then the cursor. See terminal.h.
 */
#include "libbotanik/console/terminal.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "libbotanik/console/ansi.h"


namespace Botanik { namespace Terminal {

    bool AnsiTerminal::_go(const CursorPos& target_in) {
        // The terminal clamps a position past the edge; track where it lands.
        const CursorPos target = { std::min(target_in.row, (uint16_t)(_rows - 1)),
                                   std::min(target_in.col, (uint16_t)(_cols - 1)) };
        if (target == _hw_pos && !_hw_at_eol) return true;
        bool ok;
        if (_hw_at_eol) {
            ok = Ansi::goto_absolute(_serial, target);
        } else if (target.row == _hw_pos.row) {
            if (target.col == 0)                 ok = Ansi::move_row_start(_serial);
            else if (target.col < _hw_pos.col)   ok = Ansi::move_left(_serial, (uint16_t)(_hw_pos.col - target.col));
            else if (target.col > _hw_pos.col)   ok = Ansi::move_right(_serial, (uint16_t)(target.col - _hw_pos.col));
            else                                 ok = true;
        } else if (target.col == _hw_pos.col) {
            if (target.row < _hw_pos.row) ok = Ansi::move_up(_serial, (uint16_t)(_hw_pos.row - target.row));
            else                          ok = Ansi::move_down(_serial, (uint16_t)(target.row - _hw_pos.row));
        } else if (target.col == 0 && target.row > _hw_pos.row &&
                   (target.row - _hw_pos.row) < 4 && target.row < (uint16_t)(_rows - 1)) {
            ok = Ansi::down_to_row_start(_serial, (uint16_t)(target.row - _hw_pos.row));
        } else {
            ok = Ansi::goto_absolute(_serial, target);
        }
        if (ok) { _hw_pos = target; _hw_at_eol = false; }
        return ok;
    }

    uint16_t AnsiTerminal::_run_cost(uint16_t row, uint16_t a, uint16_t b) const {
        // Leading absolute move (worst case) + one SGR per attribute transition +
        // one byte per glyph. Conservative upper bound for the ring/budget gate.
        uint16_t cost = 8;
        bool inv = _hw_inverted;
        for (uint16_t c = a; c < b; c++) {
            bool ci = (_front[_idx(row, c)] & CELL_INV) != 0;
            if (ci != inv) { cost += 5; inv = ci; }
            cost += 1;
        }
        return cost;
    }

    void AnsiTerminal::_set_hw_inverted(bool inv) {
        // Sync the wire's reverse-video state, emitting the SGR only on a change.
        if (inv == _hw_inverted) return;
        _serial.write(Ansi::CSI, sizeof(Ansi::CSI) - 1);
        _serial.write(inv ? Ansi::ATTR_NEG : Ansi::ATTR_NONE,
                      inv ? sizeof(Ansi::ATTR_NEG) - 1 : sizeof(Ansi::ATTR_NONE) - 1);
        _hw_inverted = inv;
    }

    bool AnsiTerminal::_set_hw_insert(bool on) {
        if (on == _hw_insert) return true;
        if (!Ansi::set_insert_mode(_serial, on)) return false;
        _hw_insert = on;
        return true;
    }

    uint16_t AnsiTerminal::_put_run(uint16_t row, uint16_t a, uint16_t b, bool insert) {
        uint16_t before = tx_used();
        if (!_go({row, a})) return 0;
        if (!_set_hw_insert(insert)) return 0;
        char buf[GRID_COLS];
        uint16_t k = a;
        while (k < b) {
            bool inv = (_front[_idx(row, k)] & CELL_INV) != 0;
            _set_hw_inverted(inv);
            uint16_t n = 0, m = k;
            while (m < b && (((_front[_idx(row, m)] & CELL_INV) != 0) == inv)) {
                buf[n++] = (char)(_front[_idx(row, m)] & CELL_CH);
                _back[_idx(row, m)] = _front[_idx(row, m)];   // reconcile
                m++;
            }
            _serial.write(buf, n);
            k = m;
        }
        // Track the real cursor: it advanced to column b (parked if it hit the edge).
        if (b >= _cols) { _hw_pos.col = (uint16_t)(_cols - 1); _hw_at_eol = true; }
        else            { _hw_pos.col = b; _hw_at_eol = false; }
        _hw_pos.row = row;
        return (uint16_t)(tx_used() - before);
    }

    void AnsiTerminal::present() {
        // Row slides first, so the cell diff only pays for what they could not
        // carry; none is looked for while the ring could not take one.
        Slide slide;
        for (uint8_t i = 0; i < MAX_SLIDES; i++) {
            if (_serial.availableForWrite() < 2 * SLIDE_BYTES || !_find_slide(slide)) break;
            if (!_emit_slide(slide)) break;
        }
        // Park only once the frame is fully reconciled: mid-repaint the runs are
        // moving the cursor anyway, so re-homing it would burn a move per tick.
        if (_emit_diff()) _park_cursor();
        // IRM survives cursor motion, so a frame's insert-shifts on several rows
        // share one SM4; the wire leaves the frame in replace mode.
        (void)_set_hw_insert(false);
    }

    bool AnsiTerminal::_fits(uint16_t cost, uint16_t budget) const {
        if (_serial.availableForWrite() < cost) return false;     // ring full
        // A write that needs the whole budget or more can never fit inside one,
        // so it goes out as soon as the ring is shallow (and ends the tick).
        // This has to be >=, not >: tx_used() never reports an empty ring as 0,
        // so budget is always at least a byte under _tx_cold_depth, and a write
        // costing exactly _tx_cold_depth would otherwise be too big to fit and
        // not big enough to escape - stuck for good.
        return cost >= _tx_cold_depth ? budget != 0 : cost <= budget;
    }

    uint16_t AnsiTerminal::_row_cost(const uint8_t* fr, const uint8_t* bk, uint16_t* spent) const {
        // Each differing cell is a byte, and each gap between them its width up
        // to HOP_BYTES; the stale blank tail is spaces or one EL.
        const uint16_t e = _blank_tail(fr);
        uint16_t cells = 0, stale = 0, end = 0;
        bool reached = false;
        for (uint16_t c = 0; c < _cols; c++) {
            if (spent) spent[c] = cells;
            if (fr[c] == bk[c]) continue;
            if (c >= e) { stale++; continue; }
            if (reached) cells = (uint16_t)(cells + std::min<uint16_t>((uint16_t)(c - end), (uint16_t)HOP_BYTES));
            cells = (uint16_t)(cells + 1);
            reached = true;
            end = (uint16_t)(c + 1);
        }
        if (spent) spent[_cols] = cells;
        if (!reached && stale == 0) return 0;
        return (uint16_t)(ROW_ENTRY_BYTES + cells + (stale != 0 ? _el_cost(stale) : 0));
    }

    bool AnsiTerminal::_find_slide(Slide& out) const {
        const uint16_t rows = _rows;
        uint16_t r = 0;
        while (r < rows && memcmp(&_front[_idx(r, 0)], &_back[_idx(r, 0)], _cols) == 0) r++;
        if (r == rows) return false;

        // What each FRONT row costs against the BACK row it would face: its own
        // (`stay`, 0 when they match), a blank one, and below, the one an offset
        // brings. Prefix sums, so any band is priced in O(1).
        uint8_t blank_row[GRID_COLS];
        memset(blank_row, CELL_BLANK, _cols);
        int32_t stay[GRID_ROWS + 1], blank[GRID_ROWS + 1], moved[GRID_ROWS + 1];
        stay[0] = blank[0] = moved[0] = 0;
        for (r = 0; r < rows; r++) {
            const uint8_t* fr = &_front[_idx(r, 0)];
            stay [r + 1] = stay [r] + _row_cost(fr, &_back[_idx(r, 0)]);
            blank[r + 1] = blank[r] + _row_cost(fr, blank_row);
        }

        // Offsets worth trying: the short ones a growing editor or a stepping
        // table makes, whose rows may also have changed, and any a changed row
        // shows exactly in some other BACK row. Blank rows seed nothing: every
        // blank row matches them.
        int16_t offsets[MAX_OFFSETS] = { -1, 1, -2, 2, -3, 3 };
        uint8_t n_offsets = 6;
        for (r = 0; r < rows && n_offsets < MAX_OFFSETS; r++) {
            const uint8_t* fr = &_front[_idx(r, 0)];
            if (stay[r + 1] == stay[r] || memcmp(fr, blank_row, _cols) == 0) continue;
            for (uint16_t src = 0; src < rows && n_offsets < MAX_OFFSETS; src++) {
                if (src == r || memcmp(fr, &_back[_idx(src, 0)], _cols) != 0) continue;
                const int16_t d = (int16_t)((int16_t)r - (int16_t)src);
                bool known = false;
                for (uint8_t k = 0; k < n_offsets; k++) known |= offsets[k] == d;
                if (!known) offsets[n_offsets++] = d;
            }
        }

        // Every band an IL/DL pair can slide by each offset: the rows it keeps
        // take their new source, the rows it opens are blank, and the pair costs
        // two moves and two escapes, one of each when the band reaches the last
        // row. The last candidate slides a band by its whole height, clearing it.
        int32_t best = 0;
        for (uint8_t k = 0; k <= n_offsets; k++) {
            const bool     clear = k == n_offsets;
            const bool     up    = clear || offsets[k] < 0;
            const uint16_t n     = clear ? rows : (uint16_t)(up ? -offsets[k] : offsets[k]);
            if (!clear) {
                for (r = 0; r < rows; r++) {
                    const int32_t src = up ? (int32_t)r + n : (int32_t)r - n;
                    moved[r + 1] = moved[r] + ((src < 0 || src >= rows) ? 0
                                 : _row_cost(&_front[_idx(r, 0)], &_back[_idx((uint16_t)src, 0)]));
                }
            }
            for (uint16_t top = 0; top < rows; top++) {
                for (uint16_t bottom = clear ? top : (uint16_t)(top + n); bottom < rows; bottom++) {
                    const uint16_t m      = std::min<uint16_t>(n, (uint16_t)(bottom - top + 1));
                    const uint16_t edge   = up ? (uint16_t)(bottom - m + 1) : (uint16_t)(top + m);
                    const int32_t  opened = up ? blank[bottom + 1] - blank[edge] : blank[edge] - blank[top];
                    const int32_t  kept   = clear ? 0 : up ? moved[edge] - moved[top]
                                                           : moved[bottom + 1] - moved[edge];
                    const int32_t  pair   = bottom + 1 == rows ? SLIDE_BYTES : 2 * SLIDE_BYTES;
                    const int32_t  gain   = (stay[bottom + 1] - stay[top]) - kept - opened - pair;
                    if (gain > best) { best = gain; out = { top, bottom, m, up }; }
                }
            }
        }
        return best > 0;
    }

    bool AnsiTerminal::_emit_slide(const Slide& s) {
        // DL/IL fill the rows they free using the current attributes on terminals
        // with background-colour erase, and _shift_back blanks them PLAIN.
        _set_hw_inverted(false);
        const bool     restore = s.bottom < (uint16_t)(_rows - 1);
        const uint16_t back    = (uint16_t)(s.bottom - s.n + 1);
        bool ok;
        if (s.up) {
            ok = _go({s.top, 0}) && Ansi::delete_rows(_serial, s.n);
            if (ok && restore) ok = _go({back, 0}) && Ansi::insert_rows(_serial, s.n);
        } else {
            ok = !restore || (_go({back, 0}) && Ansi::delete_rows(_serial, s.n));
            if (ok) ok = _go({s.top, 0}) && Ansi::insert_rows(_serial, s.n);
        }
        // BACK follows only a pair that reached the wire whole; a half-sent one
        // leaves the band for the cell diff, which repairs whatever it did.
        if (ok) { _shift_back(s); restart_scan(); }
        return ok;
    }

    void AnsiTerminal::_shift_back(const Slide& s) {
        const uint16_t kept  = (uint16_t)(s.bottom - s.top + 1 - s.n);
        const uint16_t freed = s.up ? (uint16_t)(s.bottom - s.n + 1) : s.top;
        memmove(&_back[_idx(s.up ? s.top : (uint16_t)(s.top + s.n), 0)],
                &_back[_idx(s.up ? (uint16_t)(s.top + s.n) : s.top, 0)], (size_t)kept * _cols);
        memset(&_back[_idx(freed, 0)], CELL_BLANK, (size_t)s.n * _cols);
    }

    bool AnsiTerminal::_find_shift(uint16_t row, Shift& out) const {
        const uint8_t* fr = &_front[_idx(row, 0)];
        const uint8_t* bk = &_back [_idx(row, 0)];
        uint16_t first = 0;
        while (first < _cols && fr[first] == bk[first]) first++;
        uint16_t last = (uint16_t)(_cols - 1);
        while (last > first && fr[last] == bk[last]) last--;
        // Short diffs are cheaper to just rewrite than to bracket in an escape.
        if (first >= _cols || (uint16_t)(last - first + 1) < SHIFT_MIN_SPAN) return false;

        // The run loop's price for the row, and in `spent[c]` for what precedes
        // column c, which a slide starting there still has to write.
        uint16_t spent[GRID_COLS + 1];
        const int32_t plain = (int32_t)_row_cost(fr, bk, spent) - ROW_ENTRY_BYTES;

        // For each width, the longest suffix that is BACK slid by it, and the
        // column the slide starts at. A right slide writes the n cells it opens
        // inside SM4/RM4; a left slide is one DCH, then whatever FRONT wants in
        // the n cells it blanks at the edge. The scan from the right stops at
        // the first mismatch, which for a width that is not the slide is usually
        // the first cell. A slide carrying only blanks moves nothing a rewrite
        // would pay for.
        int32_t gain = 0;
        for (uint16_t n = 1; (uint16_t)(n + SHIFT_MIN_SPAN) <= _cols; n++) {
            if (plain - (int32_t)n - 8 <= gain) break;   // no wider slide can beat it
            bool moves = false;
            uint16_t c = (uint16_t)(_cols - n);
            while (c > first && fr[c - 1 + n] == bk[c - 1]) { moves |= bk[c - 1] != CELL_BLANK; c--; }
            int32_t g = plain - ((int32_t)spent[c] + (int32_t)n + 8);
            if (moves && g > gain) { gain = g; out = { c, n, true }; }

            moves = false;
            c = (uint16_t)(_cols - n);
            while (c > first && fr[c - 1] == bk[c - 1 + n]) { moves |= fr[c - 1] != CELL_BLANK; c--; }
            int32_t tail = 0;
            for (uint16_t t = (uint16_t)(_cols - n); t < _cols; t++)
                if (fr[t] != CELL_BLANK) tail++;
            g = plain - ((int32_t)spent[c] + 6 + tail + (tail != 0 ? 8 : 0));
            if (moves && g > gain) { gain = g; out = { c, n, false }; }
        }
        return gain > 0;
    }

    bool AnsiTerminal::_emit_runs(uint16_t row, uint16_t from, uint16_t to, uint16_t& budget) {
        const uint8_t* fr = &_front[_idx(row, 0)];
        const uint8_t* bk = &_back [_idx(row, 0)];
        for (uint16_t col = from; col < to;) {
            if (fr[col] == bk[col]) { col++; continue; }
            // A run goes on across a gap no wider than BRIDGE_CELLS under the same
            // pen: rewriting what the wire already shows is no dearer than the move.
            uint16_t b = col;
            for (;;) {
                while (b < to && fr[b] != bk[b]) b++;
                uint16_t g = b;
                while (g < to && fr[g] == bk[g] && (fr[g] & CELL_INV) == (fr[b - 1] & CELL_INV)) g++;
                if (g >= to || fr[g] == bk[g] || (uint16_t)(g - b) > BRIDGE_CELLS) break;
                b = g;
            }
            const uint16_t cost = _run_cost(row, col, b);
            if (!_fits(cost, budget)) return false;
            const uint16_t sent = _put_run(row, col, b);
            if (sent == 0) return false;
            budget = _spend(budget, sent);
            if (cost >= _tx_cold_depth || budget == 0) return false;
            col = b;
        }
        return true;
    }

    bool AnsiTerminal::_reconcile_row(uint16_t row, uint16_t& budget) {
        // Nothing pending: skip in one memcmp rather than two per-cell walks.
        if (memcmp(&_front[_idx(row, 0)], &_back[_idx(row, 0)], _cols) == 0) return true;

        // A sideways slide is one escape, after the cells that changed before
        // it. Both go out this tick or neither does: a slide the budget cannot
        // hold is owed the next tick, not the whole-row rewrite, which would
        // escape the very budget the slide was refused for.
        Shift sh;
        if (_find_shift(row, sh)) {
            const uint16_t escape = sh.right ? (uint16_t)(4 + sh.n + 4 + 5) : (uint16_t)(6 + 5);
            if (!_fits((uint16_t)(_run_cost(row, 0, sh.at) + escape), budget)) return false;
            if (!_emit_runs(row, 0, sh.at, budget) || !_go({row, sh.at})) return false;
            const uint16_t before = tx_used();
            uint8_t* const bk = &_back[_idx(row, 0)];
            if (sh.right) {
                // Insert mode: the glyphs written push the rest of the row right.
                // Mirror that in BACK first, so the run stamps the opened cells.
                if (!_set_hw_insert(true)) return false;
                memmove(&bk[sh.at + sh.n], &bk[sh.at], (size_t)(_cols - sh.at - sh.n));
                _put_run(row, sh.at, (uint16_t)(sh.at + sh.n), /*insert=*/true);
            } else {
                // DCH fills the cells it frees using the current attributes on
                // terminals with background-colour erase, so the pen goes plain.
                _set_hw_inverted(false);
                if (!Ansi::delete_chars(_serial, sh.n)) return false;
                memmove(&bk[sh.at], &bk[sh.at + sh.n], (size_t)(_cols - sh.at - sh.n));
                memset(&bk[_cols - sh.n], CELL_BLANK, sh.n);
                _hw_at_eol = false;
            }
            budget = _spend(budget, (uint16_t)(tx_used() - before));
            if (budget == 0) return false;
        }

        // Past a few stale cells in FRONT's blank tail, one EL erases them for
        // three bytes where the runs would write a space each; it goes out last,
        // so the cursor is already near it.
        const uint16_t e = _blank_tail(&_front[_idx(row, 0)]);
        uint16_t stale = 0;
        for (uint16_t c = e; c < _cols; c++)
            if (_back[_idx(row, c)] != CELL_BLANK) stale++;
        const bool erase = stale >= EL_MIN_CELLS;
        if (!_emit_runs(row, 0, erase ? e : _cols, budget)) return false;
        if (!erase) return true;
        // Move + EL + a possible SGR: EL fills with the current attributes on
        // a terminal with background-colour erase, so the pen goes plain first.
        if (!_fits((uint16_t)(8 + 3 + (_hw_inverted ? 5 : 0)), budget)) return false;
        const uint16_t before = tx_used();
        if (!_go({row, e})) return false;
        _set_hw_inverted(false);
        if (!Ansi::erase_to_eol(_serial)) return false;
        memset(&_back[_idx(row, e)], CELL_BLANK, (size_t)(_cols - e));
        budget = _spend(budget, (uint16_t)(tx_used() - before));
        return true;
    }

    bool AnsiTerminal::_emit_diff() {
        // Walk FRONT vs BACK by priority class and emit each changed run,
        // all-or-nothing (the ring drops silently, so a partial run would be a
        // torn frame) and paced under this tick's budget. @return true when the
        // screen is fully reconciled.
        uint16_t used   = tx_used();
        uint16_t budget = (used < _tx_cold_depth) ? (uint16_t)(_tx_cold_depth - used) : 0;
        for (uint8_t prio = 0; prio < PRIO_COUNT; prio++) {
            // Within a class the walk only ever runs downward: it resumes where
            // the budget ran out, stops at the bottom, then starts the next pass
            // at the top. That keeps a tick's emissions in row order (content
            // reads top-down) while stopping budget-hungry upper rows from
            // winning the budget every tick. See @ref dev_screen.
            uint16_t start = _resume[prio];
            for (uint16_t row = start; row < _rows; row++) {
                if (_row_prio[row] != prio) continue;
                if (!_reconcile_row(row, budget)) {
                    // Pick this row up again next tick: it is either unfinished or
                    // untried, and either way it is owed the next slot.
                    _resume[prio] = (uint8_t)row;
                    return false;
                }
            }
            _resume[prio] = 0;      // next pass starts at the top
            // Reaching the bottom is not being drained: the rows above `start`
            // may be dirty again and still outrank every lower class. A class
            // only lets the page through once it has been all the way down from
            // the top - otherwise the alarm bar parking the walk on the bottom
            // row lets the table paint while the legend row above it waits.
            if (start != 0) return false;
        }
        return true;
    }

    void AnsiTerminal::_park_cursor() {
        // The wish, less whatever is holding it off (a live region).
        const bool want = _want_cursor_visible && !_cursor_inhibited;
        if (want != _hw_cursor_visible && Ansi::set_cursor_visible(_serial, want))
            _hw_cursor_visible = want;
        // A visible cursor belongs at the edit point of whoever asked for it (only
        // the shell editor does) - that is where its last move_to left the draw
        // cursor. With no such owner, idle it in the bottom-right corner so it sits
        // out of the content instead of wherever the last run happened to end.
        if (_hw_cursor_visible) _go(_live_cursor_pos);
        else                    _go({(uint16_t)(_rows - 1), (uint16_t)(_cols - 1)});
    }

}}

#endif // BOTANIK_FEATURE_CONSOLE
