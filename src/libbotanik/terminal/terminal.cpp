/**
 * @file terminal.cpp
 * @brief AnsiTerminal implementation — cursor/geometry tracking, text output,
 *        content manipulation, page routing, and status-bar rendering.
 */
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

#include "libbotanik_config.h"

#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/serial.h"
#include "libbotanik/terminal/parser.h"
#include "libbotanik/terminal/terminal.h"
#include "libbotanik/terminal/ansi.h"
#include "libbotanik/logs/logger.h"
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
        uint64_t now = HAL::uptime_ms();
        if (now - _status_last_ms >= Config::STATUS_REFRESH_MS) {
            // Only refresh when the TX buffer can absorb the pass. When it can't,
            // leave _status_last_ms untouched so the next step() retries rather than
            // waiting out another full STATUS_REFRESH_MS interval.
            if (tx_available() >= STATUS_METRICS_TX_BUDGET) {
                _status_last_ms = now;
                update_status_metrics();
            }
        }
        while (true) {
            InputEvent& ev = _parser.parse();
            if (ev.type == InputEvent::EventType::NONE) break;
            if (_pages_len == 0 || _pages[_page_index] == nullptr) continue;
            Pages::Page* page = _pages[_page_index];
            if (ev.type == InputEvent::EventType::CONTENT) {
                page->handle_text_batch(ev.batch_data, ev.batch_len);
                break;
            } else if (ev.type == InputEvent::EventType::COMMAND) {
                // Terminal-level intercepts — consumed before reaching the page
                if (ev.command == Command::CTRL_L) {
                    clear_screen();
                    draw_status_row();
                    page->redraw();
                    break;
                }
                if (ev.command == Command::HOME)   { switch_to(0);  continue; }
                if (ev.command >= Command::ALT_1 && ev.command <= Command::ALT_9) {
                    switch_to((uint8_t)((uint16_t)ev.command - (uint16_t)Command::ALT_1));
                    break;
                }
                if (ev.command == Command::ALT_0) { switch_to(9); continue; }
                page->handle_command(ev.command, ev.count);
                break;
            }
        }
        // Tick every page, not just the active one: each page is its own
        // LogWriter and must drain its queue into its own scrollback on every
        // step so a later focus() replays an up-to-date tail. Inactive pages keep
        // their hands off the screen (gated internally on their active flag); only
        // the active page renders.
        for (uint8_t i = 0; i < _pages_len; i++)
            if (_pages[i] != nullptr) _pages[i]->tick(i == _page_index);
    }


    // Move cursor to an absolute screen position
    void AnsiTerminal::move_to(const CursorPos& target_in, bool skip) {
        // The hardware caps the cursor at the last row/column when asked to
        // move past either edge, so any out-of-range target must be clamped
        // here to keep _hw_pos honest.
        CursorPos target = {
            (uint16_t)std::min((uint16_t)(_rows > 0 ? _rows - 1 : 0), target_in.row),
            (uint16_t)std::min((uint16_t)(_cols > 0 ? _cols - 1 : 0), target_in.col),
        };
        if (target == _hw_pos) {
            return;
        }
        _hw_at_eol = false;

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
            } else if (target.col == 0 && target.row > _hw_pos.row
                       && (target.row - _hw_pos.row) < 4
                       && target.row < (uint16_t)(_rows - 1)) {
                // Common "spawn next row" target — CR + LF(s) costs delta+1
                // bytes vs ~8 for goto_absolute. Bounded away from the last
                // row so a stray LF can't scroll the screen.
                _serial.write("\r", 1);
                for (uint16_t i = _hw_pos.row; i < target.row; i++)
                    _serial.write("\n", 1);
            } else { // Different row and column, use absolute move
                Ansi::goto_absolute(_serial, target);
            }
        }
        _hw_pos = target;
    }

    // Write `len` chars at cursor, advancing it right.
    void AnsiTerminal::write_chars(const char *s, uint16_t len, bool insert) {
        if (len == 0) return;

        // Lazy SM4/RM4 toggling — re-bracketing every call burns 8 bytes per
        // single-char middle-of-line insert (SM4 + char + RM4 = 9 bytes,
        // ~90 ms at 900 baud). Keep insert mode latched so consecutive
        // inserts cost just the char byte, and only emit RM4 the first time
        // a non-insert write happens.
        if (insert != _hw_insert_mode) {
            // Commit the latch only if the toggle actually reached the wire.
            // BufferedSerial drops bytes when full; latching past a dropped RM4
            // would later let a replace-mode write (e.g. the status row) paint in
            // insert mode and corrupt the line. Bail rather than emit the payload
            // in the wrong mode — the caller (status refresh / page) redraws.
            if (!Ansi::set_insert_mode(_serial, insert)) return;
            _hw_insert_mode = insert;
        }

        // Track only what reached the wire. write() drops the tail when the TX
        // buffer fills; advancing _hw_pos by the full len would then desync cursor
        // tracking from the real screen and smear every later move/write. The
        // dropped glyphs are content loss a redraw fixes — a wrong cursor isn't.
        uint16_t written = _serial.write(s, len);
        uint32_t new_col = (uint32_t)_hw_pos.col + written;
        if (new_col >= _cols) {
            if (_hw_auto_wrap) {
                _hw_pos.col = 0;
                _hw_pos.row++;
                if (_hw_pos.row >= _rows) _hw_pos.row = _rows - 1;
                _hw_at_eol = false;
            } else {
                _hw_pos.col = _cols - 1;
                // Cursor is parked at the last cell. The cell holds a char we
                // just wrote; a follow-up clear_to_eol would erase it.
                _hw_at_eol = true;
            }
        } else {
            _hw_pos.col = (uint16_t)new_col;
            _hw_at_eol = false;
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
        // When the cursor is parked at the last cell because a write just filled
        // the row, there is nothing past it to erase — and the parked cell holds
        // valid content we'd otherwise destroy.
        if (_hw_at_eol) return;
        Ansi::clear_to_eol(_serial);
        // Cursor position is typically not affected by EL
    }

    // Erase an entire row
    void AnsiTerminal::clear_row(uint16_t row_to_clear) {
        CursorPos original_pos = _hw_pos;
        bool      original_eol = _hw_at_eol;
        move_to({row_to_clear, 0});
        _serial.write(Ansi::CSI, sizeof(Ansi::CSI) - 1);
        _serial.write("2K", 2);
        move_to(original_pos);
        // clear_row doesn't touch the caller's row, so the at-eol fill state
        // (if any) on that row still holds.
        _hw_at_eol = original_eol;
    }

    // Emit a line feed
    void AnsiTerminal::line_feed(bool carriage_return) {
        if (_hw_pos.row < _rows - 1) {
            // Normal case: cursor moves into the next row
            if (carriage_return) {
                _serial.write("\r\n", 2);
                _hw_pos.col = 0;
            } else {
                _serial.write("\n", 1);
            }
            _hw_pos.row++;
        } else {
            // At the last row: scroll only the content area (rows 1..N-1) so that
            // row 0 (status bar) is never touched by a hardware scroll.
            // DL 1 at row 1 pulls rows 1..N-1 upward and inserts a blank at row N-1.
            uint16_t target_col = carriage_return ? 0 : _hw_pos.col;
            move_to({1, 0});
            Ansi::delete_rows(_serial, 1);
            // Cursor stays at {1, 0} after DL; move to the new blank last row.
            _hw_pos = {1, 0};
            move_to({(uint16_t)(_rows - 1), target_col});
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
        _hw_at_eol = false;
        // Auto-wrap stays off for the whole UI: every page pre-wraps its own
        // content to the width, so the terminal must never wrap on its own. A
        // deferred wrap on a full-width line parks the cursor pending at the last
        // cell — the next clear_to_eol then eats that glyph and the following row
        // drops its first char. Assert it once, when the screen is (re)established;
        // ED/clear doesn't reset DECAWM, so the mode persists until the next reset.
        if (_hw_auto_wrap) set_auto_wrap(false);
    }

    void AnsiTerminal::clear_content() {
        // Erase the page content area (rows 1..N-1), leaving the status bar on
        // row 0 untouched. One erase-to-end-of-display from {1,0} is far cheaper
        // than clearing each row in a loop.
        move_to({1, 0});
        Ansi::clear_to_eos(_serial);
        _hw_at_eol = false;
    }

    uint16_t AnsiTerminal::tx_available() const {
        return _serial.availableForWrite();
    }

    bool AnsiTerminal::tx_empty() const {
        return _serial.tx_empty();
    }

    int AnsiTerminal::_build_metrics(char* buf, size_t cap) const {
        if (_plc_ctx == nullptr) return 0;
        const char* plc_str;
        switch (_plc_ctx->status()) {
            case PlcIO::PLC_UNKNOWN:  plc_str = "UNK"; break;
            case PlcIO::PLC_RUNNING:  plc_str = "RUN"; break;
            case PlcIO::PLC_TIMEOUT:  plc_str = "TMO"; break;
            case PlcIO::PLC_FAULT:    plc_str = "FLT"; break;
            default:                  plc_str = "---"; break;
        }
        unsigned cpu = (unsigned)lroundf(_sched_ctx->perf.cpu_load_ema    * 100.0f);
        unsigned stk = (unsigned)lroundf(_sched_ctx->perf.stack_usage_ema * 100.0f);
        if (cpu > 9999) cpu = 9999;
        if (stk > 999)  stk = 999;
        return snprintf(buf, cap, "PLC:%s cpu:%4u%% stk:%3u%% ", plc_str, cpu, stk);
    }

    void AnsiTerminal::draw_status_row() {
        _status_bar_drawn = true;
        // Build right-side metrics string (fixed-width field, see STATUS_METRICS_LEN)
        char metrics[STATUS_METRICS_LEN];
        int mlen = _build_metrics(metrics, sizeof(metrics));

        // Keep the metrics cache in sync so update_status_metrics() won't re-draw immediately.
        memcpy(_status_metrics_cache, metrics, (size_t)mlen);

        // Save state
        CursorPos saved_pos = _hw_pos;
        bool      saved_inv = _hw_inverted;

        // Draw full inverted row 0. Insert/replace mode is owned by write_chars
        // (insert defaults to false here), so the status row always overwrites.
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
        // 31 spaces + trailing NUL. The NUL must never reach the wire: terminals
        // drop it without advancing the cursor, but write_chars bumps _hw_pos by
        // the full length anyway, desyncing cursor tracking by one column.
        static const char spaces[32] = "                               ";
        constexpr uint16_t SPACES_LEN = sizeof(spaces) - 1;
        while (_hw_pos.col < metrics_col) {
            uint16_t chunk = std::min((uint16_t)(metrics_col - _hw_pos.col), SPACES_LEN);
            write_chars(spaces, chunk);
        }

        // Right: metrics (truncate if terminal is very narrow)
        uint16_t remaining = (_cols > _hw_pos.col) ? (_cols - _hw_pos.col) : 0;
        uint16_t write_len = (uint16_t)std::min((int)remaining, mlen);
        if (write_len > 0) write_chars(metrics, write_len);

        // Restore state
        set_inverted(saved_inv);
        move_to(saved_pos);
    }

    void AnsiTerminal::update_status_metrics() {
        char metrics[STATUS_METRICS_LEN];
        int mlen = _build_metrics(metrics, sizeof(metrics));

        // mlen is constant across calls (all fields are fixed-width), so a byte
        // index in metrics maps to a fixed screen column.
        uint16_t visible = (uint16_t)std::min((int)_cols, mlen);
        if (visible == 0) return;
        uint16_t metrics_col = (uint16_t)(_cols - visible);

        CursorPos saved_pos = _hw_pos;
        bool      saved_inv = _hw_inverted;
        bool      touched   = false;

        int i = 0;
        while (i < visible) {
            if (metrics[i] == _status_metrics_cache[i]) { ++i; continue; }
            int j = i + 1;
            while (j < visible && metrics[j] != _status_metrics_cache[j]) ++j;
            if (!touched) { set_inverted(true); touched = true; }
            move_to({0, (uint16_t)(metrics_col + i)});
            write_chars(metrics + i, (uint16_t)(j - i));
            memcpy(_status_metrics_cache + i, metrics + i, (size_t)(j - i));
            i = j;
        }

        if (touched) {
            set_inverted(saved_inv);
            move_to(saved_pos);
        }
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
        if (!_status_bar_drawn) {
            // Initial display: full clear + draw the status bar once.
            clear_screen();
            draw_status_row();
        } else {
            // Page change: keep the status bar (row 0); only the content area is
            // cleared. The status bar is redrawn on a full redraw (^L), not here.
            clear_content();
        }
        _pages[_page_index]->focus();
        if (_logger != nullptr)
            _logger->info(PlcIO::STATUS, "page %u", (unsigned)index);
    }

}}