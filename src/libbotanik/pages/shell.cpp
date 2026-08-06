// REVIEW_35: TODO
/**
 * @file shell.cpp
 * @brief ShellPage implementation.
 */
#include "libbotanik/pages/shell.h" // ShellPage

#include <algorithm>                // std::min
#include <cstring>                  // memcpy / strlen

#include "libbotanik/config/terminal.h"   // Config::SCREEN_* / EDITOR_* / SHELL_* / PAGE_*

#include "libbotanik/logs/format.h" // Logs::format_log_entry
#include "libbotanik/utils/text.h"  // Utils::scan_line_segment

namespace Botanik { namespace Pages {

    // Lifecycle
    // ------------------------------------------------------------------------

    ShellPage::ShellPage(Terminal::Console& console, const SystemContext* sys) :
        _console(console),
        _terminal(console.terminal()),
        _editor(&console, _editor_bufs.storage()),
        _shell(console.terminal(), _editor, *this, sys)
    {
    }

    // Page interface
    // ------------------------------------------------------------------------

    void ShellPage::handle_text(char c) {
        _follow_tail();
        _editor.insert_characters(&c, 1);
    }

    void ShellPage::handle_text_batch(const char* buf, uint16_t len) {
        _follow_tail();
        _editor.insert_characters(buf, len);
    }

    bool ShellPage::handle_cancel() {
        _follow_tail();      // Ctrl-C echoes the line, so land at the bottom
        _cancel_command();   // discard the input line; screen left correct
        return true;
    }

    void ShellPage::handle_command(Command cmd, uint16_t count) {
        // The transcript's keys, not the editor's: they touch no editor state,
        // so they return before the suppress/occupied-rows bookkeeping below.
        // Shift+arrow goes a line at a time (plain UP/DOWN are the command
        // history here); a page keeps one row of overlap, the way a pager does,
        // and scroll() clamps a longer move to a screenful.
        if (cmd == Command::PAGE_UP   || cmd == Command::PAGE_DOWN ||
            cmd == Command::SHIFT_UP  || cmd == Command::SHIFT_DOWN) {
            const bool up   = (cmd == Command::PAGE_UP || cmd == Command::SHIFT_UP);
            const bool line = (cmd == Command::SHIFT_UP || cmd == Command::SHIFT_DOWN);
            uint16_t log_rows = _log_rows();
            uint16_t step     = 1;
            if (!line) step = (log_rows > 1) ? (uint16_t)(log_rows - 1) : 1;
            _scrollback.scroll(_terminal, up, step, 1, log_rows);
            return;
        }
        // Every other key returns to the live bottom first.
        _follow_tail();
        uint16_t occ_before = _editor.occupied_rows();
        _editor.suppress_redraw(true);
        switch (cmd) {
            // Submit leaves the screen correct; return past the shrink check.
            case Command::ENTER:
                _submit_command();
                return;
            case Command::SHIFT_ENTER: {
                _editor.insert_characters("\n", 1);
                break;
            }


            case Command::CTRL_U:     _editor.reset();              break;

            // Deletion - `count` may be >1 (parser coalesced a held key); pass
            // through for one editor reconcile.
            case Command::BACKSPACE:  _editor.backspace(count);     break;
            case Command::DELETE:
            case Command::CTRL_D:     _editor.delete_forward(count); break;

            // Kill / yank
            case Command::CTRL_K:     _shell.kill_to_line_end();    break;
            case Command::CTRL_W:     /* TODO: kill word back */     break;
            case Command::CTRL_Y:     _shell.paste_kill();          break;

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

            // Stubs
            case Command::CTRL_R:     /* TODO: history search */     break;
            case Command::TAB:        /* TODO: completion */          break;

            default: break;
        }
        _editor.suppress_redraw(false);
        uint16_t occ_after = _editor.occupied_rows();
        if (occ_after != occ_before && _scrollback.redraw_pending()) {
            // Staged replay now has stale geometry; re-arm it on the new area.
            _scrollback.begin_redraw(_log_rows());
        } else if (occ_after < occ_before) {
            _reveal_log_rows(occ_before - occ_after);
        }
    }

    void ShellPage::deactivate() {}

    void ShellPage::focus()  { _full_redraw(); }
    void ShellPage::redraw() { _full_redraw(); }

    void ShellPage::tick(bool active) {
        _active = active;
        // Drain any staged focus replay first; bail until it finishes so the live tail's
        // DL scroll can't shift rows the replay already drew.
        if (_scrollback.tick_redraw_gate(_terminal, active)) return;
        // Bank queued log entries (pending when active; only the active page queues, see
        // push()); command output banks the same way via write_lines().
        drain();
        // Paint pending lines paced into the log area; emit_tail shifts the
        // editor up (DL) and back down (IL), so reposition the cursor onto it
        // afterwards.
        if (active) {
            uint16_t log_rows = _log_rows();
            if (_scrollback.drain_tail(_terminal, 1, log_rows))
                _editor.place_cursor();
            // Keep the "scrolled up" marker in sync with the view (no-op unless
            // it moved); survives unrelated repaints since it runs every tick.
            _scrollback.refresh_scroll_indicator(_terminal, 1, log_rows);
        }
    }

    // BufferedLogWriter + ShellWriter interface
    // ------------------------------------------------------------------------

    void ShellPage::push(const Logs::LogEntry* entry) {
        // Only show logs received while active; background entries are dropped
        // (the full log lives in LogsPage). "logs off" drops them too.
        if (!_active || !_logs_enabled) return;
        Logs::BufferedLogWriter::push(entry);
    }

