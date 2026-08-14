// REVIEW_35: TODO
/**
 * @file terminal.cpp
 * @brief AnsiTerminal implementation - the VT device over a screen back-buffer.
 *
 * Renderers paint immediate-mode into the FRONT cell grid through the usual ops;
 * present() diffs FRONT against BACK (the wire) and emits the minimal, paced ANSI
 * to reconcile them. See terminal.h.
 */
#include "libbotanik/console/terminal.h"    // AnsiTerminal

#include <algorithm>                        // std::min
#include <cstdint>                          // uint8_t/uint16_t/uint32_t
#include <cstring>                          // memcmp

#include "libbotanik/config/terminal.h"   // Config::SCREEN_* / EDITOR_* / SHELL_* / PAGE_*

#include "libbotanik/console/ansi.h"        // Terminal::Ansi
#include "libbotanik/hal/platform.h"        // HAL::Env::u8
#include "libbotanik/utils/serial.h"        // Utils::BufferedSerial::tx_capacity


namespace Botanik { namespace Terminal {

    // Lifecycle

    AnsiTerminal::AnsiTerminal(Utils::BufferedSerial& serial, uint16_t cols,
                               uint16_t rows, uint32_t baud,
                               Ansi::TerminalType type) :
        _serial(serial),
        _cols(cols ? (cols < GRID_COLS ? cols : GRID_COLS) : 1),
        _rows(rows ? (rows < GRID_ROWS ? rows : GRID_ROWS) : 1),
        _type(type)
    {
        _tx_cold_depth = depth_for_baud(baud, Config::TX_COLD_TARGET_MS);
        for (uint16_t i = 0; i < GRID_CELLS; i++) { _front[i] = CELL_BLANK; _back[i] = CELL_BLANK; }
        for (uint16_t r = 0; r < GRID_ROWS; r++) _row_prio[r] = PRIO_STRUCT;
    }

    bool AnsiTerminal::reconfigure(Ansi::TerminalType type, uint16_t cols, uint16_t rows) {
        // Same clamp as the constructor's, so what the two accept cannot drift.
        const uint16_t c = cols ? (cols < GRID_COLS ? cols : GRID_COLS) : 1;
        const uint16_t r = rows ? (rows < GRID_ROWS ? rows : GRID_ROWS) : 1;
        if (c == _cols && r == _rows && type == _type) return false;
        const bool retyped = (type != _type);
        _type = type;
        _cols = c;
        _rows = r;
        // Priorities are per row and the page declares its own in focus(), which
        // the caller's repaint runs; reset them so rows the old layout claimed do
        // not keep a class the new one never gave them.
        for (uint16_t i = 0; i < GRID_ROWS; i++) _row_prio[i] = PRIO_STRUCT;
        // The handshake is a property of the make, so a swapped make needs it
        // again - and it must precede the clear below, exactly as at bring-up.
        if (retyped) setup_terminal();
        // _idx() is _cols-relative, so every cell in both grids has just changed
        // meaning. establish_screen() blanks the pair and puts the wire in the
        // state that matches, which is the only honest thing to do with contents
        // that now address the wrong columns.
        establish_screen();
        return true;
    }

    void AnsiTerminal::set_row_priority(uint16_t top, uint16_t bottom, RowPriority prio) {
        for (uint16_t r = top; r <= bottom && r < GRID_ROWS; r++)
            _row_prio[r] = (uint8_t)prio;
    }

    void AnsiTerminal::restart_scan() {
        for (uint8_t p = 0; p < PRIO_COUNT; p++) _resume[p] = 0;
    }

    void AnsiTerminal::_reset_grids() {
        for (uint16_t r = 0; r < _rows; r++)
            for (uint16_t c = 0; c < _cols; c++)
                _front[_idx(r, c)] = _back[_idx(r, c)] = CELL_BLANK;
    }

    // Draw cursor & geometry

