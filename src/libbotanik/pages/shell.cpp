#include <algorithm>
#include <cstring>
#include <cstdio>

#include "libbotanik/logs/format.h"
#include "libbotanik/pages/shell.h"

namespace Botanik { namespace Pages {

    ShellPage::ShellPage(Terminal::AnsiTerminal& terminal) :
        _terminal(terminal),
        _editor(&terminal),
        _shell(terminal, _editor, *this)
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
            // Submit / cancel
            case Command::ENTER:      _shell.submit();              break;
            case Command::CTRL_C:
            case Command::CTRL_U:     _editor.reset();              break;

            // Deletion
            case Command::BACKSPACE:  _editor.backspace();          break;
            case Command::DELETE:
            case Command::CTRL_D:     _editor.delete_forward();     break;

            // Kill / yank — stubs (kill ring not yet implemented)
            case Command::CTRL_K:     /* TODO: kill ring */          break;
            case Command::CTRL_W:     /* TODO: kill ring */          break;
            case Command::CTRL_Y:     /* TODO: kill ring */          break;

            // Cursor movement
            case Command::LEFT:
            case Command::CTRL_B:     _editor.move_left();          break;
            case Command::RIGHT:
            case Command::CTRL_F:     _editor.move_right();         break;
            case Command::CTRL_A:     _editor.move_to_line_start(); break;
            case Command::END:
            case Command::CTRL_E:     _editor.move_to_line_end();   break;
            case Command::ALT_B:      _editor.move_word_left();     break;
            case Command::ALT_F:      _editor.move_word_right();    break;

            // History — stubs (not yet implemented)
            case Command::UP:
            case Command::CTRL_P:     _shell.history_prev();        break;
            case Command::DOWN:
            case Command::CTRL_N:     _shell.history_next();        break;

            // Selection / mark
            case Command::CTRL_SPACE: _editor.set_mark_toggle();    break;
            case Command::ESC:
            case Command::CTRL_G:     _editor.deselect();           break;

            // Stubs
            case Command::CTRL_R:     /* TODO: history search */     break;
            case Command::TAB:        /* TODO: completion */          break;

            default: break;
        }
    }

}}
