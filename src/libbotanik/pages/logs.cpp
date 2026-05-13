#include <algorithm>
#include <cstring>

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
        // Throttle if the TX buffer is getting full to avoid losing bytes
        if (_terminal.tx_available() < 32) return 0;

        // Scroll the log area: DL at row 1 shifts rows [1..rows-1] up by one.
        // Row 0 (header) is above the cursor and is unaffected.
        _terminal.move_to({1, 0});
        _terminal.delete_rows(1);

        // Write new entry at the last row
        uint16_t last_row  = (uint16_t)(_terminal.rows() - 1);
        uint16_t write_len = (uint16_t)std::min(len, (size_t)_terminal.cols());
        _terminal.move_to({last_row, 0});
        _terminal.write_chars(buf, write_len);
        _terminal.clear_to_eol();

        return len;
    }

    void LogsPage::focus() {
        _terminal.set_auto_wrap(true);
    }

    void LogsPage::tick() {
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
            case Command::PAGE_UP:   break;             // TODO: scrollback
            case Command::PAGE_DOWN: break;             // TODO: scrollback
            default: break;
        }
    }

}}
