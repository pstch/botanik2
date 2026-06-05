#include <algorithm>
#include <cstring>
#include <cstdio>

#include "libbotanik_config.h"
#include "libbotanik/logs/format.h"
#include "libbotanik/pages/logs.h"

namespace Botanik { namespace Pages {

    LogsPage::LogsPage(Terminal::AnsiTerminal& terminal) :
        _terminal(terminal)
    {}

    size_t LogsPage::format(const LogEntry_t* entry, char* buf, size_t len) {
        return Logs::format_log_entry(entry, buf, len);
    }

    size_t LogsPage::write(const char* buf, size_t len) {
        // Back-pressure: only emit a line once the TX buffer has drained, so a
        // burst of entries can't fill it. Reporting no progress makes the
        // BufferedLogWriter hold the entry and retry on a later tick.
        if (!_terminal.tx_empty()) return 0;

        _emit_line(buf, len);
        return len;
    }

    void LogsPage::_emit_line(const char* buf, size_t len) {
        _scrollback.store(buf, len);

        uint16_t last_row  = (uint16_t)(_terminal.rows() - 1);
        uint16_t write_len = (uint16_t)std::min(len, (size_t)_terminal.cols());

        _terminal.move_to({1, 0});
        _terminal.delete_rows(1);
        _terminal.move_to({last_row, 0});
        _terminal.write_chars(buf, write_len);
        _terminal.clear_to_eol();
    }

    void LogsPage::focus() {
        _terminal.set_auto_wrap(true);
        _scrollback.begin_redraw((uint16_t)(_terminal.rows() - 1)); // rows 1..rows-1
    }

    void LogsPage::tick() {
        _scrollback.drain_redraw(_terminal);
        // Hold off processing incoming log entries until the historical redraw
        // is complete — a DL scroll from write() would shift rows already drawn
        // and corrupt the display.
        if (_scrollback.redraw_pending()) return;
        drain();
    }

    void LogsPage::deactivate() {}

    void LogsPage::_emit_mark() {
        // Don't disturb the tail while a page-switch redraw is still filling it,
        // and respect the same TX back-pressure as incoming log entries.
        if (_scrollback.redraw_pending() || !_terminal.tx_empty()) return;

        // Stamp the marker with the current time using the same format as log lines.
        std::array<char, 22> time_buf;
        Utils::format_ms_timestamp(HAL::uptime_ms(), &time_buf);
        char mark[40];
        int written = snprintf(mark, sizeof(mark), "%s --- MARK ---", time_buf.data());
        if (written < 0) return;
        _emit_line(mark, std::min((size_t)written, sizeof(mark) - 1));
    }

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

}}
