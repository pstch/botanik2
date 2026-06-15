/**
 * @file logs.cpp
 * @brief LogsPage implementation — entry formatting, draining queued entries
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
        _scrollback.begin_redraw((uint16_t)(_terminal.rows() - 1)); // rows 1..rows-1
    }

    void LogsPage::tick(bool active) {
        _active = active;
        if (active) {
            _scrollback.drain_redraw(_terminal);
            // Hold off processing incoming log entries until the historical redraw
            // is complete — a DL scroll from write() would shift rows already drawn
            // and corrupt the display.
            if (_scrollback.redraw_pending()) return;
        }
        // Drain the queue either way: active paints the new tail, background just
        // banks the entries into the scrollback (see write()).
        drain();
    }

    // BufferedLogWriter interface
    // ------------------------------------------------------------------------

    size_t LogsPage::format(const LogEntry_t* entry, char* buf, size_t len) {
        return Logs::format_log_entry(entry, buf, len);
    }

    size_t LogsPage::write(const char* buf, size_t len) {
        // In the background: just keep the scrollback current so a later focus()
        // replays an up-to-date tail. Nothing reaches the screen, so there's no TX
        // back-pressure to honour — store the whole entry and report it consumed.
        if (!_active) {
            _scrollback.store_wrapped(buf, len, (uint16_t)_terminal.cols(),
                Config::SHELL_LOG_CONT_PREFIX,
                (uint16_t)strlen(Config::SHELL_LOG_CONT_PREFIX));
            return len;
        }

        // Back-pressure: only emit a line once the TX buffer has drained, so a
        // burst of entries can't fill it. Reporting no progress makes the
        // BufferedLogWriter hold the entry and retry on a later tick.
        if (!_terminal.tx_empty()) return 0;

        _emit_line(buf, len);
        return len;
    }

    // Internals
    // ------------------------------------------------------------------------

    void LogsPage::_emit_line(const char* buf, size_t len) {
        // Wrap into one or more stored rows (indented log-prefixed continuations),
        // then let the scrollback scroll the tail up and paint them at the bottom.
        // The log area is rows 1..rows-1 (the whole content area).
        uint16_t n = _scrollback.store_wrapped(buf, len, (uint16_t)_terminal.cols(),
            Config::SHELL_LOG_CONT_PREFIX,
            (uint16_t)strlen(Config::SHELL_LOG_CONT_PREFIX));
        _scrollback.emit_tail(_terminal, n, 1, (uint16_t)(_terminal.rows() - 1));
    }

    void LogsPage::_emit_mark() {
        // Don't disturb the tail while a page-switch redraw is still filling it,
        // and respect the same TX back-pressure as incoming log entries.
        if (_scrollback.redraw_pending() || !_terminal.tx_empty()) return;

        // Stamp the marker with the current time using the same format as log lines.
        char time_buf[Utils::TIMESTAMP_LEN];
        Utils::format_ms_timestamp(HAL::uptime_ms(), time_buf, sizeof(time_buf));
        char mark[40];
        int written = snprintf(mark, sizeof(mark), "%s --- MARK ---", time_buf);
        if (written < 0) return;
        _emit_line(mark, std::min((size_t)written, sizeof(mark) - 1));
    }

}}
