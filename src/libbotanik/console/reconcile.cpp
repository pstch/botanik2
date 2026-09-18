/**
 * @file reconcile.cpp
 * @brief AnsiTerminal::present() - FRONT reconciled to the wire: the layout
 *        changes the renderers announced, then each row's changed cells,
 *        paced, then the cursor. See terminal.h.
 */
#include "libbotanik/console/terminal.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "libbotanik/console/ansi.h"
#include "libbotanik/utils/assert.h"


namespace Botanik { namespace Terminal {

    // Wire state

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
            const uint16_t down = (uint16_t)(target.row - _hw_pos.row);
            // A line feed a row while that beats CUD's four bytes, off the last
            // row, where it would scroll, and only in column 0: a terminal in
            // new-line mode, or a tty with ONLCR, returns the carriage with it.
            if (target.row < _hw_pos.row)                    ok = Ansi::move_up(_serial, (uint16_t)(_hw_pos.row - target.row));
            else if (target.col == 0 && down < LF_MAX_ROWS && target.row < (uint16_t)(_rows - 1))
                                                             ok = Ansi::feed_lines(_serial, down);
            else                                             ok = Ansi::move_down(_serial, down);
        } else if (target.col == 0 && target.row > _hw_pos.row &&
                   (target.row - _hw_pos.row) < LF_MAX_ROWS && target.row < (uint16_t)(_rows - 1)) {
            ok = Ansi::down_to_row_start(_serial, (uint16_t)(target.row - _hw_pos.row));
        } else {
            ok = Ansi::goto_absolute(_serial, target);
        }
        if (ok) { _hw_pos = target; _hw_at_eol = false; }
        return ok;
    }

    void AnsiTerminal::_set_hw_inverted(bool inv) {
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
                _back[_idx(row, m)] = _front[_idx(row, m)];
                m++;
            }
            _serial.write(buf, n);
            k = m;
        }
        // The real cursor advanced to column b, or parked on the edge.
        if (b >= _cols) { _hw_pos.col = (uint16_t)(_cols - 1); _hw_at_eol = true; }
        else            { _hw_pos.col = b; _hw_at_eol = false; }
        _hw_pos.row = row;
        return (uint16_t)(tx_used() - before);
    }

    uint16_t AnsiTerminal::_run_cost(uint16_t row, uint16_t a, uint16_t b) const {
        // An absolute move, an SGR per attribute change, a byte per glyph.
        uint16_t cost = 8;
        bool inv = _hw_inverted;
        for (uint16_t c = a; c < b; c++) {
            bool ci = (_front[_idx(row, c)] & CELL_INV) != 0;
            if (ci != inv) { cost += 5; inv = ci; }
            cost += 1;
        }
        return cost;
    }

    bool AnsiTerminal::_fits(uint16_t cost, uint16_t budget) const {
        if (_serial.availableForWrite() < cost) return false;     // ring full
        // A write that needs the whole budget or more can never fit inside one,
        // so it goes out as soon as the ring is shallow (and ends the tick).
        // >=, not >: tx_used() never reports an empty ring as 0, so budget is
        // always at least a byte under _tx_cold_depth, and a write costing
        // exactly _tx_cold_depth would otherwise never go.
        return cost >= _tx_cold_depth ? budget != 0 : cost <= budget;
    }

    void AnsiTerminal::_check_dirty() const {
#ifdef DEBUG
        for (uint16_t r = 0; r < _rows; r++)
            if ((_dirty & (1u << r)) == 0)
                BOTANIK_ASSERT(memcmp(&_front[_idx(r, 0)], &_back[_idx(r, 0)], _cols) == 0);
#endif
    }

    void AnsiTerminal::_restart_walk() {
        for (uint8_t p = 0; p < PRIO_COUNT; p++) _resume[p] = 0;
    }

    // Layout changes

    void AnsiTerminal::change(const LayoutChange& c) {
        if (c.n == 0 || c.top > c.bottom || c.bottom >= _rows) return;
        if (c.kind == LayoutChange::SCROLL && std::abs(c.n) > c.bottom - c.top) return;   // nothing of it stays
        if (c.kind == LayoutChange::RIPPLE && (c.col >= _cols || c.lead >= _cols)) return;
        if (c.kind == LayoutChange::SCROLL ? _merge_scroll(c) : _merge_ripple(c)) return;
        if (_queued == CHANGE_QUEUE) { _drop_changes(); return; }   // a beat that busy is repainted
        _queue[_queued++] = Queued{ c, _rows_of(c) };
    }

    bool AnsiTerminal::_merge_ripple(const LayoutChange& c) {
        // Two ripples compose where the second picks up the cells the first
        // left: typing on, backspacing over, or deleting at one spot.
        if (_queued == 0) return false;
        Queued& q = _queue[_queued - 1];
        if (q.c.kind != LayoutChange::RIPPLE || q.left != _rows_of(q.c)) return false;
        if (q.c.top != c.top || q.c.bottom != c.bottom || q.c.lead != c.lead || (q.c.n < 0) != (c.n < 0)) return false;
        const int32_t sum = (int32_t)q.c.n + c.n;
        if (!(c.col == q.c.col + q.c.n || (c.n < 0 && c.col == q.c.col)) || sum <= -127 || sum >= 127) return false;
        q.c.n    = (int16_t)sum;
        q.c.col  = std::min(q.c.col, c.col);
        q.c.tail = std::min(q.c.tail, c.tail);
        return true;
    }

    bool AnsiTerminal::_merge_scroll(const LayoutChange& c) {
        // Two views scrolling the same way over neighbouring bands are one
        // move: the transcript sliding under an editor that grew is the common
        // one. A ripple queued between them stands only if the band does not
        // reach its rows, since it is in the coordinates of after.
        const uint32_t rows = _rows_of(c);
        for (uint8_t i = _queued; i-- > 0;) {
            Queued& q = _queue[i];
            if (q.c.kind == LayoutChange::RIPPLE) { if ((_rows_of(q.c) & rows) != 0) return false; continue; }
            if (q.c.bottom + 1 < c.top || q.c.top > c.bottom + 1) return false;
            // Another band over the same rows, moving differently: the later
            // word stands and the walk writes what the first one described.
            if (q.c.n != c.n) { q.c = c; return true; }
            q.c.top    = std::min(q.c.top, c.top);
            q.c.bottom = std::max(q.c.bottom, c.bottom);
            return true;
        }
        return false;
    }

    uint32_t AnsiTerminal::_owed() const {
        uint32_t rows = 0;
        for (uint8_t i = 0; i < _queued; i++)
            if (_queue[i].c.kind == LayoutChange::SCROLL) rows |= _rows_of(_queue[i].c);
        return rows;
    }

    void AnsiTerminal::_apply_changes() {
        // Scrolls go now, oldest first, and a ripple waits for the walk to
        // reach its rows. A ripple announced before a scroll over its rows is
        // in coordinates the scroll moves, and is forgotten.
        bool moved = false;
        uint8_t kept = 0;
        for (uint8_t i = 0; i < _queued; i++) {
            Queued& q = _queue[i];
            if (q.c.kind == LayoutChange::RIPPLE) {
                q.left &= _dirty;                   // a clean row is reconciled already
                if (q.left != 0) _queue[kept++] = q;
                continue;
            }
            const uint32_t rows = _rows_of(q.c);
            if ((_dirty & rows) == 0) continue;
            if (!_slide(q.c.top, q.c.bottom, q.c.n)) {
                // The ring is full: it and everything after it wait.
                memmove(&_queue[kept], &_queue[i], (size_t)(_queued - i) * sizeof _queue[0]);
                kept = (uint8_t)(kept + _queued - i);
                break;
            }
            moved = true;
            for (uint8_t k = 0; k < kept; k++)
                if ((_rows_of(_queue[k].c) & rows) != 0) _queue[k].left = 0;
        }
        _queued = kept;
        if (_queued == 0 || moved) _queue_age = 0;
    }

    bool AnsiTerminal::_shift(uint16_t row, uint16_t at, int16_t n, uint16_t& budget) {
        const uint16_t mag = (uint16_t)std::abs(n);
        uint8_t* const bk  = &_back[_idx(row, 0)];
        if (at + mag >= _cols) return true;             // nothing of the row stays
        _touch(row);
        _queue_age = 0;
        const uint16_t before = tx_used();
        if (n > 0) {
            // One SM4, the glyphs it opens, the move onto them. BACK opens first,
            // so the run stamps the cells the wire is about to open.
            if (!_fits((uint16_t)(8 + 4 + 5 + mag), budget)) return false;
            if (!_go({row, at}) || !_set_hw_insert(true)) return false;
            memmove(&bk[at + mag], &bk[at], (size_t)(_cols - at - mag));
            _put_run(row, at, (uint16_t)(at + mag), /*insert=*/true);
            budget = _spend(budget, (uint16_t)(tx_used() - before));
            return true;
        }
        if (!_fits((uint16_t)(8 + 5 + 6 + 8 + mag), budget)) return false;
        if (!_go({row, at})) return false;
        // DCH fills the cells it frees with the current attributes on terminals
        // with background-colour erase, so the pen goes plain first.
        _set_hw_inverted(false);
        if (!Ansi::delete_chars(_serial, mag)) return false;
        memmove(&bk[at], &bk[at + mag], (size_t)(_cols - at - mag));
        memset(&bk[_cols - mag], CELL_BLANK, mag);
        _hw_at_eol = false;
        // The cells pulled in from the next row land in what the DCH freed;
        // written now they are a step away, left to the walk an address.
        const uint16_t e = _blank_tail(&_front[_idx(row, 0)]);
        if (e > _cols - mag) _put_run(row, (uint16_t)(_cols - mag), e);
        budget = _spend(budget, (uint16_t)(tx_used() - before));
        return true;
    }

    bool AnsiTerminal::_erase_from(uint16_t row, uint16_t col) {
        // EL fills with the current attributes on terminals with
        // background-colour erase, so the pen goes plain first.
        if (!_go({row, col})) return false;
        _set_hw_inverted(false);
        if (!Ansi::erase_to_eol(_serial)) return false;
        memset(&_back[_idx(row, col)], CELL_BLANK, (size_t)(_cols - col));
        return true;
    }

    bool AnsiTerminal::_ripple_row(uint16_t row, uint16_t& budget) {
        const uint32_t bit = 1u << row;
        for (uint8_t i = 0; i < _queued; i++) {
            Queued& q = _queue[i];
            if (q.c.kind != LayoutChange::RIPPLE || (q.left & bit) == 0) continue;
            const LayoutChange& c = q.c;
            const uint16_t mag     = (uint16_t)std::abs(c.n);
            const uint16_t at      = (row == c.top) ? c.col : c.lead;
            const uint16_t carried = (row == c.bottom) ? c.tail
                                   : (_cols > at + mag ? (uint16_t)(_cols - at - mag) : 0);
            q.left &= ~bit;
            // The escape brackets what it carries; under a few cells the walk
            // writes them for less. Nor is there anything to carry on a row the
            // wire already shows right.
            if (carried < SHIFT_MIN_CARRY) continue;
            if (memcmp(&_front[_idx(row, 0)], &_back[_idx(row, 0)], _cols) == 0) continue;
            // The cells left of the shift are the walk's either way, and written
            // first they leave the cursor a step from the escape, not an address.
            if (!_emit_runs(row, 0, at, budget) || !_shift(row, at, c.n, budget)) { q.left |= bit; return false; }
        }
        return true;
    }

    bool AnsiTerminal::_slide(uint16_t top, uint16_t bottom, int16_t n) {
        // Room for the whole pair before the first escape goes: half a pair on
        // the wire with BACK unshifted is a screen nothing repairs.
        if (_serial.availableForWrite() < SLIDE_ROOM) return false;
        const bool     up      = n < 0;
        const uint16_t mag     = (uint16_t)std::abs(n);
        const bool     restore = bottom < (uint16_t)(_rows - 1);
        const uint16_t back    = (uint16_t)(bottom - mag + 1);
        // DL/IL fill the rows they free with the current attributes on terminals
        // with background-colour erase, and BACK blanks them plain.
        _set_hw_inverted(false);
        bool ok;
        if (up) {
            ok = _go({top, 0}) && Ansi::delete_rows(_serial, mag);
            if (ok && restore) ok = _go({back, 0}) && Ansi::insert_rows(_serial, mag);
        } else {
            ok = !restore || (_go({back, 0}) && Ansi::delete_rows(_serial, mag));
            if (ok) ok = _go({top, 0}) && Ansi::insert_rows(_serial, mag);
        }
        if (!ok) return false;
        const uint16_t kept  = (uint16_t)(bottom - top + 1 - mag);
        const uint16_t freed = up ? back : top;
        memmove(&_back[_idx(up ? top : (uint16_t)(top + mag), 0)],
                &_back[_idx(up ? (uint16_t)(top + mag) : top, 0)], (size_t)kept * _cols);
        memset(&_back[_idx(freed, 0)], CELL_BLANK, (size_t)mag * _cols);
        _dirty |= _mask(top, bottom);
        // The rows under the band moved, so the walk starts from the top again
        // rather than from wherever the last tick's budget ran out.
        _restart_walk();
        return true;
    }

    void AnsiTerminal::blank_rows(uint16_t top, uint16_t bottom) {
        clear_rows(top, bottom);
        if (top > bottom || bottom >= _rows) return;
        // The content changed as a whole: pending changes describe a screen
        // that is gone, and the walk starts over. One EL per row, the cursor
        // stepping down: nothing on screen moves and the bars are untouched,
        // which no ED or DL/IL can offer.
        _drop_changes();
        _restart_walk();
        const uint16_t n = (uint16_t)(bottom - top + 1);
        if (_serial.availableForWrite() < (uint16_t)(8 + n * 8)) return;   // the walk blanks it row by row
        for (uint16_t r = top; r <= bottom; r++)
            if (!_erase_from(r, 0)) return;
        _dirty &= ~_mask(top, bottom);         // blank against blank
    }

    // The walk

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
            // Leaving insert mode costs its escape too: a ripple on a row above
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
        if (!_ripple_row(row, budget)) return false;
        if (memcmp(&_front[_idx(row, 0)], &_back[_idx(row, 0)], _cols) == 0) { _dirty &= ~(1u << row); return true; }
        // Past a few stale cells in FRONT's blank tail, one EL erases them for
        // three bytes where the runs would write a space each; it goes out last,
        // so the cursor is already near it.
        const uint16_t e = _blank_tail(&_front[_idx(row, 0)]);
        uint16_t stale = 0;
        for (uint16_t c = e; c < _cols; c++)
            if (_back[_idx(row, c)] != CELL_BLANK) stale++;
        const bool erase = stale >= EL_MIN_CELLS;
        if (!_emit_runs(row, 0, erase ? e : _cols, budget)) return false;
        if (!erase) { _dirty &= ~(1u << row); return true; }
        if (!_fits((uint16_t)(8 + 3 + (_hw_inverted ? 5 : 0)), budget)) return false;
        const uint16_t before = tx_used();
        if (!_erase_from(row, e)) return false;
        budget = _spend(budget, (uint16_t)(tx_used() - before));
        _dirty &= ~(1u << row);
        return true;
    }

    bool AnsiTerminal::_emit_diff() {
        // Neither grid moved and nothing is owed: the wire already shows FRONT.
        // This is most beats.
        if (_dirty == 0 && _queued == 0) { _restart_walk(); return true; }
        // Beats without a change going out: a ripple the walk never reaches
        // or a scroll the ring never takes is dropped, and its rows written.
        if (_queued != 0 && ++_queue_age > CHANGE_MAX_BEATS) _drop_changes();
        uint16_t used   = tx_used();
        uint16_t budget = (used < _tx_cold_depth) ? (uint16_t)(_tx_cold_depth - used) : 0;
        const uint32_t owed = _owed();
        for (uint8_t prio = 0; prio < PRIO_COUNT; prio++) {
            // Within a class the walk only ever runs downward: it resumes where
            // the budget ran out, stops at the bottom, then starts the next pass
            // at the top. That keeps a tick's emissions in row order (content
            // reads top-down) while stopping budget-hungry upper rows from
            // winning the budget every tick.
            const uint16_t start = _resume[prio];
            for (uint16_t row = start; row < _rows; row++) {
                if (_row_prio[row] != prio) continue;
                // A row a change still owes stops the class where it stands, so
                // a page never arrives bottom first.
                if ((owed & (1u << row)) != 0) { _resume[prio] = (uint8_t)row; return false; }
                if ((_dirty & (1u << row)) == 0) continue;
                if (!_reconcile_row(row, budget)) { _resume[prio] = (uint8_t)row; return false; }
            }
            _resume[prio] = 0;
            // Reaching the bottom is not being drained: the rows above `start`
            // may be dirty again and still outrank every lower class. A class
            // only lets the next through once it has been all the way down from
            // the top - otherwise the alarm bar parking the walk on the bottom
            // row lets the table paint while the legend row above it waits.
            if (start != 0) return false;
        }
        return owed == 0;
    }

    void AnsiTerminal::_park_cursor() {
        const bool want = _want_cursor_visible && !_cursor_inhibited;
        if (want != _hw_cursor_visible && Ansi::set_cursor_visible(_serial, want))
            _hw_cursor_visible = want;
        // A visible cursor belongs at the edit point of whoever asked for it.
        // With no such owner, idle it in the bottom-right corner, out of the
        // content.
        if (_hw_cursor_visible) _go(_live_cursor_pos);
        else                    _go({(uint16_t)(_rows - 1), (uint16_t)(_cols - 1)});
    }

    void AnsiTerminal::present() {
        _check_dirty();
        // The scrolls go first: they move whole rows, and the walk reads the
        // rows they land on.
        if (_queued != 0) _apply_changes();
        // Park only once the frame is fully reconciled: mid-repaint the runs are
        // moving the cursor anyway.
        if (_emit_diff()) _park_cursor();
        // IRM survives cursor motion, so a frame's inserts on several rows share
        // one SM4; the wire leaves the frame in replace mode.
        (void)_set_hw_insert(false);
    }

}}

#endif // BOTANIK_FEATURE_CONSOLE
