// REVIEW_35: TODO
/**
 * @file editor.h
 * @brief Byte-efficient multi-row line editor for low-bandwidth serial
 *        terminals.
 *
 * Three prompt classes start a row: PS1 (first row), PS2 (after a soft wrap),
 * PS3 (after a hard `\\n`). Auto-wrap is off - the editor owns wrapping. Each
 * edit mutates the buffer, rebuilds the row layout (`_layout`), and repaints
 * from the edit row down - plus one IL/DL when the row count changed, which is
 * the only thing the terminal's cell diff cannot work out for itself.
 */
#ifndef CONSOLE_EDITOR_H
#define CONSOLE_EDITOR_H

#include <cstdint>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/console/terminal.h"


namespace Botanik { namespace Terminal {

    class Console;

    using Ansi::CursorPos;

    /** @brief Class of prompt to draw at the start of a row */
    enum class RowPrompt : uint8_t {
        PS1, /**< First editor row */
        PS2, /**< Row that continues a soft-wrapped row */
        PS3, /**< Row that follows a hard `\\n` */
    };

    /** @brief Geometry of a contiguous slice of the editor buffer (a row). */
    struct RowView {
        uint16_t buf_start;  /**< Offset of the slice in the buffer */
        uint16_t buf_len;    /**< Number of content bytes (no trailing \\n) */
        bool     hard_break; /**< True iff the slice is closed by `\\n` */
    };

    /** @brief A laid-out view of the buffer: the row slices plus their count.
     *  Used for both `_layout` (current/target) and `_pre` (pre-edit, kept only
     *  to tell how the row count moved and where a removal started). */
    struct TextView {
        RowView  rows[Config::EDITOR_MAX_ROWS];
        uint16_t count;
    };

    /**
     * @class TextEditor
     * @brief Multi-row line editor.
     *
     * insert/remove mutate the buffer, rebuild the row layout, then reconcile
     * the screen: one IL/DL when the row count changed (`_pre` vs `_layout`),
     * then a repaint of every row from the edit row down. The repaint looks
     * wasteful and is not - the terminal diffs it against the back-buffer, so a
     * row the IL/DL already placed correctly costs nothing, and a row that did
     * change costs exactly its changed cells.
     *
     * @note Layout is capped at `Config::EDITOR_MAX_ROWS`. At >=
     * `EDITOR_MIN_COLS` the buffer always fits by soft-wrap (static_assert in
     * config), so the cap is only hit by hard `\n` breaks; excess rows truncate
     * and the cursor folds onto the last row - a visual artifact only, never an
     * overflow.
     */
    class TextEditor {

        /**
         * @name State
         * @{
         */

        Console*      _console;        /**< Owning console (content-area rows()). */
        AnsiTerminal* _terminal;       /**< VT device (cached _console->terminal()). */

        char     _buf[Config::SHELL_INPUT_LEN]; /**< Editor buffer. */
        uint16_t _len    = 0;          /**< Bytes in _buf. */
        uint16_t _pos    = 0;          /**< Cursor offset in _buf. */
        uint16_t _origin = 0;          /**< Prompt's top row on screen. */
        uint16_t _indent = 0;          /**< Left margin: cols [0.._indent) are
                                        *   reserved for a caller-drawn label;
                                        *   0 = flush left (the shell default). */
        bool     _single_line = false; /**< When set, refuse newlines and any
                                        *   insert past the row end, so the
                                        *   editor stays exactly one row. */

        TextView _layout{};            /**< Current/target row layout. */
        /** Pre-edit layout (copied before rebuild). */
        TextView _pre{};

        /** @brief Prompt buffers and lengths, indexed by RowPrompt
         *  (PS1/PS2/PS3). */
        char     _ps_buf[3][Config::EDITOR_PROMPT_MAX];
        uint16_t _ps_len[3] = {0, 0, 0};

        /** @brief When set, automatic redraws on origin shifts are
         *  suppressed. */
        bool     _suppress_sync_redraw = false;

        /** @} */

    public:

        /** @brief Construct a TextEditor bound to the given console */
        explicit TextEditor(Console* console);

        /**
         * @name Prompts
         * @{
         */

