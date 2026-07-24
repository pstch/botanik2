// REVIEW_35: TODO
/**
 * @file terminal.cpp
 * @brief AnsiTerminal implementation - the VT device over a screen back-buffer.
 *
 * Renderers paint immediate-mode into the FRONT cell grid through the usual ops;
 * present() diffs FRONT against BACK (the wire) and emits the minimal, paced ANSI
 * to reconcile them. See terminal.h.
 */
#include <cstdint>
#include <algorithm>
#ifdef TARGET_SIMULATOR
#include <cstdlib>   // getenv, for the BOTANIK_SHOW_CURSOR dev override
#endif

#include "libbotanik_config.h"

#include "libbotanik/utils/serial.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/console/ansi.h"


namespace Botanik { namespace Terminal {

    // Lifecycle

    AnsiTerminal::AnsiTerminal(Utils::BufferedSerial& serial, uint16_t cols,
                               uint16_t rows, uint32_t baud) :
        _serial(serial),
        _cols(cols ? (cols < GRID_COLS ? cols : GRID_COLS) : 1),
        _rows(rows ? (rows < GRID_ROWS ? rows : GRID_ROWS) : 1)
    {
        _tx_cold_depth = depth_for_baud(baud, Config::TX_COLD_TARGET_MS);
        for (uint16_t i = 0; i < GRID_CELLS; i++) { _front[i] = CELL_BLANK; _back[i] = CELL_BLANK; }
    }

    void AnsiTerminal::_reset_grids() {
        for (uint16_t r = 0; r < _rows; r++)
            for (uint16_t c = 0; c < _cols; c++)
                _front[_idx(r, c)] = _back[_idx(r, c)] = CELL_BLANK;
    }

    // Draw cursor & geometry

    void AnsiTerminal::move_to(const CursorPos& target_in, bool /*skip*/) {
        uint16_t r = std::min((uint16_t)(_rows > 0 ? _rows - 1 : 0), target_in.row);
        uint16_t c = std::min((uint16_t)(_cols > 0 ? _cols - 1 : 0), target_in.col);
        _draw_row = r;
        _draw_col = c;
        _live_cursor_pos = { r, c };
    }

