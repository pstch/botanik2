/**
 * @file reconcile.cpp
 * @brief AnsiTerminal::present() - FRONT reconciled to the wire: the moves the
 *        renderers hinted, then each row's changed cells, paced, then the
 *        cursor. See terminal.h.
 */
#include "libbotanik/console/terminal.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "libbotanik/console/ansi.h"
#include <cstdio>
#include <cstdlib>


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
        // The band goes first: it moves whole rows, and the walk reads the rows
        // it lands on. A column move waits for the walk to reach its row.
        _apply_row_hint();
        // Park only once the frame is fully reconciled: mid-repaint the runs are
        // moving the cursor anyway, so re-homing it would burn a move per tick.
        if (_emit_diff()) _park_cursor();
        // IRM survives cursor motion, so a frame's insert-shifts on several rows
        // share one SM4; the wire leaves the frame in replace mode.
        (void)_set_hw_insert(false);
    }

    // Hints

    void AnsiTerminal::hint_rows(uint16_t top, uint16_t bottom, int16_t delta) {
        if (delta == 0 || top > bottom || bottom >= _rows) return;
        const uint16_t n = (uint16_t)(delta < 0 ? -delta : delta);
        if (n > (uint16_t)(bottom - top)) return;         // nothing of the band stays
        // Two views moving the same way over neighbouring bands are one move: the
        // transcript sliding under an editor that grew is the common one.
        if (_row_hint.delta == delta && _row_hint.bottom + 1 >= top && _row_hint.top <= bottom + 1) {
            const uint16_t ntop = std::min(_row_hint.top, top);
            const uint16_t nbot = std::max(_row_hint.bottom, bottom);
            if (n <= (uint16_t)(nbot - ntop)) { _row_hint.top = ntop; _row_hint.bottom = nbot; return; }
        }
        if (_row_hint.delta != 0 && _row_hint.top == top && _row_hint.bottom == bottom) {
            const int32_t sum = (int32_t)_row_hint.delta + delta;   // the band moved again
            const int32_t mag = sum < 0 ? -sum : sum;
            _row_hint.delta = (mag > (int32_t)(bottom - top)) ? 0 : (int16_t)sum;
            return;
        }
        // Another band, with one still owed: the old one goes and the walk writes
        // its rows out. Column moves stand: they are in the coordinates the band
        // leaves behind, and each is checked against BACK before it goes.
        _row_hint = { top, bottom, delta, 0 };
    }

    void AnsiTerminal::hint_cols(uint16_t row, uint16_t col, int16_t delta) {
        if (delta == 0 || row >= _rows || col >= _cols) return;
        ColHint& h = _col_hint[row];
        if (h.delta != 0) {
            // Two moves compose into one only where the second picks up the cells
            // the first left: typing on, or backspacing over, one spot.
            const int32_t back_col = (int32_t)col - h.delta;
            const int32_t sum      = (int32_t)h.delta + delta;
            if (back_col != h.col || sum > 127 || sum < -127) { h.delta = 0; return; }
            h.delta = (int8_t)sum;
            return;
        }
        if (delta > 127 || delta < -127) return;
        h = { (uint8_t)col, (int8_t)delta, 0 };
    }

    void AnsiTerminal::_drop_hints() {
        _row_hint.delta = 0;
        for (uint16_t r = 0; r < GRID_ROWS; r++) _col_hint[r].delta = 0;
    }

    uint32_t AnsiTerminal::_band_rows() const {
        uint32_t rows = 0;
        if (_row_hint.delta != 0)
            for (uint16_t r = _row_hint.top; r <= _row_hint.bottom && r < GRID_ROWS; r++) rows |= 1u << r;
        return rows;
    }

    uint8_t AnsiTerminal::_after_hints(uint16_t row, uint16_t col, bool cols) const {
        uint16_t src = row;
        if (_row_hint.delta != 0 && row >= _row_hint.top && row <= _row_hint.bottom) {
            const int32_t from = (int32_t)row - _row_hint.delta;
            if (from < (int32_t)_row_hint.top || from > (int32_t)_row_hint.bottom) return CELL_BLANK;
            src = (uint16_t)from;
        }
        const ColHint& h = _col_hint[row];
        if (!cols || h.delta == 0) return _back[_idx(src, col)];
        const uint16_t n = (uint16_t)(h.delta < 0 ? -h.delta : h.delta);
        if (h.delta > 0) {
            if (col < h.col)                      return _back[_idx(src, col)];
            if (col < (uint16_t)(h.col + n))      return _front[_idx(row, col)];   // the cells it opens
            return _back[_idx(src, (uint16_t)(col - n))];
        }
        if (col < (uint16_t)(h.col - n))          return _back[_idx(src, col)];
        if ((uint16_t)(col + n) < _cols)          return _back[_idx(src, (uint16_t)(col + n))];
        return CELL_BLANK;
    }

    int32_t AnsiTerminal::_tail_cost(uint16_t stale) {
        if (stale == 0)               return 0;
        if (stale >= EL_MIN_CELLS)    return EL_BYTES;
        return (int32_t)stale + 1 + ROW_ENTRY_BYTES;
    }

    int32_t AnsiTerminal::_saved(uint16_t row, bool cols) const {
        const uint8_t* fr = &_front[_idx(row, 0)];
        const uint8_t* bk = &_back [_idx(row, 0)];
        const uint16_t e  = _blank_tail(fr);
        bool run_now = false, run_then = false;
        int32_t saved = 0;
        // What the walk writes now, less what it still writes after: a run costs
        // its cells and the move onto them.
        for (uint16_t c = 0; c < e; c++) {
            if (fr[c] != bk[c])                         { saved += run_now  ? 1 : 1 + ROW_ENTRY_BYTES; run_now  = true; } else run_now  = false;
            if (fr[c] != _after_hints(row, c, cols))    { saved -= run_then ? 1 : 1 + ROW_ENTRY_BYTES; run_then = true; } else run_then = false;
        }
        uint16_t stale_now = 0, stale_then = 0;
        for (uint16_t c = e; c < _cols; c++) {
            if (bk[c] != CELL_BLANK)                        stale_now++;
            if (_after_hints(row, c, cols) != CELL_BLANK)   stale_then++;
        }
        return saved + _tail_cost(stale_now) - _tail_cost(stale_then);
    }

    int32_t AnsiTerminal::_band_price() const {
        int32_t saved = 0;
        for (uint16_t r = _row_hint.top; r <= _row_hint.bottom && r < _rows; r++) saved += _saved(r, false);
        // A band ending on the last row needs no second escape: there is nothing
        // under it to put back.
        const int32_t cost = (_row_hint.bottom + 1 == _rows) ? SLIDE_BYTES : 2 * SLIDE_BYTES;
        return cost - saved;
    }

    bool AnsiTerminal::_col_hint_pays(uint16_t row) const {
        const ColHint& h = _col_hint[row];
        const uint16_t n = (uint16_t)(h.delta < 0 ? -h.delta : h.delta);
        // The move onto the row, the escape, and for an insert the glyphs it opens.
        const int32_t cost  = h.delta > 0 ? (int32_t)(4 + 4 + n) : (int32_t)(4 + 5);
        const int32_t saved = _saved(row, true);
        return saved > cost;
    }

    void AnsiTerminal::_apply_row_hint() {
        if (_row_hint.delta == 0) return;
        // Dragging the rows under the band costs nothing where the walk was going
        // to write them anyway, and saves the escape that would put them back.
        const uint16_t asked = _row_hint.bottom;
        if (asked + 1 < _rows) {
            const int32_t here = _band_price();
            _row_hint.bottom = (uint16_t)(_rows - 1);
            if (_band_price() > here) _row_hint.bottom = asked;
        }
        const bool     up = _row_hint.delta < 0;
        const uint16_t n  = (uint16_t)(up ? -_row_hint.delta : _row_hint.delta);
        const Slide    s  = { _row_hint.top, _row_hint.bottom, n, up };
        // Room for the whole pair before the first escape goes: half a pair on
        // the wire with BACK unshifted is a screen nothing repairs.
        if (_band_price() >= 0)                                              _row_hint.delta = 0;
        else if (_serial.availableForWrite() >= SLIDE_ROOM && _emit_slide(s)) _row_hint.delta = 0;
        else if (++_row_hint.age > HINT_MAX_BEATS)                            _drop_hints();
    }

    bool AnsiTerminal::_take_col_hint(uint16_t row) {
        if (!_col_hint_pays(row))                  { _col_hint[row].delta = 0; return true; }
        if (_apply_col_hint(row))                  { _col_hint[row].delta = 0; return true; }
        if (++_col_hint[row].age > HINT_MAX_BEATS) { _col_hint[row].delta = 0; return true; }
        return false;
    }

    bool AnsiTerminal::_apply_col_hint(uint16_t row) {
        const ColHint& h  = _col_hint[row];
        const uint16_t n  = (uint16_t)(h.delta < 0 ? -h.delta : h.delta);
        uint8_t* const bk = &_back[_idx(row, 0)];
        if (h.delta > 0) {
            if ((uint16_t)(h.col + n) >= _cols) return true;    // the row is all new
            // One SM4, the glyphs it opens, and the move onto them. A move too
            // wide for any tick is dropped rather than left pending: waiting for
            // room that never comes holds the row off the wire.
            const uint16_t cost = (uint16_t)(8 + 8 + n + 5);
            if (cost > _cols)          return true;
            if (!_fits(cost, _cols))   return false;
            if (!_go({row, h.col}) || !_set_hw_insert(true)) return false;
            // Insert mode: the glyphs written push the rest of the row right.
            // Mirror that in BACK first, so the run stamps the opened cells.
            memmove(&bk[h.col + n], &bk[h.col], (size_t)(_cols - h.col - n));
            _put_run(row, h.col, (uint16_t)(h.col + n), /*insert=*/true);
            return true;
        }
        if (h.col < n) return true;                            // nothing of it stays
        const uint16_t at = (uint16_t)(h.col - n);
        if (!_fits((uint16_t)(8 + 6 + 5), _cols)) return false;
        if (!_go({row, at})) return false;
        // DCH fills the cells it frees using the current attributes on terminals
        // with background-colour erase, so the pen goes plain first.
        _set_hw_inverted(false);
        if (!Ansi::delete_chars(_serial, n)) return false;
        memmove(&bk[at], &bk[h.col], (size_t)(_cols - h.col));
        memset(&bk[_cols - n], CELL_BLANK, n);
        _hw_at_eol = false;
        return true;
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
        // The rows under the band moved, so the walk starts from the top again
        // rather than from wherever the last tick's budget ran out.
        if (ok) { _shift_back(s); _restart_walk(); }
        return ok;
    }

    void AnsiTerminal::_shift_back(const Slide& s) {
        const uint16_t kept  = (uint16_t)(s.bottom - s.top + 1 - s.n);
        const uint16_t freed = s.up ? (uint16_t)(s.bottom - s.n + 1) : s.top;
        memmove(&_back[_idx(s.up ? s.top : (uint16_t)(s.top + s.n), 0)],
                &_back[_idx(s.up ? (uint16_t)(s.top + s.n) : s.top, 0)], (size_t)kept * _cols);
        memset(&_back[_idx(freed, 0)], CELL_BLANK, (size_t)s.n * _cols);
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
            // Leaving insert mode costs its escape too, and a hint on a row above
            // may have left the wire in it.
            const uint16_t cost = (uint16_t)(_run_cost(row, col, b) + (_hw_insert ? 4 : 0));
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
        const ColHint& h = _col_hint[row];
        if (h.delta != 0) {
            // The cells left of the move are the walk's either way, and writing
            // them first leaves the cursor a step from the escape rather than an
            // address away.
            const uint16_t n  = (uint16_t)(h.delta < 0 ? -h.delta : h.delta);
            const uint16_t at = h.delta > 0 ? h.col : (uint16_t)(h.col > n ? h.col - n : 0);
            if (!_emit_runs(row, 0, at, budget)) return false;
            if (!_take_col_hint(row))            return false;
        }
        // Nothing pending: skip in one memcmp rather than a per-cell walk.
        if (memcmp(&_front[_idx(row, 0)], &_back[_idx(row, 0)], _cols) == 0) return true;

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
        // A row a hint still owes is left alone: writing it now would pay for the
        // cells that move is about to carry.
        const uint32_t owed = _band_rows();
        for (uint8_t prio = 0; prio < PRIO_COUNT; prio++) {
            // Within a class the walk only ever runs downward: it resumes where
            // the budget ran out, stops at the bottom, then starts the next pass
            // at the top. That keeps a tick's emissions in row order (content
            // reads top-down) while stopping budget-hungry upper rows from
            // winning the budget every tick. See @ref dev_screen.
            uint16_t start = _resume[prio];
            for (uint16_t row = start; row < _rows; row++) {
                if (_row_prio[row] != prio) continue;
                // A row a hint still owes stops the class where it stands: what
                // goes out in a tick stays in row order, so a page never arrives
                // bottom first.
                if ((owed & (1u << row)) != 0) { _resume[prio] = (uint8_t)row; return false; }
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
        // The rows a hint owes are not reconciled until it goes out.
        return owed == 0;
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
