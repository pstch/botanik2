/**
 * @file shell.cpp
 * @brief ShellPage implementation.
 */
#include "libbotanik/utils/minmax.h"
#include "libbotanik/pages/shell.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_SHELL_PAGE

#include <cstring>

#include "libbotanik/config/terminal.gen.h"

#include "libbotanik/logs/format.h"
#include "libbotanik/utils/text.h"

namespace Botanik { namespace Pages {

    // Lifecycle

    ShellPage::ShellPage(Terminal::Console& console, const SystemContext* sys) :
        Logs::BufferedLogWriter(_slots, (uint8_t)Config::LOG_QUEUE_SHELL,
                                nullptr, "shell_log_level"),
        _console(console),
        _terminal(console.terminal()),
        _editor(&console, _editor_bufs.storage()),
        _shell(console.terminal(), _editor, *this, sys)
    {
        // You are working here, so keep the running commentary (INFO and up)
        // but not the one-shot DEBUG boot lines.
        set_min_level(Logs::LOG_INFO);
    }

    // Page interface

    void ShellPage::handle_text(char c) {
        handle_text_batch(&c, 1);
    }

    void ShellPage::handle_text_batch(const char* buf, uint16_t len) {
        _scrollback.reset_view();
        _editor.insert_characters(buf, len);
    }

    bool ShellPage::handle_cancel() {
        _scrollback.reset_view();   // Ctrl-C echoes the line, so land at the bottom
        _cancel_command();
        return true;
    }

    void ShellPage::handle_command(Command cmd, uint16_t count) {
        // The transcript's keys, not the editor's: they touch no editor state,
        // so they return before the bookkeeping below. Shift+arrow steps a line
        // (plain UP/DOWN are the command history here); a page keeps one row of
        // overlap, and scroll() clamps a longer move to a screenful.
        if (cmd == Command::PAGE_UP   || cmd == Command::PAGE_DOWN ||
            cmd == Command::SHIFT_UP  || cmd == Command::SHIFT_DOWN) {
            const Direction towards =
                (cmd == Command::PAGE_UP || cmd == Command::SHIFT_UP)
                    ? Direction::OLDER : Direction::NEWER;
            const bool line = (cmd == Command::SHIFT_UP || cmd == Command::SHIFT_DOWN);
            uint16_t log_rows = _log_rows();
            uint16_t step     = 1;
            if (!line) step = (log_rows > 1) ? (uint16_t)(log_rows - 1) : 1;
            _scrollback.scroll(towards, step, log_rows);
            return;
        }
        // Every other key returns to the live bottom first.
        _scrollback.reset_view();
        switch (cmd) {
            case Command::ENTER:       _submit_command();                  break;
            case Command::SHIFT_ENTER: _editor.insert_characters("\n", 1); break;
            case Command::CTRL_U:      _editor.reset();                    break;

            // History, where the editor would move between rows.
            case Command::UP:
            case Command::CTRL_P:      _shell.history_prev();              break;
            case Command::DOWN:
            case Command::CTRL_N:      _shell.history_next();              break;

            case Command::CTRL_R:      /* TODO: history search */           break;
            case Command::TAB:         /* TODO: completion */               break;

            // `count` may be >1 (the parser coalesced a held key): one reconcile.
            default:                   (void)_editor.handle_key(cmd, count); break;
        }
    }

    void ShellPage::deactivate() {}

    void ShellPage::focus()  { _full_redraw(); }
    void ShellPage::redraw() { _full_redraw(); }

    void ShellPage::tick(bool active) {
        _active = active;
        // Bank queued log entries (only the active page queues, see push());
        // command output banks the same way via write_lines().
        drain();
        if (!active) return;
        _scrollback.reveal_step();      // once per tick; paint() does not
        _scrollback.paint(_terminal, _console.content_top(), _log_rows());
    }

    // BufferedLogWriter + ShellWriter interface

    void ShellPage::push(const Logs::LogEntry* entry) {
        // Only show logs received while active; background entries are dropped
        // (the full log lives in LogsPage).
        if (!_active) return;
        // "logs off" silences the running COMMENTARY, not an emergency: a
        // critical clears every other floor in the system and this is a floor
        // like the rest. Load-bearing for anything that says something once and
        // does not repeat it - a restored override, a store that refused a
        // write - which an operator with the interleave off would never see.
        if (!_logs_enabled &&
            (entry == nullptr || entry->level != Logs::LOG_CRITICAL)) return;
        Logs::BufferedLogWriter::push(entry);
    }

    size_t ShellPage::write(const char* buf, size_t len) {
        // Store-only, for both log output (via drain(), indented prefix) and
        // command output (via write_lines(), plain "> " prefix). Never blocks
        // or returns short, so write_lines() cannot drop command output.
        const char* prefix = _draining_log ? Logs::LOG_CONT_PREFIX
                                           : Logs::CONT_PREFIX;
        uint16_t    plen   = (uint16_t)strlen(prefix);
        _scrollback.ingest(buf, len, (uint16_t)_terminal.cols(), prefix, plen,
                           _active ? Reveal::PACED : Reveal::AT_ONCE);
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
        // The editor row goes too, so repaint the prompt after.
        _console.clear_content();
        _editor.redraw();
    }

    // Internals

    void ShellPage::_setup_editor() {
        _editor.set_prompts("botanik> ", "> ", "...> ");
        _editor.as_prompt(_console.last_row());
    }

    void ShellPage::_full_redraw() {
        _setup_editor();
        _scrollback.reset_view();
        _editor.redraw();
        // The editor owns a live cursor; switch_to() hides it before focus()
        // and every other page leaves it hidden.
        _terminal.set_cursor_visible(true);
    }

    uint16_t ShellPage::_log_rows() const {
        uint16_t content = _console.content_rows();
        uint16_t occ     = _editor.occupied_rows();
        // A prompt taller than the screen leaves no transcript; every consumer
        // of this treats 0 as "nothing to paint".
        return (occ < content) ? (uint16_t)(content - occ) : 0;
    }

    uint16_t ShellPage::_echo_and_reset(char* line, uint16_t cap) {
        // Snapshot the line (it must outlive commit(), which clears the editor),
        // record it to history, and advance to a fresh prompt.
        uint16_t n = Utils::min(_editor.buffer_len(), (uint16_t)(cap - 1));
        memcpy(line, _editor.buffer(), n);
        line[n] = '\0';
        _record_command();
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

    void ShellPage::_record_command() {
        // The echo is the editor's rows as they stand, prompts included, so
        // the transcript can carry them along rather than write them again.
        char echo[Config::SCROLLBACK_LINE_LEN];
        const uint16_t rows = _editor.occupied_rows();
        for (uint16_t i = 0; i < rows; i++)
            _scrollback.store(echo, _editor.row_text(i, echo, sizeof echo));
        _scrollback.adopt(rows);
    }

}}

#endif // BOTANIK_FEATURE_SHELL_PAGE
