// REVIEW_35: TODO
/**
 * @file logs.cpp
 * @brief LogsPage implementation.
 */
#include "libbotanik/pages/logs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "libbotanik/config/base.h"

#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/format.h"

namespace Botanik { namespace Pages {

    // Lifecycle

    LogsPage::LogsPage(Terminal::Console& console) :
        Logs::BufferedLogWriter(nullptr, "console_logs_level"),
        _console(console), _terminal(console.terminal())
    {
        // This page IS the record, so it takes whatever the logger forwarded -
        // DEBUG included. Raising the master floor (`sys_log_level`) still
        // quiets it.
        set_min_level((Logs::LogLevel)Config::LOGS_LOG_MIN_LEVEL);
    }

    // Page interface

    void LogsPage::handle_text(char c) {
        if (c == ' ') _emit_mark();   // SPACE: annotate the stream w/ a marker
    }

    void LogsPage::handle_command(Command cmd, uint16_t /*count*/) {
        // Scroll history a line at a time (live tail freezes while scrolled
        // back); ENTER clears and returns to the live bottom.
        const uint16_t first_row = 1;
        const uint16_t log_rows  = (uint16_t)(_console.rows() - 1);
        switch (cmd) {
            case Command::ENTER:
                _console.clear_content();
                focus();
                break;
            // Shift+arrow too: the shell's bare arrows are its command
            // history, so the line-at-a-time key is the shifted one wherever a
            // scrollback is on screen.
            case Command::UP:
            case Command::SHIFT_UP:
            case Command::CTRL_P:
                _scrollback.scroll(_terminal, /*up=*/true, 1, first_row, log_rows);
                break;
            case Command::DOWN:
            case Command::SHIFT_DOWN:
            case Command::CTRL_N:
                _scrollback.scroll(_terminal, /*up=*/false, 1, first_row, log_rows);
                break;
            // A screenful less one row of overlap, the way a pager steps.
            case Command::PAGE_UP:
            case Command::PAGE_DOWN: {
                uint16_t step = (log_rows > 1) ? (uint16_t)(log_rows - 1) : 1;
                _scrollback.scroll(_terminal, cmd == Command::PAGE_UP, step,
                                   first_row, log_rows);
                break;
            }
            default: break;
        }
    }

    void LogsPage::deactivate() {}

    void LogsPage::focus() {
        // Park the cursor at the top of the log area; the redraw replay
        // captures it as its resting place and restores it once the historical
        // tail has drained.
        _terminal.move_to({1, 0});
        // rows 1..rows-1
        _scrollback.begin_redraw((uint16_t)(_console.rows() - 1));
    }

    void LogsPage::tick(bool active) {
        _active = active;
        // Staged focus replay first, and bail until it finishes: the live
        // tail's DL scroll must not shift rows the replay already drew.
        if (_scrollback.tick_redraw_gate(_terminal, active)) return;
        // Bank queued entries, then paint them paced into rows 1..rows-1.
        drain();
        if (active) {
            _scrollback.drain_tail(_terminal, 1, (uint16_t)(_console.rows() - 1));
            // Keep the "scrolled up" marker in sync with the view (no-op unless
            // it moved); running every tick survives unrelated repaints.
            _scrollback.refresh_scroll_indicator(_terminal, 1,
                                                 (uint16_t)(_console.rows() - 1));
        }
    }

    // BufferedLogWriter interface

    size_t LogsPage::write(const char* buf, size_t len) {
        // Store-only: bank the entry, mark pending when active for the paced
        // drain_tail. Never blocks or drops, so report it all consumed.
        _scrollback.ingest(buf, len, (uint16_t)_terminal.cols(),
            Logs::LOG_CONT_PREFIX,
            (uint16_t)strlen(Logs::LOG_CONT_PREFIX), _active);
        return len;
    }

    // Internals

    void LogsPage::_emit_mark() {
        // Don't disturb the tail while a page-switch replay is still filling it.
        if (_scrollback.redraw_pending()) return;

        // Stamp the marker like a log line, then store it as any entry.
        char time_buf[Utils::TIMESTAMP_LEN];
        Utils::format_ms_timestamp(HAL::Clock::uptime_ms(), time_buf,
                                   sizeof(time_buf));
        char mark[40];
        int written = snprintf(mark, sizeof(mark), "%s --- MARK ---", time_buf);
        if (written < 0) return;
        write(mark, std::min((size_t)written, sizeof(mark) - 1));
    }

}}
