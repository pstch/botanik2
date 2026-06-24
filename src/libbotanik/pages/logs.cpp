/**
 * @file logs.cpp
 * @brief LogsPage implementation.
 */
#include <algorithm>
#include <cstring>
#include <cstdio>

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/format.h"
#include "libbotanik/pages/logs.h"

namespace Botanik { namespace Pages {

    // Lifecycle
    // ------------------------------------------------------------------------

    LogsPage::LogsPage(Terminal::AnsiTerminal& terminal) :
        _terminal(terminal)
    {}

    // Page interface
    // ------------------------------------------------------------------------

    void LogsPage::handle_text(char c) {
        if (c == ' ') _emit_mark();   // SPACE: annotate the stream w/ a marker
    }

    void LogsPage::handle_command(Command cmd, uint16_t /*count*/) {
        // Scroll history a line at a time (live tail freezes while scrolled
        // back); ENTER clears and returns to the live bottom.
        const uint16_t first_row = 1;
        const uint16_t log_rows  = (uint16_t)(_terminal.rows() - 1);
        switch (cmd) {
            case Command::ENTER:
                _terminal.clear_content();
                focus();
                break;
            case Command::UP:
            case Command::CTRL_P:
                _scrollback.scroll(_terminal, /*up=*/true, 1, first_row, log_rows);
                break;
            case Command::DOWN:
            case Command::CTRL_N:
                _scrollback.scroll(_terminal, /*up=*/false, 1, first_row, log_rows);
                break;
            case Command::PAGE_UP:   break;
            case Command::PAGE_DOWN: break;
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
        _scrollback.begin_redraw((uint16_t)(_terminal.rows() - 1));
    }

    void LogsPage::tick(bool active) {
        _active = active;
        if (active) {
            _scrollback.drain_redraw(_terminal);
            // Hold off the live tail until the replay completes - drain_tail's
            // DL scroll would shift rows the replay already drew.
            if (_scrollback.redraw_pending()) return;
        }
        // Bank queued entries (pending when active), then paint them paced into
        // the content area (rows 1..rows-1).
        drain();
        if (active)
            _scrollback.drain_tail(_terminal, 1, (uint16_t)(_terminal.rows() - 1));
    }

    // BufferedLogWriter interface
    // ------------------------------------------------------------------------

    size_t LogsPage::format(const LogEntry_t* entry, char* buf, size_t len) {
        return Logs::format_log_entry(entry, buf, len);
    }

    size_t LogsPage::write(const char* buf, size_t len) {
        // Store-only: bank the entry, mark pending when active for the paced
        // drain_tail. Never blocks/drops, so always report the whole entry
        // consumed.
        _scrollback.ingest(buf, len, (uint16_t)_terminal.cols(),
            Config::SHELL_LOG_CONT_PREFIX,
            (uint16_t)strlen(Config::SHELL_LOG_CONT_PREFIX), _active);
        return len;
    }

    // Internals
    // ------------------------------------------------------------------------

    void LogsPage::_emit_mark() {
        // Don't disturb the tail while a page-switch replay is still filling
        // it.
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