    size_t ShellPage::write(const char* buf, size_t len) {
        // Store-only, for both log output (via drain(), indented prefix) and
        // command output (via write_lines(), plain "> " prefix). Bank, mark
        // pending when active. Never blocks/returns short, so write_lines()
        // can't drop command output.
        const char* prefix = _draining_log ? Logs::LOG_CONT_PREFIX
                                           : Logs::CONT_PREFIX;
        uint16_t    plen   = (uint16_t)strlen(prefix);
        _scrollback.ingest(buf, len, (uint16_t)_terminal.cols(), prefix, plen,
                           _active);
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
        // One ED from {1,0} wipes the whole content area in a single CSI
        // (cheaper than clearing each row); the editor row goes too, so repaint
        // the prompt after.
        _console.clear_content();
        _editor.redraw();
    }

    // Internals
    // ------------------------------------------------------------------------

    void ShellPage::_setup_editor() {
        uint16_t editor_row = (uint16_t)(_console.rows() - 1);
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
        // Pull origin up so a multi-row prompt redraws without a scroll that
        // would push the status bar (row 0) off screen.
        uint16_t occ = _editor.occupied_rows();
        if (occ > 1) {
            uint16_t rows   = (uint16_t)_console.rows();
            uint16_t origin = (uint16_t)(rows - occ);
            _editor.set_origin(origin > 0 ? origin : 1);
        }
        // Stage the log area for incremental replay (drained in tick()), then
        // draw the editor at the bottom.
        _scrollback.begin_redraw(_log_rows());
        _editor.redraw();
        // The editor owns a live cursor; switch_to() hides it before focus()
        // and every other page leaves it hidden.
        _terminal.set_cursor_visible(true);
    }

    uint16_t ShellPage::_log_rows() const {
        uint16_t content = (uint16_t)(_console.rows() - 1);  // less row 0
        uint16_t occ     = _editor.occupied_rows();
        // A prompt taller than the screen leaves no transcript; every consumer
        // of this treats 0 as "nothing to paint".
        return (occ < content) ? (uint16_t)(content - occ) : 0;
    }

    void ShellPage::_follow_tail() {
        _scrollback.scroll_to_bottom(_terminal, 1, _log_rows());
    }

    uint16_t ShellPage::_echo_and_reset(char* line, uint16_t cap) {
        // Snapshot the line (it must outlive commit(), which clears the editor),
        // record it to history, and advance to a fresh prompt.
        uint16_t n = std::min(_editor.buffer_len(), (uint16_t)(cap - 1));
        memcpy(line, _editor.buffer(), n);
        line[n] = '\0';
        _record_command(line, n);
        _terminal.move_to(_editor.end_pos());
        _console.line_feed(true);
        _editor.suppress_redraw(false);
        _editor.commit();
        return n;
    }
    void ShellPage::_submit_command() {
        char line[Config::SHELL_INPUT_LEN];
        uint16_t n = _echo_and_reset(line, sizeof(line));
        _shell.run(line, n);         // run() ignores an empty line
    }
    void ShellPage::_cancel_command() {
        // Ctrl-C: echo and reset like a submit, but run nothing.
        char line[Config::SHELL_INPUT_LEN];
        _echo_and_reset(line, sizeof(line));
    }

    void ShellPage::_record_command(const char* cmd, uint16_t cmd_len) {
        uint16_t cols = (uint16_t)_terminal.cols();
        char     echo[Config::SCROLLBACK_LINE_LEN];
        uint16_t pos = 0;
        for (uint16_t row = 0; ; row++) {
            bool        first = (row == 0);
            const char* pstr  = first ? _editor.ps1()     : _editor.ps2();
            uint16_t    plen  = first ? _editor.ps1_len() : _editor.ps2_len();
            // >= 1 char/row so a prompt wider than the terminal can't underflow
            // the cap (and the loop always progresses).
            uint16_t    cap   = (cols > plen) ? (uint16_t)(cols - plen) : 1;
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
        uint16_t rows     = (uint16_t)_console.rows();
        uint16_t occupied = _editor.occupied_rows();
        uint16_t log_rows = rows - 1 - occupied;  // new (larger) log area
        if (log_rows == 0) return;

        // Scroll the content band down by delta: opens delta rows at the top
        // for older scrollback; the editor rows fall off the bottom (redrawn
        // after).
        // Bounded so row 0 stays put.
        _terminal.scroll_region(1, (uint16_t)(rows - 1), /*up=*/false, delta);

        // Fill newly blank rows 1..delta with older scrollback entries (the log
        // area had log_rows - delta rows visible before the shrink).
        uint16_t prev_log     = log_rows - delta;
        uint16_t prev_visible =
            (uint16_t)std::min((size_t)prev_log, (size_t)_scrollback.count());
        for (uint16_t d = 0; d < delta; d++) {
            // Row (1 + d) gets the entry (prev_visible + delta - d) steps from
            // newest.
            uint16_t from_end = prev_visible + delta - d;
            uint16_t llen     = 0;
            const char* line  = _scrollback.line(from_end, llen);
            if (line == nullptr) continue;
            _terminal.move_to({(uint16_t)(1 + d), 0});
            uint16_t wlen = std::min(llen, (uint16_t)_terminal.cols());
            _terminal.write_chars(line, wlen);
            _terminal.clear_to_eol();
        }

        // After the scroll the editor content has shifted into its new
        // position - just reposition the cursor.
        _editor.place_cursor();
    }

}}
