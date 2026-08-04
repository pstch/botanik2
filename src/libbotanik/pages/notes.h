#ifndef PAGES_NOTES_H
#define PAGES_NOTES_H

/**
 * @file notes.h
 * @brief NotesPage - a full-screen text editor over the operator's note,
 *        persisted to flash under its own storage key.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../libbotanik_config.h"
#include "../../libbotanik_features.h"

#if BOTANIK_FEATURE_NOTES

#include "page.h"
#include "../console/console.h"
#include "../console/editor.h"
#include "../console/terminal.h"
#include "../hal/storage.h"
#include "../logs/logger.h"

namespace Botanik { namespace Pages {

    /**
     * @brief Free-text notes the operator keeps on the panel.
     *
     * A `TextEditor` in Anchor::TOP over the content area, minus the bottom row
     * which holds a status line. The editor owns the wrapping and the
     * reconcile, so editing in the middle of a paragraph reflows the rows below
     * with one IL/DL rather than repainting the screen - the whole reason this
     * is the shell's editor and not a second one.
     *
     * Persistence is deliberate, not continuous: Ctrl+O writes, and leaving the
     * page flushes if there is anything unsaved. A note is kilobytes, so each
     * write is a real flash erase; doing it per keystroke would be an overrun
     * per keystroke.
     */
    class NotesPage : public Page {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using TextEditor   = Terminal::TextEditor;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Owning console (content-area geometry). */
        Console&      _console;
        /** @brief The VT device it paints through (cached _console.terminal()). */
        AnsiTerminal& _terminal;
        /** @brief Where the logger reports a refused save. May be null. */
        Logs::Logger* _logger;

        /** @brief Buffers the editor borrows. Declared before it. */
        Terminal::EditorBuffers<Config::NOTES_LEN,
                                Config::NOTES_MAX_ROWS> _bufs;
        /** @brief The editor itself. */
        TextEditor    _editor;

        /** @brief Whether the stored note has been read in yet. Deferred to the
         *  first focus: bring-up should not pay a flash read for a page nobody
         *  may open. */
        bool _loaded = false;
        /** @brief Set when the last load() had to drop a tail that did not fit,
         *  so the status line can say so rather than silently showing less than
         *  is stored. */
        bool _truncated = false;
        /** @brief Set when the last save was refused by storage. */
        bool _save_failed = false;
        /** @brief What the status line last showed, so an unchanged one costs
         *  nothing to leave alone. */
        char _status_shown[Config::TERMINAL_MAX_COLS] = {0};

        /** @} */

    public:
        /** @brief @p console owns the content geometry; @p logger (optional)
         *  hears about a save that did not reach flash. */
        explicit NotesPage(Console& console, Logs::Logger* logger = nullptr)
            : _console(console), _terminal(console.terminal()), _logger(logger),
              _editor(&console, _bufs.storage()) {}

        const char* name() const override { return "NOTES"; }

        /** @name Input
         *  @{ */

        void handle_text(char c) override { _editor.insert_characters(&c, 1); }

        void handle_text_batch(const char* buf, uint16_t len) override {
            _editor.insert_characters(buf, len);
        }

        void handle_command(Command cmd, uint16_t count = 1) override {
            // TAB, HOME and CTRL_L never arrive: the console takes them for page
            // switching and redraw before a page is offered the event.
            switch (cmd) {
                case Command::ENTER:       _editor.insert_characters("\n", 1); break;
                case Command::SHIFT_ENTER: _editor.insert_characters("\n", 1); break;

                case Command::BACKSPACE:   _editor.backspace(count);           break;
                case Command::DELETE:
                case Command::CTRL_D:      _editor.delete_forward(count);      break;

                case Command::LEFT:
                case Command::CTRL_B:      _editor.move_left();                break;
                case Command::RIGHT:
                case Command::CTRL_F:      _editor.move_right();               break;

                // Cursor motion, not history: that is a CommandShell notion and
                // this page has no shell under it.
                case Command::UP:
                case Command::CTRL_P:      _editor.move_up();                  break;
                case Command::DOWN:
                case Command::CTRL_N:      _editor.move_down();                break;

                case Command::PAGE_UP:     _editor.page_up();                  break;
                case Command::PAGE_DOWN:   _editor.page_down();                break;

                case Command::CTRL_A:      _editor.move_to_line_start();       break;
                case Command::END:
                case Command::CTRL_E:      _editor.move_to_line_end();         break;

                case Command::ALT_B:       _editor.move_word_left();           break;
                case Command::ALT_F:       _editor.move_word_right();          break;

                case Command::CTRL_O:      _save();                            break;

                // Deliberately unbound: CTRL_U clears the buffer in the shell,
                // which on a page whose whole content is the buffer is a
                // keystroke away from losing the lot.
                default: break;
            }
            _draw_status();
            _editor.place_cursor();
        }

        /** @brief Ctrl+C: consume it, so it cannot quit the simulator out from
         *  under an unsaved note. */
        bool handle_cancel() override {
            _draw_status();
            _editor.place_cursor();
            return true;
        }

        /** @} */

        /** @name Focus & update
         *  @{ */

        void focus() override {
            uint16_t rows = _console.rows();
            if (rows < 3) return;                 // no room for text and a status row

            static const char NO_PROMPT[] = "";
            _editor.set_anchor(Terminal::Anchor::TOP);
            _editor.set_origin(1);                // row 0 is the status bar
            _editor.set_view_rows((uint16_t)(rows - 2));  // last row is ours
            _editor.set_ps1(NO_PROMPT, 0);
            _editor.set_ps2(NO_PROMPT, 0);
            _editor.set_ps3(NO_PROMPT, 0);

            if (!_loaded) { _load(); _loaded = true; }

            // load() repaints; a re-focus after a page switch needs the same.
            _editor.redraw();
            _status_shown[0] = '\0';              // the switch cleared the row
            _draw_status();
            _terminal.set_cursor_visible(true);
            _editor.place_cursor();
        }

        void deactivate() override {
            // Leaving with unsaved edits would lose them at the next reset, and
            // nothing else is going to ask.
            if (_editor.dirty()) _save();
            _terminal.set_cursor_visible(false);
        }

        void tick(bool active) override {
            if (!active) return;
            _draw_status();
            _editor.place_cursor();
        }

        /** @} */

        /** @name Content
         *  @{ */

        /** @brief The note as it stands, for a test or a shell command. */
        const char* text() const     { return _editor.buffer(); }
        uint16_t    text_len() const { return _editor.buffer_len(); }

        /** @} */

    private:
        /** @name Helpers
         *  @{ */

        /** @brief Read the stored note into the editor. */
        void _load() {
            static char buf[Config::NOTES_LEN];
            size_t n = 0;
            if (!HAL::Storage::get(Config::NOTES_STORAGE_KEY, buf, sizeof(buf), &n))
                n = 0;                            // no note yet: start empty
            _truncated = !_editor.load(buf, (uint16_t)n);
        }

        /** @brief Write the note, and remember if flash refused it. */
        void _save() {
            bool ok = HAL::Storage::set(Config::NOTES_STORAGE_KEY,
                                        _editor.buffer(), _editor.buffer_len());
            _save_failed = !ok;
            if (ok) {
                _editor.clear_dirty();
            } else if (_logger != nullptr) {
                _logger->critical(Logs::STORAGE, "notes not saved: storage refused %u bytes",
                                  (unsigned)_editor.buffer_len());
            }
        }

        /** @brief The status row: the bottom content row, CHROME rather than
         *  INPUT so a squeezed byte budget drops it before the text. */
        void _draw_status() {
            uint16_t rows = _console.rows();
            if (rows < 3) return;
            uint16_t row  = (uint16_t)(rows - 1);
            uint16_t cols = _terminal.cols();
            if (cols > Config::TERMINAL_MAX_COLS) cols = Config::TERMINAL_MAX_COLS;

            char line[Config::TERMINAL_MAX_COLS];
            const char* state = _save_failed ? "NOT SAVED"
                              : _editor.dirty() ? "modified" : "saved";
            snprintf(line, sizeof(line), " %u/%u bytes  %s%s  ^O save",
                     (unsigned)_editor.buffer_len(), (unsigned)(Config::NOTES_LEN - 1),
                     state, _truncated ? "  (truncated on load)" : "");

            if (strncmp(line, _status_shown, sizeof(_status_shown)) == 0) return;
            strncpy(_status_shown, line, sizeof(_status_shown) - 1);
            _status_shown[sizeof(_status_shown) - 1] = '\0';

            _terminal.set_row_priority(row, row, Terminal::PRIO_CHROME);
            _terminal.move_to({row, 0});
            _terminal.set_inverted(true);
            uint16_t n = (uint16_t)strlen(line);
            if (n > cols) n = cols;
            _terminal.write_chars(line, n);
            for (uint16_t c = n; c < cols; c++) _terminal.write_chars(" ", 1);
            _terminal.set_inverted(false);
        }

        /** @} */
    };

}}

#endif // BOTANIK_FEATURE_NOTES
#endif // PAGES_NOTES_H