    void AnsiTerminal::restore_cursor(const CursorSnapshot& snap, bool restore_pos) {
        set_inverted(snap.inverted);
        if (restore_pos) {
            move_to(snap.pos);
            if (snap.at_eol) _draw_col = _cols;   // re-park past the last column
        }
        set_cursor_visible(snap.cursor_visible);
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
                // Shift the row's cells right by one at the draw column (drop the
                // overflow), then stamp - matches SM4 insert mode.
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
        static const bool force_visible = getenv("BOTANIK_SHOW_CURSOR") != nullptr;
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

    void AnsiTerminal::clear_to_eos() {
        if (_draw_row >= _rows) return;
        uint8_t pen = _draw_inv ? CELL_INV : 0;
        uint16_t c0 = (_draw_col < _cols) ? _draw_col : 0;
        for (uint16_t c = c0; c < _cols; c++) _front[_idx(_draw_row, c)] = (uint8_t)(CELL_BLANK | pen);
        for (uint16_t r = (uint16_t)(_draw_row + 1); r < _rows; r++)
            for (uint16_t c = 0; c < _cols; c++) _front[_idx(r, c)] = (uint8_t)(CELL_BLANK | pen);
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
    }

    void AnsiTerminal::newline(bool carriage_return) {
        if (carriage_return) _draw_col = 0;
        if (_draw_row + 1 < _rows) _draw_row++;
    }

    // Field painter (shared by StatusBar and MetricPage) - built on the primitives

    uint16_t AnsiTerminal::emit_field_run(uint16_t row, uint16_t col, const char* cf,
                                          uint16_t a, uint16_t b, uint32_t hi) {
        move_to({row, (uint16_t)(col + a)});
        uint16_t seg = a;
        bool seg_inv = ((hi >> a) & 1u) != 0;
        uint16_t written = 0;
        for (uint16_t k = a; k < b; ++k) {
            bool ki = ((hi >> k) & 1u) != 0;
            if (ki != seg_inv) {
                set_inverted(seg_inv);
                written += write_chars(&cf[seg], (uint16_t)(k - seg));
                seg = k; seg_inv = ki;
            }
        }
        set_inverted(seg_inv);
        written += write_chars(&cf[seg], (uint16_t)(b - seg));
        return written;
    }

    void AnsiTerminal::paint_field_runs(uint16_t row, uint16_t col, const char* cf,
                                        uint16_t width, uint32_t emit, uint32_t hi) {
        if (emit == 0) return;
        const uint16_t MERGE = 4;
        uint32_t render = emit;
        int last = -1;
        for (uint16_t i = 0; i < width; ++i) {
            if (!((emit >> i) & 1u)) continue;
            if (last >= 0 && (int)i - last - 1 <= (int)MERGE)
                for (int g = last + 1; g < (int)i; ++g) render |= (1u << g);
            last = (int)i;
        }
        uint16_t i = 0;
        while (i < width) {
            if (!((render >> i) & 1u)) { i++; continue; }
            uint16_t a = i;
            while (i < width && ((render >> i) & 1u)) i++;
            emit_field_run(row, col, cf, a, i, hi);
        }
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
        _hw_inverted = false;
        if (!_screen_ready) { _serial.write("ACAB", 4); _hw_pos.col = 4; }
        // Auto-wrap off UI-wide; hide the cursor by default; SGR normal, replace mode.
        set_auto_wrap(false);
        _hw_cursor_visible = true; _want_cursor_visible = false;
        if (Ansi::set_cursor_visible(_serial, false)) _hw_cursor_visible = false;
        Ansi::set_insert_mode(_serial, false); _hw_insert_mode = false;
        _screen_ready = true;
        // The screen is now physically blank: both grids blank, draw cursor home.
        _reset_grids();
        _draw_row = _draw_col = 0; _draw_inv = false;
    }

    void AnsiTerminal::setup_vt102()   { Ansi::vt102_setup(_serial); }
    void AnsiTerminal::setup_minitel() { Ansi::minitel_setup(_serial); }
    void AnsiTerminal::cancel_sequence() { Ansi::cancel_sequence(_serial); }

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

    uint16_t AnsiTerminal::_put_run(uint16_t row, uint16_t a, uint16_t b) {
        uint16_t before = tx_used();
        if (_hw_insert_mode) { Ansi::set_insert_mode(_serial, false); _hw_insert_mode = false; }
        if (!_go({row, a})) return 0;
        char buf[GRID_COLS];
        uint16_t k = a;
        while (k < b) {
            bool inv = (_front[_idx(row, k)] & CELL_INV) != 0;
            if (inv != _hw_inverted) {
                _serial.write(Ansi::CSI, sizeof(Ansi::CSI) - 1);
                _serial.write(inv ? Ansi::ATTR_NEG : Ansi::ATTR_NONE,
                              inv ? sizeof(Ansi::ATTR_NEG) - 1 : sizeof(Ansi::ATTR_NONE) - 1);
                _hw_inverted = inv;
            }
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
        // 1) Pending band scrolls: emit the DL/IL and, on success, shift BACK to
        //    match (so the cell diff then repaints only the revealed rows). On
        //    failure BACK stays put and the diff repaints the whole band - correct,
        //    just more bytes.
        for (uint8_t i = 0; i < _nscrolls; i++) {
            const PendingScroll& s = _scrolls[i];
            bool restore = (s.bottom < (uint16_t)(_rows - 1));
            bool ok = true;
            if (s.up) {
                if (_go({s.top, 0})) delete_rows_wire(s.n); else ok = false;
                if (ok && restore && _go({(uint16_t)(s.bottom - s.n + 1), 0})) insert_rows_wire(s.n);
                else if (restore) ok = false;
            } else {
                if (restore) { if (_go({(uint16_t)(s.bottom - s.n + 1), 0})) delete_rows_wire(s.n); else ok = false; }
                if (ok && _go({s.top, 0})) insert_rows_wire(s.n); else ok = false;
            }
            if (ok) _shift_grid(_back, s.top, s.bottom, s.up, s.n);   // mirror the wire
        }
        _nscrolls = 0;

        // 2) Paced cell diff.
        uint16_t used   = tx_used();
        uint16_t budget = (used < _tx_cold_depth) ? (uint16_t)(_tx_cold_depth - used) : 0;
        for (uint16_t row = 0; row < _rows; row++) {
            uint16_t col = 0;
            while (col < _cols) {
                if (_front[_idx(row, col)] == _back[_idx(row, col)]) { col++; continue; }
                uint16_t a = col, b = col;
                while (b < _cols && _front[_idx(row, b)] != _back[_idx(row, b)]) b++;
                uint16_t cost = _run_cost(row, a, b);
                if (_serial.availableForWrite() < cost) return;   // ring full: defer
                bool oversized = cost > _tx_cold_depth;
                if (oversized ? (budget == 0) : (cost > budget)) return;   // paced: defer
                uint16_t sent = _put_run(row, a, b);
                if (sent == 0) return;
                budget = (sent < budget) ? (uint16_t)(budget - sent) : 0;
                if (oversized || budget == 0) return;
                col = b;
            }
        }

        // 3) Cursor: visibility, then park it where a visible cursor belongs.
        if (_want_cursor_visible != _hw_cursor_visible &&
            Ansi::set_cursor_visible(_serial, _want_cursor_visible))
            _hw_cursor_visible = _want_cursor_visible;
        if (_hw_cursor_visible &&
            !(_live_cursor_pos.row == 0 && _live_cursor_pos.col == 0))
            _go(_live_cursor_pos);
    }

    // Wire-side row shifts used only by present() for the scroll DL/IL.
    void AnsiTerminal::delete_rows_wire(uint16_t count) { Ansi::delete_rows(_serial, count); }
    void AnsiTerminal::insert_rows_wire(uint16_t count) { Ansi::insert_rows(_serial, count); }

}}
