#ifndef PAGES_NOTES_H
#define PAGES_NOTES_H

/**
 * @file notes.h
 * @brief NotesPage - a full-screen text editor over the operator's note,
 *        persisted to flash under its own storage key.
 */

#include <cstdint>                          // uint16_t
#include <cstdio>                           // snprintf
#include <cstring>                          // strlen

#include "libbotanik_config.h"              // Config::NOTES_*
#include "libbotanik_features.h"            // BOTANIK_FEATURE_NOTES_PAGE

#if BOTANIK_FEATURE_NOTES_PAGE

#include "libbotanik/console/console.h"     // Console::rows / terminal
#include "libbotanik/console/editor.h"      // Terminal::TextEditor / EditorBuffers
#include "libbotanik/console/terminal.h"    // AnsiTerminal
#include "libbotanik/hal/storage.h"         // HAL::Storage::get / set
#include "libbotanik/logs/logger.h"         // Logs::Logger
#include "libbotanik/pages/page.h"          // Pages::Page

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
        /** @name Row gutter
         *  The editor draws one of these at the start of every row, which is
         *  what makes a wrapped line tell itself apart from a new one: a line
         *  the operator started is marked, its overflow is not. Same width
         *  either way, or the wrap column would move between rows.
         *  @{ */
        static const char* PROMPT_LINE() { return "> "; }   /**< PS1 / PS3 */
        static const char* PROMPT_WRAP() { return "  "; }   /**< PS2 */
        static const uint16_t PROMPT_COLS = 2;
        /** @} */

        /** @brief HAL::Storage key the note is persisted under. Its own key
         *  rather than a parameter: prose has no band to validate, no Modbus
         *  register and no business on the PARAMS page. Private to this page -
         *  renaming it would orphan the note already on the unit. */
        static constexpr const char* STORAGE_KEY = "notes";

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
                case Command::ENTER:
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
            _settle();
        }

        /** @brief Ctrl+C: consume it, so it cannot quit the simulator out from
         *  under an unsaved note. */
        bool handle_cancel() override {
            _settle();
            return true;
        }

        /** @} */

        /** @name Focus & update
         *  @{ */

        void focus() override {
            uint16_t rows = _console.rows();
            if (rows < 3) return;                 // no room for text and a status row

            _editor.set_anchor(Terminal::Anchor::TOP);
            _editor.set_origin(1);                // row 0 is the status bar
            _editor.set_view_rows((uint16_t)(rows - 2));  // last row is ours
            _editor.set_ps1(PROMPT_LINE(), PROMPT_COLS);  // first row of the note
            _editor.set_ps2(PROMPT_WRAP(), PROMPT_COLS);  // a soft-wrapped tail
            _editor.set_ps3(PROMPT_LINE(), PROMPT_COLS);  // a row the operator started

            if (!_loaded) { _loaded = true; _load(); }

            // The status row is a standing property of that row, not something
            // raised around a paint (see ScrollTablePage::_claim_mode_row).
            _terminal.set_row_priority((uint16_t)(rows - 1), (uint16_t)(rows - 1),
                                       Terminal::PRIO_CHROME);
            _editor.redraw();                     // the page switch cleared the band
            _terminal.set_cursor_visible(true);
            _settle();
        }

        void deactivate() override {
            // Leaving with unsaved edits would lose them at the next reset, and
            // nothing else is going to ask.
            if (_editor.dirty()) _save();
            _terminal.set_cursor_visible(false);
        }

        void tick(bool active) override {
            if (!active) return;
            _settle();
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

        /** @brief Read the stored note into the editor, straight into its own
         *  buffer - staging it through a second one would double what the whole
         *  feature costs in RAM. */
        void _load() {
            size_t n = 0;
            if (!HAL::Storage::get(STORAGE_KEY, _editor.load_buffer(),
                                   _editor.load_capacity(), &n))
                n = 0;                            // no note yet: start empty
            _truncated = !_editor.commit_loaded((uint16_t)n);
        }

        /** @brief Write the note, and remember if flash refused it. */
        void _save() {
            bool ok = HAL::Storage::set(STORAGE_KEY,
                                        _editor.buffer(), _editor.buffer_len());
            _save_failed = !ok;
            if (ok) {
                _editor.clear_dirty();
            } else if (_logger != nullptr) {
                _logger->critical(Logs::STORAGE, "notes not saved: storage refused %u bytes",
                                  (unsigned)_editor.buffer_len());
            }
        }

        /** @brief Bring the status row up to date and park the cursor back in
         *  the text. What every input path and every tick ends with. */
        void _settle() {
            _draw_status();
            _editor.place_cursor();
        }

        /** @brief The status row: the bottom content row, claimed as CHROME in
         *  focus() so a squeezed byte budget drops it before the text.
         *
         *  Painted unconditionally. The terminal's back-buffer already prices an
         *  unchanged row at zero bytes, so a page-side "what did I draw" cache
         *  would buy nothing and cost an invalidation to get wrong. */
        void _draw_status() {
            uint16_t rows = _console.rows();
            if (rows < 3) return;
            uint16_t row  = (uint16_t)(rows - 1);

            char line[Config::TERMINAL_MAX_COLS];
            const char* state = _save_failed ? "NOT SAVED"
                              : _editor.dirty() ? "modified" : "saved";
            snprintf(line, sizeof(line), " %u/%u bytes  %s%s  ^O save",
                     (unsigned)_editor.buffer_len(), (unsigned)(Config::NOTES_LEN - 1),
                     state, _truncated ? "  (truncated on load)" : "");

            _terminal.move_to({row, 0});
            _terminal.set_inverted(true);
            _terminal.write_chars(line, (uint16_t)strlen(line));
            _terminal.fill_spaces(_terminal.cols());
            _terminal.set_inverted(false);
        }

        /** @} */
    };

}}

#endif // BOTANIK_FEATURE_NOTES_PAGE
#endif // PAGES_NOTES_H