    void AnsiTerminal::move_to(const CursorPos& target_in) {
        uint16_t r = std::min((uint16_t)(_rows > 0 ? _rows - 1 : 0), target_in.row);
        uint16_t c = std::min((uint16_t)(_cols > 0 ? _cols - 1 : 0), target_in.col);
        _draw_row = r;
        _draw_col = c;
    }

    // Text output & character attributes (into the FRONT grid)

    uint16_t AnsiTerminal::write_chars(const char *s, uint16_t len, bool insert) {
        if (len == 0 || _draw_row >= _rows) return 0;
        uint8_t pen = _draw_inv ? CELL_INV : 0;
        for (uint16_t i = 0; i < len; i++) {
            if (_draw_col >= _cols) {
                if (_hw_auto_wrap) { _draw_col = 0; if (++_draw_row >= _rows) { _draw_row = _rows - 1; return i; } }
                else { _draw_col = _cols; break; }   // no wrap: park at EOL, drop the rest
            }
            if (insert) {
                // Shift the row's cells right by one at the draw column (dropping
                // the overflow), then stamp. A grid-only op: present()'s shift
                // detection is what turns it back into an escape on the wire.
                for (uint16_t c = (uint16_t)(_cols - 1); c > _draw_col; c--)
                    _front[_idx(_draw_row, c)] = _front[_idx(_draw_row, (uint16_t)(c - 1))];
            }
            _front[_idx(_draw_row, _draw_col)] = (uint8_t)((s[i] & CELL_CH) | pen);
            if (_draw_col + 1 >= _cols) { _draw_col = _cols; }   // parked past last col
            else _draw_col++;
        }
        return len;
    }

    void AnsiTerminal::set_inverted(bool enable) { _draw_inv = enable; }

    void AnsiTerminal::set_auto_wrap(bool enable) {
        // A one-time wire mode (DECAWM); emit it directly and track it (the draw
        // model reads it to decide wrap vs. park).
        if (Ansi::set_auto_wrap(_serial, enable)) _hw_auto_wrap = enable;
    }

    void AnsiTerminal::set_cursor_visible(bool show) {
#ifdef TARGET_SIMULATOR
        // Dev override: BOTANIK_SHOW_CURSOR keeps the cursor visible everywhere
        // (handy for eyeballing where a paint leaves it in the sim). Read once -
        // the environment cannot change mid-run.
        static const bool force_visible = HAL::Env::u8("BOTANIK_SHOW_CURSOR", 0) != 0;
        if (force_visible) show = true;
#endif
        _want_cursor_visible = show;   // present() reconciles the wire state
    }

    void AnsiTerminal::fill_spaces(uint16_t to_col) {
        static const char sp[32] = "                               ";
        constexpr uint16_t SP_LEN = sizeof(sp) - 1;
        for (uint16_t col = _draw_col; col < to_col; col = (uint16_t)(col + SP_LEN))
            write_chars(sp, std::min((uint16_t)(to_col - col), SP_LEN));
    }

    // Content manipulation (into the FRONT grid)

    void AnsiTerminal::delete_chars(uint16_t len) {
        if (len == 0 || _draw_row >= _rows || _draw_col >= _cols) return;
        uint8_t pen = _draw_inv ? CELL_INV : 0;
        for (uint16_t c = _draw_col; c < _cols; c++) {
            uint16_t src = (uint16_t)(c + len);
            _front[_idx(_draw_row, c)] = (src < _cols) ? _front[_idx(_draw_row, src)]
                                                       : (uint8_t)(CELL_BLANK | pen);
        }
    }

    void AnsiTerminal::clear_to_eol() {
        if (_draw_row >= _rows || _draw_col >= _cols) return;   // parked past last col
        uint8_t pen = _draw_inv ? CELL_INV : 0;
        for (uint16_t c = _draw_col; c < _cols; c++)
            _front[_idx(_draw_row, c)] = (uint8_t)(CELL_BLANK | pen);
    }


