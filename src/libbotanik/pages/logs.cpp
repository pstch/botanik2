/**
 * @file logs.cpp
 * @brief LogsPage implementation - entry formatting, draining queued entries
 *        into scrollback, and replay on focus.
 */
#include <algorithm>
#include <cstring>
#include <cstdio>

#include "libbotanik_config.h"
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
        // SPACE inserts a manual marker into the log tail, handy for annotating
        // the stream before triggering an action.
        if (c == ' ') _emit_mark();
    }

    void LogsPage::handle_command(Command cmd, uint16_t /*count*/) {
        switch (cmd) {
            case Command::ENTER:
                _terminal.clear_content();
                focus();
                break;
            case Command::PAGE_UP:   break;
            case Command::PAGE_DOWN: break;
            default: break;
        }
    }

    void LogsPage::deactivate() {}

    void LogsPage::focus() {
        // Park the cursor at the top of the log area so it's ready for the next
        // log line. The redraw replay captures this as its resting place and
        // restores it once the historical tail has drained.
        _terminal.move_to({1, 0});
        _scrollback.begin_redraw((uint16_t)(_terminal.rows() - 1)); // rows 1..rows-1
    }

    void LogsPage::tick(bool active) {
        _active = active;
        if (active) {
            _scrollback.drain_redraw(_terminal);
            // Hold off the live tail until the historical replay is complete - a DL
            // scroll from drain_tail() would shift rows the replay already drew.
            if (_scrollback.redraw_pending()) return;
        }
        // Move queued entries into the scrollback (banks them, and marks them pending
        // when active). Then paint the pending lines, paced, into the whole content
        // area (rows 1..rows-1). In the background drain_tail is skipped.
        drain();
        if (active) _scrollback.drain_tail(_terminal, 1, (uint16_t)(_terminal.rows() - 1));
    }

    // BufferedLogWriter interface
    // ------------------------------------------------------------------------

    size_t LogsPage::format(const LogEntry_t* entry, char* buf, size_t len) {
        return Logs::format_log_entry(entry, buf, len);
    }

    size_t LogsPage::write(const char* buf, size_t len) {
        // Store-only: bank every entry into the scrollback (the live-tail buffer) and,
        // when active, mark the new row(s) pending so tick()'s drain_tail paints them
        // paced. In the background nothing reaches the screen; a later focus() replays
        // the tail. Never blocks and never drops, so always report the whole entry
        // consumed (this is also why command/log output can't be truncated).
        uint16_t n = _scrollback.store_wrapped(buf, len, (uint16_t)_terminal.cols(),
            Config::SHELL_LOG_CONT_PREFIX,
            (uint16_t)strlen(Config::SHELL_LOG_CONT_PREFIX));
        if (_active) _scrollback.note_pending(n);
        return len;
    }

    // Internals
    // ------------------------------------------------------------------------

    void LogsPage::_emit_mark() {
        // Don't disturb the tail while a page-switch replay is still filling it.
        if (_scrollback.redraw_pending()) return;

        // Stamp the marker with the current time using the same format as log lines,
        // then store it like any entry; tick()'s drain_tail paints it on a later tick.
        char time_buf[Utils::TIMESTAMP_LEN];
        Utils::format_ms_timestamp(HAL::uptime_ms(), time_buf, sizeof(time_buf));
        char mark[40];
        int written = snprintf(mark, sizeof(mark), "%s --- MARK ---", time_buf);
        if (written < 0) return;
        write(mark, std::min((size_t)written, sizeof(mark) - 1));
    }

}}
