#include <cstdint>
#include <cstring> // For strlen, memcpy etc.
#include <algorithm> // For std::min

#include "libbotanik/utils/serial.h"
#include "libbotanik/terminal/parser.h"
#include "libbotanik/terminal/terminal.h"
#include "libbotanik/terminal/ansi.h"

#include "libbotanik/pages/home.h"
#include "libbotanik/pages/logs.h"
#include "libbotanik/pages/shell.h"
#include "libbotanik/pages/status.h"


namespace Botanik { namespace Terminal {

    namespace {
        using namespace Botanik::Utils;
        using namespace Botanik::Pages;
    }

    // Constructor
    AnsiTerminal::AnsiTerminal(Utils::BufferedSerial& serial, uint16_t cols, uint16_t rows, uint32_t* timestamp_ms) :
        _parser(serial, timestamp_ms),
        _serial(serial),
        _cols(cols),
        _rows(rows)
    {
    }

    // Step through the terminal handling code
    void AnsiTerminal::step() {
        _serial.step();
        while (true) {
            InputEvent& ev = _parser.parse();
            if (ev.type == InputEvent::EventType::NONE) break;
            if (_pages_len == 0 || _pages[_page_index] == nullptr) continue;
            Pages::Page* page = _pages[_page_index];
            if (ev.type == InputEvent::EventType::CONTENT) {
                for (uint16_t i = 0; i < ev.batch_len; i++)
                    page->handle_text(ev.batch_data[i]);
            } else if (ev.type == InputEvent::EventType::COMMAND) {
                page->handle_command(ev.command);
            }
        }
    }


    // Move cursor to an absolute screen position
    void AnsiTerminal::move_to(const CursorPos& target, bool skip) {
        if (target == _hw_pos) {
            return;
        }

        if (!skip) {
            // Optimizations for short moves, otherwise goto_absolute
            if (target.row == _hw_pos.row) { // Same row
                if (target.col == 0) {
                    Ansi::move_line_begin(_serial);
                } else if (target.col < _hw_pos.col && (_hw_pos.col - target.col) < 4) {
                    Ansi::move_left(_serial, _hw_pos.col - target.col);
                } else if (target.col > _hw_pos.col && (target.col - _hw_pos.col) < 4) {
                    Ansi::move_right(_serial, target.col - _hw_pos.col);
                } else {
                    Ansi::goto_absolute(_serial, target);
                }
            } else if (target.col == _hw_pos.col) { // Same column
                if (target.row < _hw_pos.row && (_hw_pos.row - target.row) < 4) {
                    Ansi::move_up(_serial, _hw_pos.row - target.row);
                } else if (target.row > _hw_pos.row && (target.row - _hw_pos.row) < 4) {
                    Ansi::move_down(_serial, target.row - _hw_pos.row);
                } else {
                    Ansi::goto_absolute(_serial, target);
                }
            } else { // Different row and column, use absolute move
                Ansi::goto_absolute(_serial, target);
            }
        }
        _hw_pos = target;
    }

    // Move cursor relatively by `amount` steps
    void AnsiTerminal::move_cursor(Ansi::MoveDirection direction, uint16_t amount) {
        if (amount == 0) return;
        switch (direction) {
            case Ansi::MoveDirection::LEFT:
                Ansi::move_left(_serial, amount);
                _hw_pos.col = (_hw_pos.col >= amount) ? (_hw_pos.col - amount) : 0;
                break;
            case Ansi::MoveDirection::RIGHT:
                Ansi::move_right(_serial, amount);
                _hw_pos.col = std::min((uint16_t)(_hw_pos.col + amount), (uint16_t)(_cols - 1));
                break;
            case Ansi::MoveDirection::UP:
                Ansi::move_up(_serial, amount);
                _hw_pos.row = (_hw_pos.row >= amount) ? (_hw_pos.row - amount) : 0;
                break;
            case Ansi::MoveDirection::DOWN:
                Ansi::move_down(_serial, amount);
                _hw_pos.row = std::min((uint16_t)(_hw_pos.row + amount), (uint16_t)(_rows - 1));
                break;
        }
    }

