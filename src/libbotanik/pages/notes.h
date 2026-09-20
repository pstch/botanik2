#ifndef PAGES_NOTES_H
#define PAGES_NOTES_H

/**
 * @file notes.h
 * @brief NotesPage - a full-screen text editor over the operator's note,
 *        persisted to flash under its own storage key.
 */

#include <cstdint>

#include "libbotanik/config/simulator.gen.h"
#include "libbotanik/config/notes.gen.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_NOTES_PAGE

#include "libbotanik/console/console.h"
#include "libbotanik/console/editor.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/hal/storage.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/console/page.h"

namespace Botanik { namespace Pages {

#ifdef TARGET_SIMULATOR
    // The note is the largest single value the store holds, and the sim backend
    // keeps every key in one hex-encoded file, so the cap must clear twice
    // NOTES_LEN plus the parameters. Asserted here, not in the config: it spans
    // two concerns and this page is why it exists.
    static_assert(2 * (uint32_t)Config::NOTES_LEN + 1024 <= Config::STORE_MAX_BYTES,
                  "STORE_MAX_BYTES too small for a hex-encoded NOTES_LEN plus the parameters");
#endif

    /** @brief Free-text notes the operator keeps on the panel: a `TextEditor`
     *  in Anchor::TOP over the content area, less the bottom status row.
     *  @note Persistence is deliberate - Ctrl+O writes, leaving the page
     *  flushes - because a note is kilobytes and a write is a flash erase. */
    class NotesPage : public Terminal::Page {
    private:
        /** @name Row gutter
         *  The editor draws one of these at the start of every row, which is
         *  what makes a wrapped line tell itself apart from a new one: a line
         *  the operator started is marked, its overflow is not. Same width
         *  either way, or the wrap column would move between rows.
         *  @{ */
        static const char* PROMPT_LINE() { return "> "; }   /**< PS1 / PS3 */
        static const char* PROMPT_WRAP() { return "  "; }   /**< PS2 */
        /** @} */

        /** @brief HAL::Storage key the note is persisted under. Its own key,
         *  not a parameter: prose has no band, no Modbus register and no
         *  business on the PARAMS page. @warning Renaming it orphans the note
         *  already stored on the unit. */
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

        void handle_command(Command cmd, uint16_t count = 1) override;

        /** @brief Ctrl+C: consume it, so it cannot quit the simulator out from
         *  under an unsaved note. */
        bool handle_cancel() override {
            _settle();
            return true;
        }

        /** @} */

        /** @name Focus & update
         *  @{ */

        void focus() override;

        void deactivate() override;

        void tick(bool active) override {
            if (!active) return;
            _settle();
        }

        /** @} */

        /** @name Content
         *  @{ */

        /** @brief The note as it stands. */
        const char* text() const { return _editor.buffer(); }

        /** @} */

    private:
        /** @name Helpers
         *  @{ */

        /** @brief Read the stored note into the editor, straight into its own
         *  buffer - staging it through a second one would double what the whole
         *  feature costs in RAM. */
        void _load();

        /** @brief Write the note, and remember if flash refused it. */
        void _save();

        /** @brief Bring the status row up to date and park the cursor back in
         *  the text. What every input path and every tick ends with. */
        void _settle() {
            _draw_status();
            _editor.place_cursor();
        }

        /** @brief The status row: the bottom content row, claimed as CHROME in
         *  focus() so a squeezed byte budget drops it before the text. Painted
         *  unconditionally - an unchanged row costs zero bytes at the diff
         *  (@ref dev_screen), so a page-side paint cache would buy nothing. */
        void _draw_status();

        /** @} */
    };

}}

#endif // BOTANIK_FEATURE_NOTES_PAGE
#endif // PAGES_NOTES_H