    void AnsiTerminal::_shift_grid(uint8_t* g, uint16_t top, uint16_t bottom,
                                   bool up, uint16_t n) {
        // Move the band's rows within @p g and blank the freed rows. Used for
        // FRONT (in scroll_region, the desired screen) and for BACK (in present(),
        // once the matching DL/IL has actually reached the wire).
        if (up) {
            for (uint16_t r = top; (uint16_t)(r + n) <= bottom; r++)
                for (uint16_t c = 0; c < _cols; c++)
                    g[_idx(r, c)] = g[_idx((uint16_t)(r + n), c)];
            for (uint16_t r = (uint16_t)(bottom - n + 1); r <= bottom; r++)
                for (uint16_t c = 0; c < _cols; c++) g[_idx(r, c)] = CELL_BLANK;
        } else {
            for (int r = (int)bottom; r - (int)n >= (int)top; r--)
                for (uint16_t c = 0; c < _cols; c++)
                    g[_idx((uint16_t)r, c)] = g[_idx((uint16_t)(r - n), c)];
            for (uint16_t r = top; r < (uint16_t)(top + n); r++)
                for (uint16_t c = 0; c < _cols; c++) g[_idx(r, c)] = CELL_BLANK;
        }
    }

    void AnsiTerminal::scroll_region(uint16_t top, uint16_t bottom, bool up, uint16_t n) {
        if (n == 0 || top > bottom) return;
        uint16_t phys_last = (uint16_t)(_rows > 0 ? _rows - 1 : 0);
        if (bottom > phys_last) bottom = phys_last;
        uint16_t height = (uint16_t)(bottom - top + 1);
        if (n > height) n = height;
        // Shift FRONT now (the desired screen); present() shifts BACK only if it
        // emits the matching DL/IL, so a dropped scroll just repaints the band.
        _shift_grid(_front, top, bottom, up, n);
        // Overflow degrades to a band repaint by the cell diff (FRONT already
        // shifted), never a lost frame - so silently dropping the record is safe.
        if (_nscrolls < MAX_SCROLLS) _scrolls[_nscrolls++] = { top, bottom, n, up };
        // The band moved as a unit, so the diff walks the revealed rows from the
        // top rather than from wherever the last frame's budget stopped.
        restart_scan();
    }

    void AnsiTerminal::newline(bool carriage_return) {
        if (carriage_return) _draw_col = 0;
        if (_draw_row + 1 < _rows) _draw_row++;
    }

    // Field painter (shared by StatusBar and MetricPage) - built on the primitives

    void AnsiTerminal::paint_field_runs(uint16_t row, uint16_t col, const char* cf,
                                        uint16_t width, uint32_t hi) {
        // Paint the whole field, split into reverse-video segments per `hi`. No
        // per-cell gating: the back-buffer diff already drops every cell whose
        // glyph and attribute both held.
        if (width == 0) return;
        move_to({row, col});
        uint16_t seg = 0;
        bool seg_inv = (hi & 1u) != 0;
        for (uint16_t k = 0; k < width; ++k) {
            bool ki = ((hi >> k) & 1u) != 0;
            if (ki != seg_inv) {
                set_inverted(seg_inv);
                write_chars(&cf[seg], (uint16_t)(k - seg));
                seg = k; seg_inv = ki;
            }
        }
        set_inverted(seg_inv);
        write_chars(&cf[seg], (uint16_t)(width - seg));
        set_inverted(false);
    }

    // Screen bring-up / low-level ops

    void AnsiTerminal::clear_screen() {
        _serial.tx_discard();
        Ansi::cancel_sequence(_serial);
        establish_screen();
    }

    void AnsiTerminal::clear_screen_now() {
        clear_screen();
        present();
        for (uint16_t i = 0; i < tx_capacity() && !_serial.tx_empty(); i++)
            _serial.transmit();
    }

