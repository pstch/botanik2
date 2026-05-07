#include <algorithm>
#include <cstring>
#include <cstdio>

#include "libbotanik/logs/format.h"
#include "libbotanik/pages/shell.h"

namespace Botanik { namespace Pages {

    ShellPage::ShellPage(Terminal::AnsiTerminal& terminal) :
        _terminal(terminal),
        _editor(&terminal)
    {}

    void ShellPage::_draw_header() {
        _terminal.move_to({0, 0});
        _terminal.set_inverted(true);
        const char header[] = " SHELL ";
        _terminal.write_chars(header, sizeof(header) - 1);
        _terminal.clear_to_eol();
        _terminal.set_inverted(false);
    }

    size_t ShellPage::format(const LogEntry_t* entry, char* buf, size_t len) {
        return Logs::format_log_entry(entry, buf, len);
    }

    size_t ShellPage::write(const char* buf, size_t len) {
        // Throttle if the TX buffer is getting full
        if (_terminal.tx_available() < 64) return 0;

        uint16_t last_log_row = (uint16_t)(_terminal.rows() - 3);
        uint16_t editor_row   = (uint16_t)(_terminal.rows() - 2);

        // Clear editor rows so DL doesn't displace live content into them
        _terminal.clear_row(editor_row);
        _terminal.clear_row(editor_row + 1);

        // Scroll log area: DL at row 1 shifts rows [1..N-1] up by one.
        // Row 0 (header) is unaffected; old editor rows (now blank) shift off.
        _terminal.move_to({1, 0});
        _terminal.delete_rows(1);

        // Write the new log entry at the last log row
        uint16_t write_len = (uint16_t)std::min(len, (size_t)_terminal.cols());
        _terminal.move_to({last_log_row, 0});
        _terminal.write_chars(buf, write_len);
        _terminal.clear_to_eol();

        // Redraw the editor at its fixed position
        _editor.redraw(false);

        return len;
    }

    void ShellPage::focus() {
        _terminal.set_auto_wrap(false);
        _terminal.clear_screen();
        _draw_header();

        uint16_t editor_row = (uint16_t)(_terminal.rows() - 2);
        _editor.set_origin(editor_row);

        const char ps1[] = "> ";
        _editor.set_ps1(ps1, sizeof(ps1) - 1);

        _editor.reset();
    }

    void ShellPage::tick() {
        drain();
    }

    void ShellPage::deactivate() {}

    void ShellPage::handle_text(char c) {
        _editor.insert_characters(&c, 1);
    }

    void ShellPage::handle_command(Command cmd) {
        switch (cmd) {
            case Command::ENTER:
                _submit_command();
                _editor.reset();
                break;
            case Command::BACKSPACE:
                _editor.backspace();
                break;
            case Command::CTRL_SPACE:
                _editor.set_mark_toggle();
                break;
            case Command::LEFT:
                _editor.move_left();
                break;
            case Command::RIGHT:
                _editor.move_right();
                break;
            default:
                break;
        }
    }

    void ShellPage::_submit_command() {
        const char* cmd = _editor.buffer();
        uint16_t    len = _editor.buffer_len();

        if (len == 0) return;

        char out[Config::LOG_FORMATTED_LEN];
        int  n = 0;

        if (len == 4 && strncmp(cmd, "help", 4) == 0) {
            n = snprintf(out, sizeof(out), "Commands: help, clear");
        } else if (len == 5 && strncmp(cmd, "clear", 5) == 0) {
            // Clear the log area
            for (uint16_t row = 1; row <= _terminal.rows() - 3; row++)
                _terminal.clear_row(row);
            return;
        } else {
            n = snprintf(out, sizeof(out), "Unknown command: %.*s", (int)len, cmd);
        }

        if (n > 0)
            write(out, (size_t)n);
    }

}}
