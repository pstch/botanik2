#include <algorithm>
#include <cstring>
#include <cstdio>

#include "libbotanik_config.h"
#include "libbotanik/logs/format.h"
#include "libbotanik/pages/shell.h"

namespace Botanik { namespace Pages {

    ShellPage::ShellPage(Terminal::AnsiTerminal& terminal) :
        _terminal(terminal),
        _editor(&terminal),
        _shell(terminal, _editor, *this)
    {}

    size_t ShellPage::format(const LogEntry_t* entry, char* buf, size_t len) {
        return Logs::format_log_entry(entry, buf, len);
    }

    void ShellPage::_store_line(const char* buf, size_t len) {
        uint16_t store_len = (uint16_t)std::min(len, (size_t)(Config::SHELL_SCROLLBACK_LINE_LEN));
        memcpy(_log_buf[_log_head], buf, store_len);
        _log_lens[_log_head] = store_len;
        _log_head = (uint16_t)((_log_head + 1) % Config::SHELL_SCROLLBACK_LINES);
        if (_log_count < Config::SHELL_SCROLLBACK_LINES) _log_count++;
    }

    void ShellPage::_redraw_log() {
        uint16_t log_rows = (uint16_t)(_terminal.rows() - 1 - _editor.occupied_rows()); // rows 1..rows-occ-1
        for (uint16_t r = 1; r <= log_rows; r++)
            _terminal.clear_row(r);
        _redraw_i = UINT16_MAX; // cancel any pending drain
        if (_log_count == 0) return;

        _redraw_count     = (_log_count < log_rows) ? _log_count : log_rows;
        _redraw_start     = (uint16_t)((_log_head + Config::SHELL_SCROLLBACK_LINES - _redraw_count) % Config::SHELL_SCROLLBACK_LINES);
        _redraw_first_row = (uint16_t)(log_rows - _redraw_count + 1);
        _redraw_i         = 0;
    }

    void ShellPage::_drain_redraw_row() {
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

    size_t ShellPage::write(const char* buf, size_t len) {
        // Throttle if the TX buffer is getting full
        if (_terminal.tx_available() < 64) return 0;

        _store_line(buf, len);

        uint16_t rows     = (uint16_t)_terminal.rows();
        uint16_t occupied = _editor.occupied_rows();
        // The log area ends at the row just above the editor (which may be >1 row).
        uint16_t last_log_row = (uint16_t)(rows - 1 - occupied);

        // Clear all editor rows before DL so their content doesn't corrupt the log.
        for (uint16_t r = (uint16_t)(rows - occupied); r < rows; r++)
            _terminal.clear_row(r);

        // Scroll log area: DL at row 1 shifts rows [1..N-1] up by one.
        // Row 0 (header) is unaffected; cleared editor rows pull blank rows in.
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

    void ShellPage::_setup_editor() {
        _terminal.set_auto_wrap(false);
        uint16_t editor_row = (uint16_t)(_terminal.rows() - 1);
        _editor.set_origin(editor_row);
        const char ps1[] = "botanik> ";
        _editor.set_ps1(ps1, sizeof(ps1) - 1);
        const char ps2[] = "...> ";
        _editor.set_ps2(ps2, sizeof(ps2) - 1);
    }

    void ShellPage::_full_redraw() {
        _setup_editor();
        // Pull origin up so a multi-row prompt redraws without triggering a scroll
        // that would push the freshly-drawn status bar (row 0) off screen.
        uint16_t occ = _editor.occupied_rows();
        if (occ > 1) {
            uint16_t rows   = (uint16_t)_terminal.rows();
            uint16_t origin = (uint16_t)(rows - occ);
            _editor.set_origin(origin > 0 ? origin : 1);
        }
        _redraw_log();
        _editor.redraw(true);
    }

    void ShellPage::focus()  { _full_redraw(); }
    void ShellPage::redraw() { _full_redraw(); }

    void ShellPage::tick() {
        _drain_redraw_row();
        // Hold off processing incoming log entries until the historical redraw
        // is complete — a DL scroll from write() would shift rows already drawn
        // and corrupt the display.
        if (_redraw_i != UINT16_MAX) return;
        drain();
    }

    void ShellPage::_echo_command(const char* cmd, uint16_t cmd_len, uint16_t old_occ) {
        uint16_t cols    = (uint16_t)_terminal.cols();
        uint16_t rows    = (uint16_t)_terminal.rows();
        // Rows [rows-old_occ .. rows-2] are blank donated rows (cleared by reset()).
        // Writing directly into them avoids the DL that write() would issue, which
        // would otherwise scroll those blank rows up as ghost blank lines.
        uint16_t donated = old_occ > 0 ? old_occ - 1 : 0;
        char     echo[Config::SHELL_SCROLLBACK_LINE_LEN];
        uint16_t pos = 0;
        for (uint16_t row = 0; ; row++) {
            bool        first = (row == 0);
            const char* pstr  = first ? _editor.ps1()     : _editor.ps2();
            uint16_t    plen  = first ? _editor.ps1_len() : _editor.ps2_len();
            uint16_t    cap   = cols - plen;
            uint16_t    rlen  = 0;
            while (rlen < cap && pos + rlen < cmd_len && cmd[pos + rlen] != '\n') rlen++;
            uint16_t elen = std::min((uint16_t)(plen + rlen), (uint16_t)sizeof(echo));
            memcpy(echo, pstr, std::min(plen, elen));
            if (plen < elen) memcpy(echo + plen, cmd + pos, elen - plen);
            if (row < donated) {
                // Direct write into donated blank row — no DL.
                _store_line(echo, elen);
                uint16_t target = (uint16_t)(rows - old_occ + row);
                uint16_t wlen   = std::min(elen, (uint16_t)_terminal.cols());
                auto saved = _terminal.cursor_pos();
                _terminal.move_to({target, 0});
                _terminal.write_chars(echo, wlen);
                _terminal.clear_to_eol();
                _terminal.move_to(saved);
            } else {
                write(echo, elen);
            }
            bool hard = (pos + rlen < cmd_len) && (cmd[pos + rlen] == '\n');
            pos += rlen + (hard ? 1 : 0);
            if (pos >= cmd_len) break;
        }
    }

    void ShellPage::clear() {
        _log_head  = 0;
        _log_count = 0;
        _redraw_i  = UINT16_MAX;
    }

    void ShellPage::deactivate() {}

    void ShellPage::handle_text(char c) {
        _editor.insert_characters(&c, 1);
    }

    void ShellPage::handle_command(Command cmd) {
        switch (cmd) {
            // Submit / cancel
            case Command::ENTER: {
                char cmd[Config::SHELL_INPUT_LEN];
                uint16_t cmd_len = std::min(_editor.buffer_len(), (uint16_t)(sizeof(cmd) - 1));
                memcpy(cmd, _editor.buffer(), cmd_len);
                cmd[cmd_len] = '\0';
                uint16_t old_occ = _editor.occupied_rows();
                _editor.reset();
                _echo_command(cmd, cmd_len, old_occ);
                if (cmd_len > 0) _shell.run(cmd, cmd_len);
                break;
            }
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
