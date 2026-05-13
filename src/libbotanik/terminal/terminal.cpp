#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>

#include "libbotanik/utils/serial.h"
#include "libbotanik/terminal/parser.h"
#include "libbotanik/terminal/terminal.h"
#include "libbotanik/terminal/ansi.h"
#include "libbotanik/logs/logger.h"

#include "libbotanik/pages/home.h"
#include "libbotanik/pages/logs.h"
#include "libbotanik/pages/shell.h"
#include "libbotanik/pages/status.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/plcio/sync.h"


namespace Botanik { namespace Terminal {

    namespace {
        using namespace Botanik::Utils;
        using namespace Botanik::Pages;
    }

    // Constructor
    AnsiTerminal::AnsiTerminal(Utils::BufferedSerial& serial, uint16_t cols, uint16_t rows,
                               uint32_t* timestamp_ms, Logs::Logger* logger,
                               Scheduler::SchedulerContext* sched_ctx,
                               PlcIO::PlcSyncContext* plc_ctx) :
        _parser(serial, timestamp_ms),
        _serial(serial),
        _cols(cols),
        _rows(rows),
        _logger(logger),
        _sched_ctx(sched_ctx),
        _plc_ctx(plc_ctx)
    {
    }

    // Step through the terminal handling code
    void AnsiTerminal::step() {
        using Command = InputEvent::Command;
        while (true) {
            InputEvent& ev = _parser.parse();
            if (ev.type == InputEvent::EventType::NONE) break;
            if (_pages_len == 0 || _pages[_page_index] == nullptr) continue;
            Pages::Page* page = _pages[_page_index];
            if (ev.type == InputEvent::EventType::CONTENT) {
                for (uint16_t i = 0; i < ev.batch_len; i++)
                    page->handle_text(ev.batch_data[i]);
            } else if (ev.type == InputEvent::EventType::COMMAND) {
                // Terminal-level intercepts — consumed before reaching the page
                if (ev.command == Command::CTRL_L) {
                    clear_screen();
                    draw_status_row();
                    page->redraw();
                    continue;
                }
                if (ev.command == Command::HOME)   { switch_to(0);  continue; }
                if (ev.command >= Command::ALT_1 && ev.command <= Command::ALT_9) {
                    switch_to((uint8_t)((uint16_t)ev.command - (uint16_t)Command::ALT_1));
                    continue;
                }
                if (ev.command == Command::ALT_0) { switch_to(9); continue; }
                page->handle_command(ev.command);
            }
        }
        if (_pages_len > 0 && _pages[_page_index] != nullptr)
            _pages[_page_index]->tick();
        if ((++_status_tick % 100) == 0)
            draw_status_row();
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
            // FIXME: defend against non-printable bytes corrupting cursor tracking
            char c = Ansi::is_printable(s[i]) ? s[i] : '?';
            _serial.write(&c, 1);
            _hw_pos.col++;
            if (_hw_pos.col >= _cols) {
                if (_hw_auto_wrap) {
                    _hw_pos.col = 0;
                    _hw_pos.row++;
                    if (_hw_pos.row >= _rows) _hw_pos.row = _rows - 1;
                } else {
                    // DECAWM off: cursor stays at last column, doesn't wrap
                    _hw_pos.col = _cols - 1;
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
        CursorPos original_pos = _hw_pos;
        move_to({row_to_clear, 0});
        _serial.write(Ansi::CSI, sizeof(Ansi::CSI) - 1);
        _serial.write("2K", 2);
        move_to(original_pos);
    }

    // Emit a line feed
    void AnsiTerminal::line_feed(bool carriage_return) {
        bool was_last_row = (_hw_pos.row >= _rows - 1);
        if (carriage_return) {
            _serial.write("\r\n", 2); // CR+LF
            _hw_pos.col = 0;
        } else {
            _serial.write("\n", 1); // LF only
        }
        _hw_pos.row++;
        if (_hw_pos.row >= _rows) {
            _hw_pos.row = _rows - 1;
            if (was_last_row) draw_status_row(); // repaint row 0 after hardware scroll
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
        _hw_auto_wrap = enable;
    }

    // Set/clear reverse-video character attribute
    void AnsiTerminal::set_inverted(bool enable) {
        if (enable && !_hw_inverted) {
            _serial.write(Ansi::CSI,      sizeof(Ansi::CSI)      - 1);
            _serial.write(Ansi::ATTR_NEG, sizeof(Ansi::ATTR_NEG) - 1);
            _hw_inverted = true;
        } else if (!enable && _hw_inverted) {
            _serial.write(Ansi::CSI,       sizeof(Ansi::CSI)       - 1);
            _serial.write(Ansi::ATTR_NONE, sizeof(Ansi::ATTR_NONE) - 1);
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

    void AnsiTerminal::draw_status_row() {
        // Build right-side metrics string (fixed 26-char field)
        char metrics[28];
        const char* plc_str = "---";
        if (_plc_ctx) {
            switch (_plc_ctx->status()) {
                case PlcIO::PLC_UNKNOWN:  plc_str = "UNK"; break;
                case PlcIO::PLC_RUNNING:  plc_str = "RUN"; break;
                case PlcIO::PLC_TIMEOUT:  plc_str = "TMO"; break;
                case PlcIO::PLC_FAULT:    plc_str = "FLT"; break;
            }
        }
        unsigned cpu = _sched_ctx ? (unsigned)(_sched_ctx->cpu_load_ema    * 100.0f + 0.5f) : 0;
        unsigned stk = _sched_ctx ? (unsigned)(_sched_ctx->stack_usage_ema * 100.0f + 0.5f) : 0;
        int mlen = snprintf(metrics, sizeof(metrics),
            "PLC:%s cpu:%3u%% stk:%3u%% ", plc_str, cpu, stk);
        if (mlen < 0) mlen = 0;

        // Save state
        CursorPos saved_pos = _hw_pos;
        bool      saved_inv = _hw_inverted;

        // Draw full inverted row 0
        move_to({0, 0});
        set_inverted(true);

        // Left: page name
        const char* pg = (_pages_len > 0 && _pages[_page_index] != nullptr)
                         ? _pages[_page_index]->name() : "";
        write_chars(" ", 1);
        write_chars(pg, (uint16_t)strlen(pg));
        write_chars(" ", 1);

        // Middle: spaces up to metrics start column
        uint16_t metrics_col = (_cols > (uint16_t)(mlen)) ? (uint16_t)(_cols - mlen) : 0;
        while (_hw_pos.col < metrics_col)
            write_chars(" ", 1);

        // Right: metrics (truncate if terminal is very narrow)
        uint16_t remaining = (_cols > _hw_pos.col) ? (_cols - _hw_pos.col) : 0;
        uint16_t write_len = (uint16_t)std::min((int)remaining, mlen);
        if (write_len > 0) write_chars(metrics, write_len);

        // Restore state
        set_inverted(saved_inv);
        move_to(saved_pos);
    }

    int8_t AnsiTerminal::add_page(Pages::Page* page) {
        if (page == nullptr || _pages_len >= 8) return -1;
        _pages[_pages_len] = page;
        return (int8_t)(_pages_len++);
    }

    void AnsiTerminal::switch_to(uint8_t index) {
        if (index >= _pages_len || _pages[index] == nullptr) return;
        if (_pages_len > 0 && _pages[_page_index] != nullptr)
            _pages[_page_index]->deactivate();
        _page_index = index;
        clear_screen();
        _pages[_page_index]->focus();
        draw_status_row();
        if (_logger != nullptr) {
            using namespace PlcIO;
            LogEntry_t entry = {};
            entry.timestamp = HAL::current_ms();
            entry.level     = LOG_INFO;
            entry.topic     = STATUS;
            entry.length    = (uint16_t)snprintf(entry.message, sizeof(entry.message),
                "page %u", (unsigned)index);
            _logger->push_entry(entry);
        }
    }

}}