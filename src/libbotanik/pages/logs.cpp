/**
 * @file logs.cpp
 * @brief LogsPage implementation.
 */
#include "libbotanik/utils/minmax.h"
#include "libbotanik/pages/logs.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_LOGS_PAGE

#include <cstdio>
#include <cstring>

#include "libbotanik/config/base.h"

#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/format.h"

namespace Botanik { namespace Pages {

    // Lifecycle

    LogsPage::LogsPage(Terminal::Console& console) :
        Logs::BufferedLogWriter(_slots, (uint8_t)Config::LOG_QUEUE_LOGS,
                                nullptr, "logs_log_level"),
        _console(console), _terminal(console.terminal())
    {
        // This page IS the record, so it takes whatever the logger forwarded -
        // DEBUG included. Raising `logs_log_level` still
        // quiets it.
        set_min_level(Logs::LOG_DEBUG);
    }

    // Page interface

    void LogsPage::handle_text(char c) {
        if (c == ' ') _emit_mark();   // SPACE: annotate the stream w/ a marker
    }

    void LogsPage::handle_command(Command cmd, uint16_t /*count*/) {
        // Scroll history a line at a time (live tail freezes while scrolled
        // back); ENTER returns to the live bottom.
        const uint16_t log_rows = (uint16_t)(_console.rows() - 1);
        switch (cmd) {
            case Command::ENTER:
                focus();
                break;
            // Shift+arrow too: the shell's bare arrows are its command
            // history, so the line-at-a-time key is the shifted one wherever a
            // scrollback is on screen.
            case Command::UP:
            case Command::SHIFT_UP:
            case Command::CTRL_P:
                _scrollback.scroll(/*up=*/true, 1, log_rows);
                break;
            case Command::DOWN:
            case Command::SHIFT_DOWN:
            case Command::CTRL_N:
                _scrollback.scroll(/*up=*/false, 1, log_rows);
                break;
            // A screenful less one row of overlap, the way a pager steps.
            case Command::PAGE_UP:
            case Command::PAGE_DOWN: {
                uint16_t step = (log_rows > 1) ? (uint16_t)(log_rows - 1) : 1;
                _scrollback.scroll(cmd == Command::PAGE_UP, step, log_rows);
                break;
            }
            default: break;
        }
    }

    void LogsPage::deactivate() {}

    void LogsPage::focus() {
        _scrollback.reset_view();
    }

    void LogsPage::tick(bool active) {
        _active = active;
        drain();
        if (!active) return;
        _scrollback.paint(_terminal, 1, (uint16_t)(_console.rows() - 1));
    }

    // BufferedLogWriter interface

    size_t LogsPage::write(const char* buf, size_t len) {
        // Store-only: the next paint shows it. Never blocks or drops, so report
        // it all consumed.
        _scrollback.ingest(buf, len, (uint16_t)_terminal.cols(),
            Logs::LOG_CONT_PREFIX,
            (uint16_t)strlen(Logs::LOG_CONT_PREFIX), _active);
        return len;
    }

    // Internals

    void LogsPage::_emit_mark() {
        // Stamp the marker like a log line, then store it as any entry.
        char time_buf[Utils::TIMESTAMP_LEN];
        Utils::format_ms_timestamp(HAL::Clock::uptime_ms(), time_buf,
                                   sizeof(time_buf));
        char mark[40];
        int written = snprintf(mark, sizeof(mark), "%s --- MARK ---", time_buf);
        if (written < 0) return;
        write(mark, Utils::min((size_t)written, sizeof(mark) - 1));
    }

}}

#endif // BOTANIK_FEATURE_LOGS_PAGE
