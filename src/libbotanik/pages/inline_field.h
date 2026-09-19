#ifndef PAGES_INLINE_FIELD_H
#define PAGES_INLINE_FIELD_H

/**
 * @file inline_field.h
 * @brief InlineField - the one-row value field MONITOR and PARAMS edit in.
 * @note The field owns the editor and the refusal marker; the page owns what
 *       is being edited and what committing it means.
 */

#include <cstdint>
#include <cstring>

#include "libbotanik/console/console.h"
#include "libbotanik/console/editor.h"

namespace Botanik { namespace Pages {

    /** @brief One-row inline editor (see file header).
     *  @tparam CAP  Field buffer bytes, NUL included.
     *  @tparam ROWS Row-layout entries the editor may use. */
    template <uint16_t CAP, uint16_t ROWS>
    class InlineField {

        /** @name State
         *  @{ */

        using Command = Terminal::InputEvent::Command;
        using Console = Terminal::Console;
        using Editor  = Terminal::TextEditor;

        /** @brief Buffers the editor borrows; declared before it. */
        Terminal::EditorBuffers<CAP, ROWS> _bufs;
        Editor                    _editor;
        Terminal::AnsiTerminal&   _terminal;
        bool                      _open  = false;
        bool                      _error = false;   ///< last commit refused

        /** @} */

    public:
        /** @brief What a key left for the page to do. */
        enum class Outcome : uint8_t { HANDLED, COMMIT, CANCEL };

        explicit InlineField(Console& console)
            : _editor(&console, _bufs.storage()), _terminal(console.terminal()) {}

        /** @name State
         *  @{ */

        bool open()  const { return _open; }
        bool error() const { return _error; }

        /** @brief The editor itself, for a page that has to reach its
         *  geometry. */
        Editor& editor() { return _editor; }

        /** @} */

        /** @name Lifecycle
         *  @{ */

        /** @brief Open the field on @p row behind @p label (@p label_w wide),
         *  seeded with [@p seed, @p seed + @p seed_len). */
        void begin(uint16_t row, const char* label, uint16_t label_w,
                   const char* seed, uint16_t seed_len) {
            _editor.as_field(row, label_w);
            _editor.reset();                    // clears the row past the label
            _terminal.move_to({row, 0});
            _terminal.write_chars(label, label_w);
            if (seed_len > 0) _editor.insert_characters(seed, seed_len);
            else              _editor.place_cursor();
            _error = false;
            _open  = true;
            _terminal.set_cursor_visible(true);
        }

        /** @brief Leave the field. The page repaints the row it frees. */
        void end() { _open = false; }

        /** @brief Repaint the whole field row: label, editor, and the refusal
         *  marker when the last commit was refused. */
        void redraw(uint16_t row, const char* label, uint16_t label_w) {
            _terminal.set_inverted(false);
            _terminal.move_to({row, 0});
            _terminal.clear_to_eol();
            _terminal.write_chars(label, label_w);
            _editor.redraw();
            if (_error) _draw_marker();
            _terminal.set_cursor_visible(true);
        }

        /** @} */

        /** @name Content
         *  @{ */

        /** @brief Copy the field into @p out, NUL-terminated and truncated to
         *  @p cap. */
        void read(char* out, size_t cap) const {
            uint16_t n = _editor.buffer_len();
            if (n > (uint16_t)(cap - 1)) n = (uint16_t)(cap - 1);
            std::memcpy(out, _editor.buffer(), n);
            out[n] = '\0';
        }

        /** @brief Flag the field: the page refused what it holds. */
        void refuse() { _error = true; _draw_marker(); }

        /** @} */

        /** @name Input
         *  @{ */

        /** @brief Run one editing key through the field.
         *  @return what the page still owes: COMMIT on ENTER, CANCEL on ESC. */
        Outcome handle_command(Command cmd, uint16_t count) {
            if (cmd == Command::ENTER) return Outcome::COMMIT;
            if (cmd == Command::ESC)   return Outcome::CANCEL;
            // Ctrl-U clears the field, which the editor does not bind: a field
            // has nothing to lose.
            const bool clear = (cmd == Command::CTRL_U);
            const Editor::Key k = clear ? Editor::Key::CHANGED : _editor.key_kind(cmd);
            if (k == Editor::Key::IGNORED) return Outcome::HANDLED;
            // Before the edit, not after: the marker is drawn from the field's
            // END, which the edit is about to move.
            if (k == Editor::Key::CHANGED) _clear_marker();
            if (clear) _editor.reset();
            else       (void)_editor.handle_key(cmd, count);
            return Outcome::HANDLED;
        }

        /** @brief Type one character into the field. */
        void handle_text(char c) {
            _clear_marker();
            _editor.insert_characters(&c, 1);
        }

        /** @} */

    private:
        /** @name Refusal marker
         *  @{ */

        void _draw_marker() {
            static const char mark[] = "  [invalid]";
            _terminal.move_to(_editor.end_pos());
            _terminal.write_chars(mark, (uint16_t)(sizeof(mark) - 1));
            _editor.place_cursor();
        }

        void _clear_marker() {
            if (!_error) return;
            _terminal.move_to(_editor.end_pos());
            _terminal.clear_to_eol();
            _error = false;
            _editor.place_cursor();
        }

        /** @} */
    };

}}

#endif // PAGES_INLINE_FIELD_H