        /** @brief Set the PS1/PS2/PS3 prompt (first row / soft-wrap / after a
         *  hard `\\n`). */
        void set_ps1(const char* buf, uint16_t len) { _set_ps(RowPrompt::PS1, buf, len); }
        void set_ps2(const char* buf, uint16_t len) { _set_ps(RowPrompt::PS2, buf, len); }
        void set_ps3(const char* buf, uint16_t len) { _set_ps(RowPrompt::PS3, buf, len); }

        /** @brief PS1 / PS2 prompt bytes and lengths. */
        const char* ps1() const { return _ps_buf[0]; }
        uint16_t    ps1_len() const { return _ps_len[0]; }
        const char* ps2() const { return _ps_buf[1]; }
        uint16_t    ps2_len() const { return _ps_len[1]; }

        /** @} */

        /**
         * @name Lifecycle
         * @{
         */

        /** @brief Clear all state and redraw an empty prompt at the bottom
         *  row */
        void reset();

        /** @brief Finalize a submitted line: reset state and draw a new PS1 at
         *  the current cursor row. Caller must have issued a `line_feed()`
         *  first, so the cursor sits at column 0 of a blank row. */
        void commit();

        /** @brief Redraw the editor's rows from the buffer. Assumes the
         *  destination rows are already blank (caller cleared them). */
        void redraw();

        /** @brief When set, redraws on origin shifts are skipped - caller
         *  becomes responsible for one explicit redraw. */
        void suppress_redraw(bool v) { _suppress_sync_redraw = v; }

        /** @} */

        /**
         * @name Geometry
         * @{
         */

        /** @brief Hardware cursor position one past the last buffer
         *  character */
        CursorPos end_pos() const { return _map_cursor_pos(_len); }

        /** @brief Hardware cursor position at the logical edit point (_pos) */
        CursorPos cursor_screen_pos() const { return _map_cursor_pos(_pos); }

        /** @brief Home the terminal cursor at the edit point and declare it as
         *  the visible cursor's home. Pages that draw around the editor call this
         *  once they are done, so present() parks there and not wherever the last
         *  bar painted. */
        void place_cursor() { _place_cursor(_map_cursor_pos(_pos)); }

        /** @brief Number of screen rows occupied by the editor right now */
        uint16_t occupied_rows() const { return _layout.count; }

        /** @brief Pin the prompt's top row to a specific screen row */
        void set_origin(uint16_t row);

        /** @brief Reserve `col` columns at the left for a caller-drawn label;
         *  the editor renders its prompt + content starting at that column.
         *  0 = flush left (the default; the shell uses it). */
        void set_indent(uint16_t col) { _indent = col; }

        /** @brief Single-line mode: refuse newlines and any insert past the row
         *  end, keeping the editor to one row (the CONFIG value field). Off by
         *  default (the shell's multi-row editing). */
        void set_single_line(bool v) { _single_line = v; }

        /** @} */

        /**
         * @name Editing
         * @{
         */

        /** @brief Insert `len` bytes at the current cursor position. */
        void insert_characters(const char* buf, uint16_t len);

        /** @brief Remove `len` bytes at the current cursor position. */
        void remove_characters(uint16_t len);

        /** @brief Move the cursor one position left */
        void move_left();

        /** @brief Move the cursor one position right */
        void move_right();

        /** @brief Move the cursor to the start of the current logical line */
        void move_to_line_start();

        /** @brief Move the cursor to the end of the current logical line */
        void move_to_line_end();

        /** @brief Delete `count` chars before the cursor in one reconcile
         *  pass. */
        void backspace(uint16_t count = 1);

        /** @brief Delete `count` chars at the cursor in one reconcile pass. */
        void delete_forward(uint16_t count = 1);

        /** @brief Move the cursor one word to the left */
        void move_word_left();

        /** @brief Move the cursor one word to the right */
        void move_word_right();

        /** @brief Toggle the mark - stub for parity with InputEditor (selection
         *         features are deferred). */
        void set_mark_toggle() {}

        /** @brief Clear the active mark/selection - stub for parity. */
        void deselect() {}

        /** @} */

        /**
         * @name Buffer access
         * @{
         */

        /** @brief Editor buffer bytes. */
        const char* buffer() const     { return _buf; }
        /** @brief Number of bytes in the editor buffer. */
        uint16_t    buffer_len() const { return _len; }
        /** @brief Cursor byte offset in the buffer (0..buffer_len()); for
         *  kill-to-EOL. */
        uint16_t    cursor_offset() const { return _pos; }

        /** @} */

    private:
        /**
         * @name Helpers
         * @{
         */

