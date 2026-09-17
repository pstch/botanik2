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
        _back_row_moved(row);
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

    uint16_t AnsiTerminal::_row_cost(const uint8_t* fr, uint16_t fr_tail, const uint8_t* bk,
                                     uint16_t bk_tail, uint16_t* spent) const {
        // Each differing cell is a byte, and each gap between them its width up
        // to HOP_BYTES; the stale blank tail is spaces or one EL.
        const uint16_t e = fr_tail;
        // Past both blank tails every cell matches; only `spent` wants them walked.
        const uint16_t stop = spent ? _cols : std::max<uint16_t>(e, bk_tail);
        uint16_t cells = 0, stale = 0, end = 0;
        bool reached = false;
        for (uint16_t c = 0; c < stop;) {
            // A word of content at a time: `m` has the high bit of each byte set
            // where the two words differ, so only those cells are visited.
            if (spent == nullptr && (uint16_t)(c + 4) <= e) {
                uint32_t a, b;
                memcpy(&a, fr + c, 4);
                memcpy(&b, bk + c, 4);
                const uint32_t x = a ^ b;
                uint32_t m = (((x & 0x7F7F7F7Fu) + 0x7F7F7F7Fu) | x) & 0x80808080u;
                if (m == 0x80808080u) {
                    if (reached) cells = (uint16_t)(cells + std::min<uint16_t>((uint16_t)(c - end), (uint16_t)HOP_BYTES));
                    cells = (uint16_t)(cells + 4);
                    reached = true;
                    end = (uint16_t)(c + 4);
                } else {
                    while (m != 0) {
                        const uint16_t d = (uint16_t)(c + (__builtin_ctz(m) >> 3));
                        if (reached) cells = (uint16_t)(cells + std::min<uint16_t>((uint16_t)(d - end), (uint16_t)HOP_BYTES));
                        cells = (uint16_t)(cells + 1);
                        reached = true;
                        end = (uint16_t)(d + 1);
                        m &= m - 1u;
                    }
                }
                c = (uint16_t)(c + 4);
                continue;
            }
            if (spent) spent[c] = cells;
            if (fr[c] != bk[c]) {
                if (c >= e) {
                    stale++;
                } else {
                    if (reached) cells = (uint16_t)(cells + std::min<uint16_t>((uint16_t)(c - end), (uint16_t)HOP_BYTES));
                    cells = (uint16_t)(cells + 1);
                    reached = true;
                    end = (uint16_t)(c + 1);
                }
            }
            c++;
        }
        if (spent) spent[_cols] = cells;
        if (!reached && stale == 0) return 0;
        return (uint16_t)(ROW_ENTRY_BYTES + cells + (stale != 0 ? _el_cost(stale) : 0));
    }

    namespace {
        constexpr uint16_t GRID_ROWS_MAX = Config::TERMINAL_MAX_ROWS;
        struct Band { int32_t gain; uint16_t top, bottom; };

        /** The best band an offset of @p n rows can slide, first in (top, bottom)
         *  order among equals, as the nested walk over every band would find it.
         *  Its gain is f(bottom) - g(top), so each bottom needs only the least g
         *  of the tops that reach it. @p moved null prices every kept row at 0. */
        Band best_band(const int32_t* stay, const int32_t* blank, const int32_t* moved,
                       uint16_t rows, uint16_t n, bool up, bool clear, int32_t slide_bytes) {
            static const int32_t NONE[GRID_ROWS_MAX + 1] = { 0 };
            if (moved == nullptr) moved = NONE;
            Band best = { INT32_MIN, 0, 0 };
            const uint16_t reach = clear ? 0 : n;
            int32_t  gmin = 0;
            uint16_t tmin = 0;
            for (uint16_t b = reach; b < rows; b++) {
                const uint16_t t = (uint16_t)(b - reach);
                int32_t g, f;
                const int32_t pair = b + 1 == rows ? slide_bytes : 2 * slide_bytes;
                if (clear) {
                    g = stay[t] - blank[t];
                    f = stay[b + 1] - blank[b + 1] - pair;
                } else if (up) {
                    g = stay[t] - moved[t];
                    f = stay[b + 1] - blank[b + 1] + blank[b - n + 1] - pair - moved[b - n + 1];
                } else {
                    g = stay[t] + blank[t + n] - blank[t] - moved[t + n];
                    f = stay[b + 1] - pair - moved[b + 1];
                }
                if (b == reach || g < gmin) { gmin = g; tmin = t; }
                const int32_t gain = f - gmin;
                if (gain > best.gain || (gain == best.gain && tmin < best.top)) best = { gain, tmin, b };
            }
            return best;
        }
    }

    bool AnsiTerminal::_find_slide(Slide& out) {
        if (_none_known && _none_insert == _hw_insert) return false;
        if (_search_slide(out)) return true;
        _none_known  = true;
        _none_insert = _hw_insert;
        return false;
    }

    uint16_t AnsiTerminal::_row_tail(uint8_t grid, uint16_t row) {
        const uint32_t bit = 1u << row;
        if ((_tail_known[grid] & bit) == 0) {
            _tail[grid][row] = (uint8_t)_blank_tail(grid == 0 ? &_front[_idx(row, 0)] : &_back[_idx(row, 0)]);
            _tail_known[grid] |= bit;
        }
        return _tail[grid][row];
    }

    uint16_t AnsiTerminal::_row_stay(uint16_t row) {
        const uint32_t bit = 1u << row;
        if ((_stay_known & bit) != 0 && ((_stay_insert & bit) != 0) == _hw_insert)
            return _stay_price[row];
        _stay_price[row] = _row_price(row, &_stay_shift[row]);
        _stay_known |= bit;
        if (_hw_insert) _stay_insert |= bit; else _stay_insert &= ~bit;
        return _stay_price[row];
    }

    void AnsiTerminal::_front_row_moved(uint16_t row) {
        const uint32_t keep = ~(1u << row);
        _unsettled   |= 1u << row;
        _tail_known[0] &= keep;
        _stay_known  &= keep;
        _blank_known &= keep;
        for (uint8_t k = 0; k < FIXED_OFFSETS; k++) _moved_known[k] &= keep;
        _none_known = false;
    }

    void AnsiTerminal::_back_row_moved(uint16_t row) {
        _unsettled  |= 1u << row;
        _tail_known[1] &= ~(1u << row);
        _stay_known &= ~(1u << row);
        // The row an offset prices against BACK row @p row: src = r + n up, r - n down.
        for (uint8_t k = 0; k < FIXED_OFFSETS; k++) {
            const int32_t r = (int32_t)row + _fixed_offset(k);
            if (r >= 0 && r < GRID_ROWS) _moved_known[k] &= ~(1u << r);
        }
        _none_known = false;
    }

    void AnsiTerminal::_all_rows_moved() {
        _unsettled  = 0xFFFFFFFFu;
        _tail_known[0] = _tail_known[1] = 0;
        _stay_known = _blank_known = 0;
        for (uint8_t k = 0; k < FIXED_OFFSETS; k++) _moved_known[k] = 0;
        _none_known = false;
    }

    bool AnsiTerminal::_row_settled(uint16_t row) {
        const uint32_t bit = 1u << row;
        if ((_unsettled & bit) == 0) return true;
        if (memcmp(&_front[_idx(row, 0)], &_back[_idx(row, 0)], _cols) != 0) return false;
        _unsettled &= ~bit;
        return true;
    }

    bool AnsiTerminal::_search_slide(Slide& out) {
        const uint16_t rows = _rows;
        uint16_t r = 0;
        while (r < rows && _row_settled(r)) r++;
        if (r == rows) return false;

        // What each FRONT row costs against the BACK row it would face: its own
        // (`stay`, 0 when they match, and a sideways slide when that is how it
        // would go out), a blank one, and below, the one an offset brings.
        // Prefix sums, so any band is priced in O(1). Every price is kept per row
        // until a row it reads moves.
        int32_t stay[GRID_ROWS + 1], blank[GRID_ROWS + 1], moved[GRID_ROWS + 1];
        stay[0] = blank[0] = moved[0] = 0;
        for (r = 0; r < rows; r++) stay[r + 1] = stay[r] + _row_stay(r);
        // A pair costs at least SLIDE_BYTES and what it keeps or opens is never
        // free, so no band gains more than the whole screen's stay less that.
        if (stay[rows] <= SLIDE_BYTES) return false;

        uint8_t blank_row[GRID_COLS];
        memset(blank_row, CELL_BLANK, _cols);
        for (r = 0; r < rows; r++) {
            if ((_blank_known & (1u << r)) == 0) {
                _blank_price[r] = _row_cost(&_front[_idx(r, 0)], _row_tail(0, r), blank_row, 0);
                _blank_known |= 1u << r;
            }
            blank[r + 1] = blank[r] + _blank_price[r];
        }

        // Offsets worth trying: the short ones a growing editor or a stepping
        // table makes, whose rows may also have changed, and any a changed row
        // shows exactly in some other BACK row. Blank rows seed nothing: every
        // blank row matches them.
        int16_t offsets[MAX_OFFSETS];
        for (uint8_t k = 0; k < FIXED_OFFSETS; k++) offsets[k] = _fixed_offset(k);
        uint8_t n_offsets = FIXED_OFFSETS;
        for (r = 0; r < rows && n_offsets < MAX_OFFSETS; r++) {
            const uint8_t* fr = &_front[_idx(r, 0)];
            if (stay[r + 1] == stay[r] || memcmp(fr, blank_row, _cols) == 0) continue;
            for (uint16_t src = 0; src < rows && n_offsets < MAX_OFFSETS; src++) {
                const uint8_t* bk = &_back[_idx(src, 0)];
                if (src == r || fr[0] != bk[0] || memcmp(fr, bk, _cols) != 0) continue;
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
        // An offset that cannot beat the best even keeping every row for nothing
        // is not priced at all.
        int32_t best = 0;
        for (uint8_t k = 0; k <= n_offsets; k++) {
            const bool     clear = k == n_offsets;
            const bool     up    = clear || offsets[k] < 0;
            const uint16_t n     = clear ? rows : (uint16_t)(up ? -offsets[k] : offsets[k]);
            if (!clear) {
                const bool priced = k < FIXED_OFFSETS && (~_moved_known[k] & _active_rows()) == 0;
                if (!priced && best_band(stay, blank, nullptr, rows, n, up, false, SLIDE_BYTES).gain <= best) continue;
                for (r = 0; r < rows; r++) {
                    const int32_t src = up ? (int32_t)r + n : (int32_t)r - n;
                    uint16_t cost = 0;
                    if (src >= 0 && src < rows) {
                        if (k < FIXED_OFFSETS && (_moved_known[k] & (1u << r)) != 0) {
                            cost = _moved_price[k][r];
                        } else {
                            cost = _row_cost(&_front[_idx(r, 0)], _row_tail(0, r),
                                             &_back[_idx((uint16_t)src, 0)], _row_tail(1, (uint16_t)src));
                            if (k < FIXED_OFFSETS) { _moved_price[k][r] = cost; _moved_known[k] |= 1u << r; }
                        }
                    }
                    moved[r + 1] = moved[r] + cost;
                }
            }
            const Band b = best_band(stay, blank, clear ? nullptr : moved, rows, n, up, clear, SLIDE_BYTES);
            if (b.gain > best) {
                best = b.gain;
                out  = { b.top, b.bottom, clear ? (uint16_t)(b.bottom - b.top + 1) : n, up };
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
        for (uint16_t r = s.top; r <= s.bottom; r++) _back_row_moved(r);
    }

    uint16_t AnsiTerminal::_row_price(uint16_t row, Shift* out) {
        const uint8_t* fr = &_front[_idx(row, 0)];
        const uint8_t* bk = &_back [_idx(row, 0)];
        if (out) out->n = 0;
        uint16_t first = 0;
        while (first < _cols && fr[first] == bk[first]) first++;
        if (first >= _cols) return 0;

        // The run loop's price for the row, and in `spent[c]` for what precedes
        // column c, which a slide starting there still has to write.
        uint16_t spent[GRID_COLS + 1];
        const uint16_t fr_tail = _row_tail(0, row), bk_tail = _row_tail(1, row);
        const int32_t plain = (int32_t)_row_cost(fr, fr_tail, bk, bk_tail, spent) - ROW_ENTRY_BYTES;

        // For each width, the longest suffix that is BACK slid by it, and the
        // column the slide starts at. A right slide writes the n cells it opens
        // inside SM4/RM4, free when an earlier row of the frame left insert mode
        // on; a left slide is one DCH, then whatever FRONT wants in the n cells
        // it blanks at the edge. The scan from the right stops at
        // the first mismatch, which for a width that is not the slide is usually
        // the first cell. A slide carrying only blanks moves nothing a rewrite
        // would pay for.
        const int32_t irm = _hw_insert ? 0 : 8;
        int32_t gain = 0;
        // Built on the first width tried. Where both scans sit inside a run of
        // one glyph in each row, every cell pair left of them matches as far as
        // the shorter run reaches, so a scan steps over it at once.
        uint8_t filled[GRID_COLS + 1];     // non-blank FRONT cells in [c, _cols)
        uint8_t fr_run[GRID_COLS], bk_run[GRID_COLS];   // cells ending at c with c's glyph
        for (uint16_t n = 1; n < _cols; n++) {
            if (plain - (int32_t)n - irm <= gain) break;   // no wider slide can beat it
            if (n == 1) {
                filled[_cols] = 0;
                for (uint16_t c = _cols; c-- > 0;) filled[c] = (uint8_t)(filled[c + 1] + (fr[c] != CELL_BLANK));
                fr_run[0] = bk_run[0] = 1;
                for (uint16_t c = 1; c < _cols; c++) {
                    fr_run[c] = (uint8_t)(fr[c] == fr[c - 1] ? fr_run[c - 1] + 1 : 1);
                    bk_run[c] = (uint8_t)(bk[c] == bk[c - 1] ? bk_run[c - 1] + 1 : 1);
                }
            }
            // Where both rows are blank the scans always match and move nothing,
            // so each starts at the nearer of the two blank tails.
            bool moves = false;
            uint16_t c = std::min<uint16_t>((uint16_t)(_cols - n),
                std::max<uint16_t>(first, std::max<uint16_t>(bk_tail, fr_tail > n ? (uint16_t)(fr_tail - n) : 0)));
            while (c > first && fr[c - 1 + n] == bk[c - 1]) {
                moves |= bk[c - 1] != CELL_BLANK;
                c = (uint16_t)(c - std::min<uint16_t>(std::min<uint16_t>(fr_run[c - 1 + n], bk_run[c - 1]),
                                                      (uint16_t)(c - first)));
            }
            int32_t g = plain - ((int32_t)spent[c] + (int32_t)n + irm);
            if (moves && g > gain) { gain = g; if (out) *out = { c, n, true }; }

            moves = false;
            c = std::min<uint16_t>((uint16_t)(_cols - n),
                std::max<uint16_t>(first, std::max<uint16_t>(fr_tail, bk_tail > n ? (uint16_t)(bk_tail - n) : 0)));
            while (c > first && fr[c - 1] == bk[c - 1 + n]) {
                moves |= fr[c - 1] != CELL_BLANK;
                c = (uint16_t)(c - std::min<uint16_t>(std::min<uint16_t>(fr_run[c - 1], bk_run[c - 1 + n]),
                                                      (uint16_t)(c - first)));
            }
            const int32_t tail = filled[_cols - n];
            g = plain - ((int32_t)spent[c] + 6 + tail + (tail != 0 ? 8 : 0));
            if (moves && g > gain) { gain = g; if (out) *out = { c, n, false }; }
        }
        return (uint16_t)(plain + ROW_ENTRY_BYTES - gain);
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
        if (_row_settled(row)) return true;

        // A sideways slide is one escape, after the cells that changed before
        // it. Both go out this tick or neither does: a slide the budget cannot
        // hold is owed the next tick, not the whole-row rewrite, which would
        // escape the very budget the slide was refused for.
        (void)_row_stay(row);
        const Shift sh = _stay_shift[row];
        if (sh.n != 0) {
            const uint16_t escape = sh.right ? (uint16_t)((_hw_insert ? 0 : 8) + sh.n + 5) : (uint16_t)(6 + 5);
            if (!_fits((uint16_t)(_run_cost(row, 0, sh.at) + escape), budget)) return false;
            if (!_emit_runs(row, 0, sh.at, budget) || !_go({row, sh.at})) return false;
            const uint16_t before = tx_used();
            uint8_t* const bk = &_back[_idx(row, 0)];
            if (sh.right) {
                // Insert mode: the glyphs written push the rest of the row right.
                // Mirror that in BACK first, so the run stamps the opened cells.
                if (!_set_hw_insert(true)) return false;
                memmove(&bk[sh.at + sh.n], &bk[sh.at], (size_t)(_cols - sh.at - sh.n));
                _back_row_moved(row);
                _put_run(row, sh.at, (uint16_t)(sh.at + sh.n), /*insert=*/true);
            } else {
                // DCH fills the cells it frees using the current attributes on
                // terminals with background-colour erase, so the pen goes plain.
                _set_hw_inverted(false);
                if (!Ansi::delete_chars(_serial, sh.n)) return false;
                memmove(&bk[sh.at], &bk[sh.at + sh.n], (size_t)(_cols - sh.at - sh.n));
                memset(&bk[_cols - sh.n], CELL_BLANK, sh.n);
                _back_row_moved(row);
                _hw_at_eol = false;
            }
            budget = _spend(budget, (uint16_t)(tx_used() - before));
            if (budget == 0) return false;
        }

        // Past a few stale cells in FRONT's blank tail, one EL erases them for
        // three bytes where the runs would write a space each; it goes out last,
        // so the cursor is already near it.
        const uint16_t e = _row_tail(0, row);
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
        _back_row_moved(row);
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
            // Only a row that may differ has anything to reconcile.
            uint16_t start = _resume[prio];
            uint32_t pending = _prio_rows[prio] & _unsettled & _active_rows() & (0xFFFFFFFFu << start);
            while (pending != 0) {
                const uint16_t row = (uint16_t)__builtin_ctz(pending);
                pending &= pending - 1u;
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