    void AnsiTerminal::establish_screen() {
        Ansi::clear_screen(_serial);
        _hw_pos = {0, 0};
        _hw_at_eol = false;
        // ED does not touch SGR, so the wire could still be in reverse video if a
        // prior paced frame's closing CSI 0 m was dropped (tx_discard on Ctrl-L).
        // Force SGR normal on the wire so it matches the _hw_inverted=false model.
        _serial.write(Ansi::CSI, sizeof(Ansi::CSI) - 1);
        _serial.write(Ansi::ATTR_NONE, sizeof(Ansi::ATTR_NONE) - 1);
        _hw_inverted = false;
        if (!_screen_ready) { _serial.write("ACAB", 4); _hw_pos.col = 4; }
        // Auto-wrap off UI-wide; hide the cursor by default; SGR normal, replace mode.
        set_auto_wrap(false);
        _hw_cursor_visible = true; _want_cursor_visible = false;
        if (Ansi::set_cursor_visible(_serial, false)) _hw_cursor_visible = false;
        Ansi::set_insert_mode(_serial, false);   // defensive: we never set SM4
        _screen_ready = true;
        // The screen is now physically blank: both grids blank, draw cursor home,
        // and the diff walks the repaint that follows from the top.
        _reset_grids();
        restart_scan();
        _draw_row = _draw_col = 0; _draw_inv = false;
    }

    void AnsiTerminal::setup_terminal() { Ansi::terminal_setup(_serial, _type); }

    // present() - reconcile FRONT to the wire
    // ------------------------------------------------------------------------

