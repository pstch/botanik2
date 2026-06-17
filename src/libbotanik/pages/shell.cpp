/**
 * @file shell.cpp
 * @brief ShellPage implementation - log-area layout, scrolling on new entries,
 *        and in-place editor redraw.
 */
#include <algorithm>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/logs/format.h"
#include "libbotanik/utils/text.h"
#include "libbotanik/pages/shell.h"

namespace Botanik { namespace Pages {

    // Lifecycle
    // ------------------------------------------------------------------------

    ShellPage::ShellPage(Terminal::AnsiTerminal& terminal) :
        _terminal(terminal),
        _editor(&terminal),
        _shell(terminal, _editor, *this)
    {}

    // Page interface
    // ------------------------------------------------------------------------

    void ShellPage::handle_text(char c) {
        _editor.insert_characters(&c, 1);
    }

    void ShellPage::handle_text_batch(const char* buf, uint16_t len) {
        _editor.insert_characters(buf, len);
    }

    void ShellPage::handle_command(Command cmd, uint16_t count) {
        uint16_t occ_before = _editor.occupied_rows();
        _editor.suppress_redraw(true);
        switch (cmd) {
            // Submit. Screen is left correct by _submit_command(), so return early
            // and skip the shrink check below.
            case Command::ENTER:
                _submit_command();
                return;
            case Command::SHIFT_ENTER: {
                _editor.insert_characters("\n", 1);
                break;
            }

            // Cancel. Like submit, the screen is left correct by _cancel_command(),
            // so return early and skip the shrink check below.
            case Command::CTRL_C:
                _cancel_command();
                return;

            case Command::CTRL_U:     _editor.reset();              break;

            // Deletion - `count` may be >1 when the parser coalesced a
            // held key; pass through so the editor does one reconcile.
            case Command::BACKSPACE:  _editor.backspace(count);     break;
            case Command::DELETE:
            case Command::CTRL_D:     _editor.delete_forward(count); break;

            // Kill / yank - stubs (kill ring not yet implemented)
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

            // History - stubs (not yet implemented)
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
        _editor.suppress_redraw(false);
        uint16_t occ_after = _editor.occupied_rows();
        if (occ_after != occ_before && _scrollback.redraw_pending()) {
            // A staged replay is draining with now-stale geometry; re-arm it against
            // the new log area instead of revealing rows.
            _scrollback.begin_redraw((uint16_t)(_terminal.rows() - 1 - occ_after));
        } else if (occ_after < occ_before) {
            _reveal_log_rows(occ_before - occ_after);
        }
    }

    void ShellPage::deactivate() {}

    void ShellPage::focus()  { _full_redraw(); }
    void ShellPage::redraw() { _full_redraw(); }

    void ShellPage::tick(bool active) {
        _active = active;
        if (active) {
            _scrollback.drain_redraw(_terminal);
            // Hold off live entries until the historical redraw is complete - a DL
            // scroll from write() would shift rows already drawn and corrupt the log.
            if (_scrollback.redraw_pending()) return;
        }
        // Only the active page queues log entries (see push()), so this paints the
        // new tail; in the background the queue is empty and this is a no-op.
        drain();
    }

    // BufferedLogWriter + ShellWriter interface
    // ------------------------------------------------------------------------

    void ShellPage::push(const LogEntry_t* entry) {
        // The shell only shows logs received while it's the active page; drop
        // anything that arrives in the background. The full log lives in LogsPage.
        if (!_active) return;
        Logs::BufferedLogWriter::push(entry);
    }

    size_t ShellPage::format(const LogEntry_t* entry, char* buf, size_t len) {
        return Logs::format_log_entry(entry, buf, len);
    }

    size_t ShellPage::write(const char* buf, size_t len) {
        // Wrap into stored rows. Log output (via drain()) indents continuations to
        // align under the message; command output uses the plain "> " prefix.
        const char* prefix = _draining_log ? Config::SHELL_LOG_CONT_PREFIX
                                           : Config::SHELL_CONT_PREFIX;
        uint16_t    plen   = (uint16_t)strlen(prefix);
        uint16_t n = _scrollback.store_wrapped(buf, len, (uint16_t)_terminal.cols(),
                                               prefix, plen);

        // In the background: the entry is banked in the scrollback for a later
        // focus() replay; don't touch the screen the active page owns.
        if (!_active) return len;

        // Log area is rows 1..(rows-1-occupied), editor below. emit_tail scrolls the
        // log up and paints new lines, shifting the editor up (DL) and back (IL).
        uint16_t log_rows = (uint16_t)(_terminal.rows() - 1 - _editor.occupied_rows());
        _scrollback.emit_tail(_terminal, n, 1, log_rows);

        // Editor is back at its correct screen rows - just reposition the cursor.
        _terminal.move_to(_editor.cursor_screen_pos());

        return len;
    }

    void ShellPage::write_lines(const char* buf, uint16_t len) {
        uint16_t cols = (uint16_t)_terminal.cols();
        for (uint16_t off = 0; off < len; ) {
            uint16_t chunk = Utils::scan_line_segment(buf, off, len, cols);
            if (chunk > 0) write(buf + off, chunk);
            off += chunk;
            if (off < len && buf[off] == '\n') off++;
        }
    }

    void ShellPage::drain() {
        // Flag the log path so write() picks the indented continuation prefix;
        // command output goes through write() outside drain() and stays plain.
        _draining_log = true;
        Logs::BufferedLogWriter::drain();
        _draining_log = false;
    }

    void ShellPage::clear() {
        _scrollback.clear();
        // One ED from {1,0} wipes the whole content area (log rows + editor) in a
        // single CSI, far cheaper than clearing each log row. The editor row goes
        // with it, so repaint the (now empty) prompt back in place at the bottom.
        _terminal.clear_content();
        _editor.redraw();
    }

    // Internals
    // ------------------------------------------------------------------------

    void ShellPage::_setup_editor() {
        uint16_t editor_row = (uint16_t)(_terminal.rows() - 1);
        _editor.set_origin(editor_row);
        const char ps1[] = "botanik> ";
        _editor.set_ps1(ps1, sizeof(ps1) - 1);
        const char ps2[] = "> ";
        _editor.set_ps2(ps2, sizeof(ps2) - 1);
        const char ps3[] = "...> ";
        _editor.set_ps3(ps3, sizeof(ps3) - 1);
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
        // Stage the log area for incremental replay (drained in tick()), then draw
        // the editor in place at the bottom.
        _scrollback.begin_redraw((uint16_t)(_terminal.rows() - 1 - _editor.occupied_rows()));
        _editor.redraw();
        // The editor owns a live cursor - make it visible; switch_to() resets it off
        // before focus(), and every other page leaves it hidden.
        _terminal.set_cursor_visible(true);
    }

    void ShellPage::_submit_command() {
        // Snapshot the line first: it must outlive commit(), which clears the editor.
        char     line[Config::SHELL_INPUT_LEN];
        uint16_t line_len = std::min(_editor.buffer_len(), (uint16_t)(sizeof(line) - 1));
        memcpy(line, _editor.buffer(), line_len);
        line[line_len] = '\0';
        _record_command(line, line_len);
        _terminal.move_to(_editor.end_pos());
        _terminal.line_feed(true);
        _editor.suppress_redraw(false);
        _editor.commit();
        _shell.run(line, line_len);  // run() ignores an empty line
    }

    void ShellPage::_cancel_command() {
        // Ctrl-C: commit the visible prompt without running anything. Echo the
        // current line into the scrollback and drop down to a fresh prompt, same
        // as a submit but with no dispatch to the shell.
        char     line[Config::SHELL_INPUT_LEN];
        uint16_t line_len = std::min(_editor.buffer_len(), (uint16_t)(sizeof(line) - 1));
        memcpy(line, _editor.buffer(), line_len);
        line[line_len] = '\0';
        _record_command(line, line_len);
        _terminal.move_to(_editor.end_pos());
        _terminal.line_feed(true);
        _editor.suppress_redraw(false);
        _editor.commit();
    }

    void ShellPage::_record_command(const char* cmd, uint16_t cmd_len) {
        uint16_t cols = (uint16_t)_terminal.cols();
        char     echo[Config::SCROLLBACK_LINE_LEN];
        uint16_t pos = 0;
        for (uint16_t row = 0; ; row++) {
            bool        first = (row == 0);
            const char* pstr  = first ? _editor.ps1()     : _editor.ps2();
            uint16_t    plen  = first ? _editor.ps1_len() : _editor.ps2_len();
            uint16_t    cap   = cols - plen;
            uint16_t    rlen  = Utils::scan_line_segment(cmd, pos, cmd_len, cap);
            uint16_t elen = std::min((uint16_t)(plen + rlen), (uint16_t)sizeof(echo));
            memcpy(echo, pstr, std::min(plen, elen));
            if (plen < elen) memcpy(echo + plen, cmd + pos, elen - plen);
            _scrollback.store(echo, elen);
            bool hard = (pos + rlen < cmd_len) && (cmd[pos + rlen] == '\n');
            pos += rlen + (hard ? 1 : 0);
            if (pos >= cmd_len) break;
        }
    }

    void ShellPage::_reveal_log_rows(uint16_t delta) {
        if (delta == 0) return;
        uint16_t rows     = (uint16_t)_terminal.rows();
        uint16_t occupied = _editor.occupied_rows();
        uint16_t log_rows = rows - 1 - occupied;  // new (larger) log area
        if (log_rows == 0) return;

        // IL delta at row 1 shifts rows 1..rows-1 down by delta; the freed rows plus
        // the editor row fall off the bottom (the editor is redrawn after).
        _terminal.move_to({1, 0});
        _terminal.insert_rows(delta);

        // Fill newly blank rows 1..delta with older scrollback entries.
        // Before the shrink the log area had (log_rows - delta) rows visible.
        uint16_t prev_log     = log_rows - delta;
        uint16_t prev_visible = (uint16_t)std::min((size_t)prev_log, (size_t)_scrollback.count());
        for (uint16_t d = 0; d < delta; d++) {
            // Row (1 + d) gets the entry that is (prev_visible + delta - d) steps from newest.
            uint16_t from_end = prev_visible + delta - d;
            uint16_t llen     = 0;
            const char* line  = _scrollback.line(from_end, llen);
            if (line == nullptr) continue;
            _terminal.move_to({(uint16_t)(1 + d), 0});
            uint16_t wlen = std::min(llen, (uint16_t)_terminal.cols());
            _terminal.write_chars(line, wlen);
            _terminal.clear_to_eol();
        }

        // After IL, the editor's content (drawn at old_origin before the suppress)
        // has shifted into the correct new position - just reposition the cursor.
        _terminal.move_to(_editor.cursor_screen_pos());
    }

}}
