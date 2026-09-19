/**
 * @file terminal.cpp
 * @brief AnsiTerminal's drawing half: geometry, the FRONT grid renderers paint
 *        into, and screen bring-up. present() is in reconcile.cpp.
 */
#include "libbotanik/utils/minmax.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE

#include <cstdint>
#include <cstring>
#include <cstdlib>

#include "libbotanik/config/terminal.h"

#include "libbotanik/console/ansi.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/serial.h"


namespace Botanik { namespace Terminal {

    // Lifecycle

    AnsiTerminal::AnsiTerminal(Utils::BufferedSerial& serial) :
        _serial(serial),
        _cols(1),
        _rows(1),
        _type(ConsoleConfig().type)
    {
        _tx_cold_depth = depth_for_baud(ConsoleConfig().baud, Config::TX_COLD_TARGET_MS);
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
        // The content changed as a whole, so whatever a pending change
        // described is gone with it.
        _drop_changes();
        _restart_walk();
        _touch_all();
    }

    void AnsiTerminal::_reset_grids() {
        for (uint16_t r = 0; r < _rows; r++)
            for (uint16_t c = 0; c < _cols; c++)
                _front[_idx(r, c)] = _back[_idx(r, c)] = CELL_BLANK;
        _dirty = 0;             // blank against blank: nothing for the walk
    }

    // Draw cursor & geometry

    void AnsiTerminal::move_to(const CursorPos& target_in) {
        uint16_t r = Utils::min((uint16_t)(_rows > 0 ? _rows - 1 : 0), target_in.row);
        uint16_t c = Utils::min((uint16_t)(_cols > 0 ? _cols - 1 : 0), target_in.col);
        _draw_row = r;
        _draw_col = c;
    }

    // Text output & character attributes (into the FRONT grid)

    uint16_t AnsiTerminal::write_chars(const char *s, uint16_t len) {
        if (len == 0 || _draw_row >= _rows) return 0;
        _touch(_draw_row);
        uint8_t pen = _draw_inv ? CELL_INV : 0;
        for (uint16_t i = 0; i < len; i++) {
            if (_draw_col >= _cols) return i;     // parked at EOL: drop the rest
            _front[_idx(_draw_row, _draw_col)] = (uint8_t)((s[i] & CELL_CH) | pen);
            if (_draw_col + 1 >= _cols) { _draw_col = _cols; }   // parked past last col
            else _draw_col++;
        }
        return len;
    }

    void AnsiTerminal::set_inverted(bool enable) { _draw_inv = enable; }

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
            write_chars(sp, Utils::min((uint16_t)(to_col - col), SP_LEN));
    }

    // Content manipulation (into the FRONT grid)

    void AnsiTerminal::clear_to_eol() {
        if (_draw_row >= _rows || _draw_col >= _cols) return;   // parked past last col
        _touch(_draw_row);
        const uint8_t v = (uint8_t)(CELL_BLANK | (_draw_inv ? CELL_INV : 0));
        for (uint16_t c = _draw_col; c < _cols; c++) _front[_idx(_draw_row, c)] = v;
    }


    void AnsiTerminal::clear_rows(uint16_t top, uint16_t bottom) {
        for (uint16_t r = top; r <= bottom && r < _rows; r++) {
            _touch(r);
            for (uint16_t c = 0; c < _cols; c++) _front[_idx(r, c)] = CELL_BLANK;
        }
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
        // SGR normal BEFORE the ED: the wire may still be in reverse video (a
        // paced frame's closing CSI 0 m dropped by tx_discard, or a region
        // highlight the last run ended on), and a terminal with
        // background-colour erase fills what ED clears with the pen it holds.
        _serial.write(Ansi::CSI, sizeof(Ansi::CSI) - 1);
        _serial.write(Ansi::ATTR_NONE, sizeof(Ansi::ATTR_NONE) - 1);
        _hw_inverted = false;
        Ansi::clear_screen(_serial);
        _hw_pos = {0, 0};
        _hw_at_eol = false;
        // Deliberate four-column boot marker; _hw_pos.col accounts for it.
        if (!_screen_ready) { _serial.write("ACAB", 4); _hw_pos.col = 4; }
        // Auto-wrap off UI-wide; hide the cursor by default; SGR normal, replace mode.
        Ansi::set_auto_wrap(_serial, false);
        _hw_cursor_visible = true; _want_cursor_visible = false;
        if (Ansi::set_cursor_visible(_serial, false)) _hw_cursor_visible = false;
        // A fresh screen starts in replace mode whatever the last frame left.
        Ansi::set_insert_mode(_serial, false);
        _hw_insert = false;
        _screen_ready = true;
        // The screen is now physically blank: both grids blank, draw cursor home,
        // and the diff walks the repaint that follows from the top.
        _reset_grids();
        restart_scan();
        _draw_row = _draw_col = 0; _draw_inv = false;
    }

    void AnsiTerminal::setup_terminal() { Ansi::terminal_setup(_serial, _type); }

}}

#endif // BOTANIK_FEATURE_CONSOLE
