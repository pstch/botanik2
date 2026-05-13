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

    void LogsPage::_store_line(const char* buf, size_t len) {
        uint16_t store_len = (uint16_t)std::min(len, (size_t)(Config::SHELL_SCROLLBACK_LINE_LEN));
        memcpy(_log_buf[_log_head], buf, store_len);
        _log_lens[_log_head] = store_len;
        _log_head = (uint16_t)((_log_head + 1) % Config::SHELL_SCROLLBACK_LINES);
        if (_log_count < Config::SHELL_SCROLLBACK_LINES) _log_count++;
    }

    void LogsPage::_redraw_log() {
        uint16_t log_rows = (uint16_t)(_terminal.rows() - 1); // rows 1..rows-1
        for (uint16_t r = 1; r <= log_rows; r++)
            _terminal.clear_row(r);
        _redraw_i = UINT16_MAX; // cancel any pending drain
        if (_log_count == 0) return;

        _redraw_count     = (_log_count < log_rows) ? _log_count : log_rows;
        _redraw_start     = (uint16_t)((_log_head + Config::SHELL_SCROLLBACK_LINES - _redraw_count) % Config::SHELL_SCROLLBACK_LINES);
        _redraw_first_row = (uint16_t)(log_rows - _redraw_count + 1);
        _redraw_i         = 0;
    }

    void LogsPage::_drain_redraw_row() {
        if (_redraw_i == UINT16_MAX) return;
        if (_terminal.tx_available() < Config::SHELL_SCROLLBACK_LINE_LEN + 16) return;
        auto saved = _terminal.cursor_pos();
        uint16_t idx = (uint16_t)((_redraw_start + _redraw_i) % Config::SHELL_SCROLLBACK_LINES);
        _terminal.move_to({(uint16_t)(_redraw_first_row + _redraw_i), 0});
        uint16_t write_len = (uint16_t)std::min((size_t)_log_lens[idx], (size_t)_terminal.cols());
        _terminal.write_chars(_log_buf[idx], write_len);
        _terminal.clear_to_eol();
        _terminal.move_to(saved);
        if (++_redraw_i >= _redraw_count) _redraw_i = UINT16_MAX;
    }

    size_t LogsPage::write(const char* buf, size_t len) {
        _store_line(buf, len);

        uint16_t last_row  = (uint16_t)(_terminal.rows() - 1);
        uint16_t write_len = (uint16_t)std::min(len, (size_t)_terminal.cols());

        _terminal.move_to({1, 0});
        _terminal.delete_rows(1);
        _terminal.move_to({last_row, 0});
        _terminal.write_chars(buf, write_len);
        _terminal.clear_to_eol();

        return len;
    }

    void LogsPage::focus() {
        _terminal.set_auto_wrap(true);
        _redraw_log();
    }

    void LogsPage::tick() {
        _drain_redraw_row();
        // Hold off processing incoming log entries until the historical redraw
        // is complete — a DL scroll from write() would shift rows already drawn
        // and corrupt the display.
        if (_redraw_i != UINT16_MAX) return;
        drain();
    }

    void LogsPage::deactivate() {}

    void LogsPage::handle_text(char /* c */) {}

    void LogsPage::handle_command(Command cmd) {
        switch (cmd) {
            case Command::ENTER:
                _terminal.clear_screen();
                focus();
                _terminal.draw_status_row();
                break;
            case Command::PAGE_UP:   break;
            case Command::PAGE_DOWN: break;
            default: break;
        }
    }

}}