        /** @brief Store one prompt class's bytes, clamped to the buffer. */
        void _set_ps(RowPrompt p, const char* buf, uint16_t len) {
            if (len > Config::EDITOR_PROMPT_MAX) len = Config::EDITOR_PROMPT_MAX;
            memcpy(_ps_buf[(uint8_t)p], buf, len);
            _ps_len[(uint8_t)p] = len;
        }

        /** @brief Look up the prompt class for prompt-relative row
         *  `row_idx`. */
        RowPrompt _row_prompt(uint16_t row_idx) const;

        /** @brief Prompt class for row `idx` in layout `v` (PS1 first, PS3
         *  after a hard break, PS2 otherwise). */
        static RowPrompt _row_prompt_in(const TextView& v, uint16_t idx) {
            if (idx == 0) return RowPrompt::PS1;
            return v.rows[idx - 1].hard_break ? RowPrompt::PS3 : RowPrompt::PS2;
        }

        /** @brief Prompt bytes / length for the given row class. */
        const char* _prompt_buf(RowPrompt p) const { return _ps_buf[(uint8_t)p]; }
        uint16_t    _prompt_len(RowPrompt p) const { return _ps_len[(uint8_t)p]; }

        /** @brief Available capacity (columns) for content on row `row_idx`. */
        uint16_t _row_cap(uint16_t row_idx) const;

        /** @brief Scan one row's content starting at `pos` with the given
         *  capacity. */
        RowView _scan_row(uint16_t pos, uint16_t cap) const;

        /** @brief Rebuild the whole row layout from the buffer. */
        void _rebuild_layout();

        /** @brief Clear the buffer/cursor; reset to one empty row pinned at the
         *  bottom of the screen. */
        void _reset_state();

        /** @brief Owner row for buffer position `pos`: row `i` where
         *  `buf_start <= pos < buf_start + stride`, stride = buf_len +
         *  (hard_break). The `\n` belongs to its row; a soft-wrap boundary to
         *  the continuation row. End-of-buffer falls through to the last
         *  row. */
        static uint16_t _owner_row(uint16_t pos, const TextView& v) {
            for (uint16_t i = 0; i < v.count; i++) {
                uint16_t stride = (uint16_t)(v.rows[i].buf_len + (v.rows[i].hard_break ? 1 : 0));
                if (pos < (uint16_t)(v.rows[i].buf_start + stride)) return i;
            }
            return v.count > 0 ? (uint16_t)(v.count - 1) : 0;
        }

        /** @brief Buffer-position-to-screen-position map. O(_layout.count). */
        CursorPos _map_cursor_pos(uint16_t pos) const;

        /** @brief Home the cursor at @p p and tell the terminal that is where the
         *  visible cursor belongs. The two go together: a later renderer in the
         *  same tick (the status or alarm bar) moves the draw cursor away, so
         *  present() needs the declared position, not the last one used. */
        void _place_cursor(const CursorPos& p) {
            _terminal->move_to(p);
            _terminal->set_cursor_pos(p);
        }

        /** @brief Write the prompt for `row_idx` at the current cursor
         *  position. */
        void _write_prompt(uint16_t row_idx);

        /** @brief The single IL/DL for a row-count change: slide the rows below
         *  `edit_row` by `row_delta`, opening (grow) or closing (shrink) the rows
         *  the repaint then fills. No-op when the row count held. */
        void _slide_tail(uint16_t edit_row, int32_t row_delta, uint16_t scr_rows);

        /** @brief Reconcile the screen to `_layout` after an edit of `span_len`
         *  bytes at offset `span_start`. See the class-level note for the
         *  strategy. */
        void _reconcile(uint16_t span_start, uint16_t span_len, bool is_insert);

        /** @brief Bottom-pinned grow: scroll the screen up so the editor's
         *  added rows fit; return the new origin (for the later shrink). */
        uint16_t _grow_editor();

        /** @brief Bottom-pinned shrink: slide the editor down once its rows
         *  collapsed. */
        void _shrink_editor(uint16_t new_origin);

        /** @brief Paint row `post_idx` whole: prompt, content, clear to EOL. The
         *  terminal's back-buffer diff drops whatever did not actually change, so
         *  this is the only row painter the reconcile needs. */
        void _new_row(uint16_t post_idx);

        /** @} */
    };

}}

#endif // CONSOLE_EDITOR_H