    bool AnsiTerminal::_go(const CursorPos& target) {
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

    uint16_t AnsiTerminal::_put_run(uint16_t row, uint16_t a, uint16_t b) {
        uint16_t before = tx_used();
        if (!_go({row, a})) return 0;
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
        _emit_scrolls();
        // Park only once the frame is fully reconciled: mid-repaint the runs are
        // moving the cursor anyway, so re-homing it would burn a move per tick.
        if (_emit_diff()) _park_cursor();
    }

    void AnsiTerminal::_emit_scrolls() {
        // Emit each pending band DL/IL and shift BACK to match ONLY if every part
        // of it reached the wire. Shifting after a dropped escape would leave the
        // diff believing those rows were already right, so the band would stay
        // wrong until a full clear.
        for (uint8_t i = 0; i < _nscrolls; i++) {
            const PendingScroll& s = _scrolls[i];
            bool restore = (s.bottom < (uint16_t)(_rows - 1));
            uint16_t back = (uint16_t)(s.bottom - s.n + 1);
            bool ok;
            if (s.up) {
                ok = _go({s.top, 0}) && Ansi::delete_rows(_serial, s.n);
                if (ok && restore) ok = _go({back, 0}) && Ansi::insert_rows(_serial, s.n);
            } else {
                ok = !restore || (_go({back, 0}) && Ansi::delete_rows(_serial, s.n));
                if (ok) ok = _go({s.top, 0}) && Ansi::insert_rows(_serial, s.n);
            }
            if (ok) _shift_grid(_back, s.top, s.bottom, s.up, s.n);   // mirror the wire
        }
        _nscrolls = 0;
    }

    uint16_t AnsiTerminal::_shift_row(uint16_t row, uint16_t budget, bool& defer) {
        defer = false;
        // A row whose content slid sideways (the editor inserting a character, a
        // column widening) differs from BACK at every column past the edit, so
        // the plain run diff rewrites the whole tail. One escape does it instead:
        // SM4/RM4 insert mode to open cells, DCH to close them, both shifting the
        // cells *with* their attributes.
        //
        // @return bytes emitted, or 0 when a shift does not apply or does not pay
        // (the caller's run loop then reconciles the row the ordinary way).
        uint16_t a = 0;
        while (a < _cols && _front[_idx(row, a)] == _back[_idx(row, a)]) a++;
        if (a >= _cols) return 0;                          // row already matches
        uint16_t last = (uint16_t)(_cols - 1);
        while (last > a && _front[_idx(row, last)] == _back[_idx(row, last)]) last--;

        // Short diffs are cheaper to just rewrite than to bracket in an escape.
        if ((uint16_t)(last - a + 1) < SHIFT_MIN_SPAN) return 0;

        // Derive the width from the content: the cell at column `a` on one side
        // must reappear further along on the other, and where it lands *is* the
        // width. A few candidate landings cost a few memcmps, and this puts no
        // ceiling on how far a row may have slid (a fixed cap silently turns a
        // wider slide into a full-row rewrite).
        const uint8_t* fr = &_front[_idx(row, 0)];
        const uint8_t* bk = &_back [_idx(row, 0)];
        uint16_t right = 0, left = 0;

        // Right shift by n: BACK's cell at `a` is pushed out to FRONT's `a + n`.
        for (uint16_t p = (uint16_t)(a + 1), tries = 0;
             p < _cols && tries < SHIFT_CANDIDATES; p++) {
            if (fr[p] != bk[a]) continue;
            tries++;
            uint16_t n = (uint16_t)(p - a);
            if (memcmp(fr + a + n, bk + a, (size_t)(_cols - a - n)) == 0) { right = n; break; }
        }
        // Left shift by n: BACK's cell at `a + n` is pulled back to FRONT's `a`.
        for (uint16_t p = (uint16_t)(a + 1), tries = 0;
             p < _cols && tries < SHIFT_CANDIDATES; p++) {
            if (bk[p] != fr[a]) continue;
            tries++;
            uint16_t n = (uint16_t)(p - a);
            if (memcmp(fr + a, bk + a + n, (size_t)(_cols - a - n)) == 0) { left = n; break; }
        }
        if (right == 0 && left == 0) return 0;

        // Cost both candidates and take the cheaper, but only if it beats
        // rewriting the span outright. Costing matters as much as detecting:
        // repeating text matches itself at its own period, so both directions
        // often verify, and choosing the wrong one is worse than choosing neither.
        uint16_t plain = _run_cost(row, a, (uint16_t)(last + 1));
        uint16_t cost_right = 0xFFFF, cost_left = 0xFFFF;

        // Right: the inserted cells go out as one run, so they must share an
        // attribute. A mixed run disqualifies the candidate rather than being
        // bracketed cell by cell.
        bool ins_inv = (fr[a] & CELL_INV) != 0;
        if (right != 0) {
            bool uniform = true;
            for (uint16_t c = a; c < (uint16_t)(a + right); c++)
                if (((fr[c] & CELL_INV) != 0) != ins_inv) { uniform = false; break; }
            if (uniform)
                cost_right = (uint16_t)(8 + 4 + right + 4 + (ins_inv != _hw_inverted ? 5 : 0));
        }

        // Left: the DCH is a handful of bytes, but it blanks `n` cells at the
        // right edge and any of those FRONT wants filled must be painted behind
        // it. Ignoring that is what makes a wide coincidental match look free.
        if (left != 0) {
            uint16_t tail = 0;
            for (uint16_t c = (uint16_t)(_cols - left); c < _cols; c++)
                if (_front[_idx(row, c)] != (uint8_t)CELL_BLANK) tail++;
            cost_left = (uint16_t)(8 + 6 + (_hw_inverted ? 5 : 0) + (tail != 0 ? tail + 8 : 0));
        }

        if (cost_right <= cost_left) left = 0; else right = 0;
        uint16_t cost = right ? cost_right : cost_left;
        if (cost >= plain) return 0;
        if (_serial.availableForWrite() < cost) { defer = true; return 0; }
        // A shift the tick's budget cannot hold is owed the next tick, not the
        // fallback: the run loop's whole-row rewrite is oversized, so it escapes
        // the very budget the shift was just refused for. Only a slide too wide
        // to ever fit a whole budget takes that escape itself.
        bool oversized = cost >= _tx_cold_depth;
        if (oversized ? (budget == 0) : (cost > budget)) { defer = true; return 0; }

        uint16_t before = tx_used();
        if (!_go({row, a})) return 0;

        if (right != 0) {
            _set_hw_inverted(ins_inv);
            // Insert mode: the glyphs written push the rest of the row right.
            if (!Ansi::set_insert_mode(_serial, true)) return 0;
            char buf[GRID_COLS];
            for (uint16_t c = a; c < (uint16_t)(a + right); c++)
                buf[c - a] = (char)(fr[c] & CELL_CH);
            _serial.write(buf, right);
            Ansi::set_insert_mode(_serial, false);
            // Mirror the wire in BACK: shift right, then stamp what we wrote.
            for (uint16_t c = (uint16_t)(_cols - 1); c >= (uint16_t)(a + right); c--)
                _back[_idx(row, c)] = _back[_idx(row, (uint16_t)(c - right))];
            for (uint16_t c = a; c < (uint16_t)(a + right); c++)
                _back[_idx(row, c)] = _front[_idx(row, c)];
            _hw_pos.col = (uint16_t)(a + right);
        } else {
            // DCH fills the cells it frees using the current attributes on
            // terminals with background-colour erase, so drop reverse video first
            // or those cells come back inverted.
            _set_hw_inverted(false);
            if (!Ansi::delete_chars(_serial, left)) return 0;
            for (uint16_t c = a; (uint16_t)(c + left) < _cols; c++)
                _back[_idx(row, c)] = _back[_idx(row, (uint16_t)(c + left))];
            for (uint16_t c = (uint16_t)(_cols - left); c < _cols; c++)
                _back[_idx(row, c)] = CELL_BLANK;
        }
        _hw_at_eol = false;
        uint16_t after = tx_used();
        return (after > before) ? (uint16_t)(after - before) : 0;
    }

    bool AnsiTerminal::_reconcile_row(uint16_t row, uint16_t& budget) {
        // Nothing pending: skip in one memcmp rather than two per-cell walks.
        if (memcmp(&_front[_idx(row, 0)], &_back[_idx(row, 0)], _cols) == 0) return true;
        // A sideways slide is one escape; try that before rewriting cells. It may
        // leave a remainder (the cells it could not express), which the run loop
        // below then reconciles normally.
        bool defer = false;
        uint16_t moved = _shift_row(row, budget, defer);
        if (defer) return false;      // owed the next tick, not a full rewrite
        budget = (moved < budget) ? (uint16_t)(budget - moved) : 0;
        if (moved != 0 && budget == 0) return false;
        uint16_t col = 0;
        while (col < _cols) {
            if (_front[_idx(row, col)] == _back[_idx(row, col)]) { col++; continue; }
            uint16_t a = col, b = col;
            while (b < _cols && _front[_idx(row, b)] != _back[_idx(row, b)]) b++;
            uint16_t cost = _run_cost(row, a, b);
            if (_serial.availableForWrite() < cost) return false;  // ring full
            // A run that needs the whole budget or more can never fit inside one,
            // so it goes out as soon as the ring is shallow (and ends the tick).
            // This has to be >=, not >: tx_used() never reports an empty ring as
            // 0, so budget is always at least a byte under _tx_cold_depth, and a
            // run costing exactly _tx_cold_depth would otherwise be too big to
            // fit and not big enough to escape - stuck for good.
            bool oversized = cost >= _tx_cold_depth;
            if (oversized ? (budget == 0) : (cost > budget)) return false;
            uint16_t sent = _put_run(row, a, b);
            if (sent == 0) return false;
            budget = (sent < budget) ? (uint16_t)(budget - sent) : 0;
            if (oversized || budget == 0) return false;
            col = b;
        }
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