    // Write `len` chars at cursor, advancing it right.
    void AnsiTerminal::write_chars(const char *s, uint16_t len, bool insert) {
        // TODO: assert all bytes in s are in [0x20, 0x7E] — non-printable bytes
        //       would corrupt cursor tracking and must be rejected by the caller
        if (len == 0) return;

        if (insert) {
            Ansi::set_insert_mode(_serial, true);
        }

        // Write characters
        // Need to handle line wrapping
        for (uint16_t i = 0; i < len; ++i) {
            _serial.write(s + i, 1); // Write one char at a time
            _hw_pos.col++;
            if (_hw_pos.col >= _cols) { // Check for line wrap
                _hw_pos.col = 0;
                _hw_pos.row++;
                if (_hw_pos.row >= _rows) { // Handle screen scroll (basic)
                    _hw_pos.row = _rows - 1; // Stay on the last row
                }
            }
            }

        if (insert) {
            Ansi::set_insert_mode(_serial, false);
        }
    }

    // Delete `len` chars at cursor, shifting the rest of the line left
    void AnsiTerminal::delete_chars(uint16_t len) {
        if (len == 0) return;
        Ansi::delete_chars(_serial, len);
        // Cursor position is typically not affected by DCH itself,
        // but the content shifts left. The logical cursor position remains.
    }

    // Erase content from cursor to the end of the line
    void AnsiTerminal::clear_to_eol() {
        Ansi::clear_to_eol(_serial);
        // Cursor position is typically not affected by EL
    }

    // Erase an entire row
    void AnsiTerminal::clear_row(uint16_t row_to_clear) {
        CursorPos original_pos = _hw_pos; // Store original position
        move_to({row_to_clear, 0});       // Move to the beginning of the target row
        _serial.write(Ansi::CSI, strlen(Ansi::CSI));
        _serial.write("2K", 2);           // Emit ESC[2K to clear the entire line
        move_to(original_pos);            // Restore original cursor position
    }

    // Emit a line feed
    void AnsiTerminal::line_feed(bool carriage_return) {
        if (carriage_return) {
            _serial.write("\r\n", 2); // CR+LF
            _hw_pos.col = 0;
        } else {
            _serial.write("\n", 1); // LF only
        }
        _hw_pos.row++;
        if (_hw_pos.row >= _rows) { // Handle screen scroll
            _hw_pos.row = _rows - 1;
        }
    }

    // Insert blank rows
    void AnsiTerminal::insert_rows(uint16_t count) {
        if (count == 0) return;
        Ansi::insert_rows(_serial, count);
        // Cursor position is typically not affected by IL, only content shifts.
    }

    // Delete rows
    void AnsiTerminal::delete_rows(uint16_t count) {
        if (count == 0) return;
        Ansi::delete_rows(_serial, count);
        // Cursor position is typically not affected by DL, only content shifts.
    }

    // Enable or disable terminal auto-wrap (DECAWM)
    void AnsiTerminal::set_auto_wrap(bool enable) {
        Ansi::set_auto_wrap(_serial, enable);
    }

    // Set/clear reverse-video character attribute
    void AnsiTerminal::set_inverted(bool enable) {
        if (enable && !_hw_inverted) {
            _serial.write(Ansi::CSI, strlen(Ansi::CSI));
            _serial.write(Ansi::ATTR_NEG, strlen(Ansi::ATTR_NEG));
            _hw_inverted = true;
        } else if (!enable && _hw_inverted) {
            _serial.write(Ansi::CSI, strlen(Ansi::CSI));
            _serial.write(Ansi::ATTR_NONE, strlen(Ansi::ATTR_NONE));
            _hw_inverted = false;
        }
    }

    void AnsiTerminal::clear_screen() {
        Ansi::clear_screen(_serial);
        _hw_pos = {0, 0};
    }

    uint16_t AnsiTerminal::tx_available() const {
        return _serial.availableForWrite();
    }

    void AnsiTerminal::switch_to(uint8_t index) {
        if (index < _pages_len && _pages[index] != nullptr) {
            _page_index = index;
            // TODO: deactivate current page, clear screen, call focus() on new page
        }
    }

}}