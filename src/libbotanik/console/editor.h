// REVIEW_35: TODO
/**
 * @file editor.h
 * @brief Byte-efficient multi-row line editor for low-bandwidth serial
 *        terminals.
 *
 * Three prompt classes start a row: PS1 (first row), PS2 (after a soft wrap),
 * PS3 (after a hard `\\n`). Auto-wrap is off - the editor owns wrapping. Each
 * edit mutates the buffer, rebuilds the row layout (`_layout`), and reconciles
 * it against the pre-edit layout (`_pre`) so only the minimal terminal ops are
 * emitted.
 */
#ifndef CONSOLE_EDITOR_H
#define CONSOLE_EDITOR_H

#include <cstdint>                          // uint8_t/uint16_t/int32_t
#include <cstring>                          // memcpy

#include "libbotanik/config/terminal.h"   // Config::SCREEN_* / EDITOR_* / SHELL_* / PAGE_*

#include "libbotanik/console/terminal.h"    // AnsiTerminal / CursorPos


namespace Botanik { namespace Terminal {

    class Console;

    using Ansi::CursorPos;

    /** @brief Class of prompt to draw at the start of a row */
    enum class RowPrompt : uint8_t {
        PS1, /**< First editor row */
        PS2, /**< Row that continues a soft-wrapped row */
        PS3, /**< Row that follows a hard `\\n` */
    };

    /** @brief Which edge of its band the editor is pinned to. */
    enum class Anchor : uint8_t {
        BOTTOM, /**< Last row nailed to the content bottom; growing the text
                 *   scrolls the band up so output above it scrolls away. What a
                 *   shell prompt wants. */
        TOP,    /**< Origin fixed. The text does not push the screen around -
                 *   the viewport scrolls over it instead. What a full-screen
                 *   editor wants. */
    };

    /** @brief Geometry of a contiguous slice of the editor buffer (a row). */
    struct RowView {
        uint16_t buf_start;  /**< Offset of the slice in the buffer */
        uint16_t buf_len;    /**< Number of content bytes (no trailing \\n) */
        bool     hard_break; /**< True iff the slice is closed by `\\n` */
    };

    /** @brief A laid-out view of the buffer: the row slices plus their count.
     *  Used for both `_layout` (current/target) and `_pre` (pre-edit).
     *
     *  @note `rows` is BORROWED - the two views point at distinct owner-supplied
     *  arrays (see EditorStorage). Copying a TextView by value would alias them,
     *  hence `_snapshot_pre()` rather than assignment. */
    struct TextView {
        RowView* rows  = nullptr; /**< Borrowed row array (see EditorStorage). */
        uint16_t count = 0;       /**< Rows laid out right now. */
    };

    /** @brief The buffers a TextEditor works over, owned by whoever built it.
     *
     *  External so each editor is sized for its job: a shell prompt wants a few
     *  hundred bytes, a full-screen notes editor kilobytes, and neither pays for
     *  the other. */
    struct EditorStorage {
        char*    buf;      /**< Text buffer, `buf_cap` bytes (one reserved for NUL). */
        uint16_t buf_cap;
        RowView* layout;   /**< Row array for the current layout, `max_rows` entries. */
        RowView* pre;      /**< Row array for the pre-edit layout, same size. */
        uint16_t max_rows;
    };

    /** @brief Inline storage for a TextEditor, sized at compile time.
     *
     *  Declare one next to (and BEFORE) the editor it feeds:
     *  @code
     *  EditorBuffers<Config::SHELL_INPUT_LEN, Config::EDITOR_MAX_ROWS> _bufs;
     *  TextEditor _editor{&console, _bufs.storage()};
     *  @endcode */
    template <uint16_t BUF, uint16_t ROWS>
    struct EditorBuffers {
        char    buf[BUF];
        RowView layout[ROWS];
        RowView pre[ROWS];

        /** @brief Descriptor to hand the editor's constructor. */
        EditorStorage storage() { return EditorStorage{ buf, BUF, layout, pre, ROWS }; }
    };

    /**
     * @class TextEditor
     * @brief Multi-row line editor.
     *
     * insert/remove mutate the buffer, rebuild the row layout, then reconcile
     * the screen by diffing `_pre` against `_layout`: one IL/DL for the
     * row-count change, then a forward per-row splice walk from the edit row.
     *
     * @note Layout is capped at the owner's row array. At >= `EDITOR_MIN_COLS`
     * the buffer always fits by soft-wrap (static_asserts in config), so the cap
     * is only reachable through hard `\n` breaks; an insert that would cross it
     * is refused, since rows past the cap are text nobody can reach.
     *
     * @note Text taller than the viewport is not a special case: the window
     * scrolls over it (see Anchor and set_view_rows()).
     */
    class TextEditor {

        /**
         * @name State
         * @{
         */

        Console*      _console;        /**< Owning console (content-area rows()). */
        AnsiTerminal* _terminal;       /**< VT device (cached _console->terminal()). */

        char*    _buf;                 /**< Editor buffer (borrowed). */
        uint16_t _buf_cap;             /**< Capacity of _buf, NUL included. */
        uint16_t _max_rows;            /**< Capacity of both row arrays. */
        uint16_t _len    = 0;          /**< Bytes in _buf. */
        uint16_t _pos    = 0;          /**< Cursor offset in _buf. */
        uint16_t _origin = 0;          /**< Prompt's top row on screen. */
        uint16_t _indent = 0;          /**< Left margin: cols [0.._indent) are
                                        *   reserved for a caller-drawn label;
                                        *   0 = flush left (the shell default). */
        bool     _single_line = false; /**< When set, refuse newlines and any
                                        *   insert past the row end, so the
                                        *   editor stays exactly one row. */

        /** @brief First VISIBLE layout row. Rows before it are scrolled off the
         *  top of the viewport and paint nothing. */
        uint16_t _view_top = 0;
        /** @brief Screen rows the editor may paint into, starting at _origin.
         *  0 means "everything from _origin to the content bottom", which is
         *  what a bottom-anchored editor wants. */
        uint16_t _view_rows = 0;
        /** @brief The viewport as it was before the current edit. The reconcile
         *  needs it to know which pre rows are physically on screen: one that
         *  was not has no glyphs to splice, however well its slice matches. */
        uint16_t _pre_view_top    = 0;
        uint16_t _pre_view_height = 0;
        /** @brief Screen rows the editor has glyphs on, counted from _origin.
         *  What the text SHRANK past is not otherwise anybody's: the per-row walk
         *  only visits rows the new layout has, and the tail IL/DL only runs when
         *  there is a tail to slide. */
        uint16_t _screen_used = 0;
        /** @brief Which edge the editor is pinned to. */
        Anchor   _anchor = Anchor::BOTTOM;

        /** @brief Column vertical motion aims for, as an offset into the row's
         *  content. Walking down through a short line and out the other side
         *  returns to the column you started in, rather than sticking to the
         *  short line's end. Any horizontal motion or edit drops it. */
        uint16_t _goal_col   = 0;
        bool     _goal_valid = false;

        /** @brief Region anchor, and whether it is live. Like the goal column,
         *  a latched position that unrelated actions drop - here, ANY edit (see
         *  the Mark and region group). */
        uint16_t _mark     = 0;
        bool     _mark_set = false;
        /** @brief What the screen currently shows highlighted, so _sync_region()
         *  can return without touching a cell when it is already right - which is
         *  every call with no mark, so ordinary editing costs what it always
         *  did. */
        uint16_t _painted_from = 0, _painted_to = 0;
        bool     _painted_active = false;

        /** @brief Set by every edit, cleared by the owner once it has saved. */
        bool     _dirty = false;

        TextView _layout{};            /**< Current/target row layout. */
        /** Pre-edit layout (snapshotted before rebuild). */
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

        /** @brief Construct a TextEditor bound to @p console, working over the
         *  caller-owned buffers in @p store (which must outlive it). */
        TextEditor(Console* console, const EditorStorage& store);

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

        /** @brief Home the terminal cursor at the edit point and declare it as
         *  the visible cursor's home. Pages that draw around the editor call this
         *  once they are done, so present() parks there and not wherever the last
         *  bar painted. */
        void place_cursor() { _place_cursor(_map_cursor_pos(_pos)); }

        /** @brief Number of screen rows occupied by the editor right now */
        uint16_t occupied_rows() const { return _layout.count; }

        /** @brief Pin the prompt's top row to a specific screen row.
         *  @note Under Anchor::BOTTOM this is advisory - reset() and commit()
         *  re-pin the origin to the content bottom. Under Anchor::TOP it
         *  stands. */
        void set_origin(uint16_t row);

        /** @brief Which edge the editor is pinned to (default BOTTOM). */
        void set_anchor(Anchor a) { _anchor = a; }

        /** @brief Screen rows the editor may paint into, starting at the
         *  origin. 0 (the default) means everything down to the content bottom;
         *  a page that keeps a row of its own below the editor passes the
         *  smaller count. Text taller than this scrolls under the viewport. */
        void set_view_rows(uint16_t n) { _view_rows = n; }

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

        /** @} */

        /**
         * @name Kill and yank
         *
         * The three cut/paste keys, over the console-wide kill ring
         * (console/kill_ring.h), so what is cut here yanks wherever the operator
         * goes next. A kill takes only what the ring can hold and leaves the rest
         * in the buffer, so a yank restores exactly what went. Each returns false
         * when there was nothing to do.
         * @{
         */

        /** @brief Cut from the cursor to the end of the logical line (Ctrl-K).
         *  On the newline itself, cuts the newline - which is how a line is
         *  closed up onto the one before it. */
        bool kill_to_line_end();

        /** @brief Cut the word before the cursor (Ctrl-W), whitespace between
         *  it and the cursor included. */
        bool kill_word_back();

        /** @brief Insert the ring's contents at the cursor (Ctrl-Y). */
        bool yank();

        /** @} */

        /**
         * @name Mark and region
         *
         * Ctrl-Space drops a mark; the region is the span between it and the
         * cursor, drawn reverse-video, and Ctrl-W / Alt-W act on it instead of
         * on a word. The mark is TRANSIENT: any edit drops it, so one can never
         * need shifting by an insert or clamping by a delete. See @ref
         * dev_editor.
         * @{
         */

        /** @brief Anchor the region at the cursor. */
        void set_mark();

        /** @brief Drop the anchor (Ctrl-G, and every edit). */
        void clear_mark();

        /** @brief What Ctrl-Space does: the mark is a toggle, not a latch, so
         *  pressing it again on a region you did not want releases it. */
        void toggle_mark();

        /** @brief What Ctrl-W does: the region when there is one, the word
         *  before the cursor when there is not - which is what makes the mark
         *  optional rather than a mode. Here rather than in each page's key
         *  switch, so the three cannot drift. */
        bool kill_region_or_word();

        /** @brief Whether an anchor is live. */
        bool mark_active() const { return _mark_set; }

        /** @brief The region as an ordered half-open span.
         *  @return false (leaving @p from / @p to untouched) with no mark. */
        bool region(uint16_t& from, uint16_t& to) const;

        /** @brief Cut the region into the kill ring (Ctrl-W with a mark). */
        bool kill_region();

        /** @brief Copy the region without removing it (Alt-W). */
        bool copy_region();

        /** @brief Move the cursor one display row up / down, holding the goal
         *  column. On the first/last row it falls to the start/end of the
         *  buffer. */
        void move_up();
        void move_down();

        /** @brief Move the cursor one viewport height up / down. */
        void page_up();
        void page_down();

        /** @brief Move the cursor to the start / end of the buffer. */
        void move_to_start();
        void move_to_end();

        /** @} */

        /**
         * @name Bulk content
         * @{
         */

        /** @brief Writable buffer to read stored text straight into, and how
         *  many bytes of it are usable. Pair with commit_loaded() - it saves a
         *  caller staging a second full-size copy of the text. */
        char*    load_buffer()         { return _buf; }
        uint16_t load_capacity() const { return (uint16_t)(_buf_cap - 1); }

        /** @brief Adopt @p len bytes already written into load_buffer() and
         *  repaint, without running an edit (and a reconcile) per byte. Clears
         *  the dirty flag: this is the stored text, not an edit of it.
         *  @return false if the text did not fit - past the buffer, or past what
         *  the row cap can lay out - in which case the tail was dropped. */
        bool commit_loaded(uint16_t len);

        /** @brief commit_loaded() for text the caller already holds. */
        bool load(const char* buf, uint16_t len);

        /** @brief Whether the buffer has changed since the last load() /
         *  clear_dirty(). */
        bool dirty() const { return _dirty; }

        /** @brief Declare the buffer saved. */
        void clear_dirty() { _dirty = false; }

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

        /** @brief Rebuild the whole row layout from the buffer.
         *  @return true if it ran out of rows before it ran out of buffer - the
         *  text past that point cannot be shown or reached. */
        bool _rebuild_layout();

        /** @brief Copy `_layout` into `_pre` ahead of a rebuild. Not `_pre =
         *  _layout`: the two views borrow distinct row arrays, and assigning
         *  would alias them so the reconcile diffed a layout against itself. */
        void _snapshot_pre();

        /** @brief Put `_pre` back as the current layout, for an edit that was
         *  laid out and then refused. Cheaper and exacter than re-deriving it:
         *  `_pre` IS the pre-edit layout. */
        void _restore_pre();

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

        /**
         * @name Viewport
         *
         * The window of layout rows currently on screen. A bottom-anchored
         * editor is the degenerate case (`_view_top` 0, height running to the
         * content bottom), so these subsume a bare `_origin + i` and a clip
         * against `_console->rows()`.
         * @{
         */

        /** @brief Content-area height. Out of line because `Console` is only
         *  forward-declared here, so the inlines below cannot reach
         *  `_console->rows()` directly. */
        uint16_t _console_rows() const;

        /** @brief Screen rows the viewport spans right now. */
        uint16_t _view_height() const {
            if (_view_rows != 0) return _view_rows;
            uint16_t rows = _console_rows();
            return rows > _origin ? (uint16_t)(rows - _origin) : 0;
        }

        /** @brief Last screen row the editor may paint, never below the content
         *  area (so the reserved alarm bar stays untouched). */
        uint16_t _view_bottom_hw() const;

        /** @brief Screen row showing layout row @p i. Only meaningful when
         *  `_visible(i)`. */
        uint16_t _hw_row(uint16_t i) const {
            return (uint16_t)(_origin + (i - _view_top));
        }

        /** @brief Whether layout row @p i is inside the viewport. */
        bool _visible(uint16_t i) const {
            return i >= _view_top && (uint16_t)(i - _view_top) < _view_height();
        }

        /** @brief One past the last layout row on screen: the viewport's bottom
         *  or the end of the text, whichever comes first. */
        uint16_t _visible_end() const {
            uint16_t e = (uint16_t)(_view_top + _view_height());
            return e > _layout.count ? _layout.count : e;
        }

        /** @brief Screen rows the text occupies right now. */
        uint16_t _rows_used() const { return (uint16_t)(_visible_end() - _view_top); }

        /** @brief Blank the rows the text has just shrunk past, and record what it
         *  occupies now. One DL for the lot rather than a row of blanks each: a
         *  region kill takes as many rows as it likes for the same escape.
         *  @note Bottom-anchored the band slides instead (_shrink_editor), which
         *  puts those rows below the screen, so the clamp leaves nothing to do. */
        void _trim_screen();

        /** @brief Whether layout row @p i of the PRE-edit layout was on screen,
         *  i.e. whether the physical row still shows its glyphs. */
        bool _was_visible(uint16_t i) const {
            return i >= _pre_view_top && (uint16_t)(i - _pre_view_top) < _pre_view_height;
        }

        /** @brief Scroll the viewport if the cursor left it, painting only the
         *  rows the scroll revealed. Returns true if the viewport moved.
         *  @note A jump further than the window height cannot reuse anything, so
         *  it clears and repaints rather than scrolling by a whole screen. */
        bool _follow_cursor();

        /** @brief Bring the cursor back into view, then home the terminal at it.
         *  What every motion ends with. Drops the goal column unless @p
         *  keep_goal - only vertical motion holds on to it. */
        void _settle_cursor(bool keep_goal = false) {
            if (!keep_goal) _goal_valid = false;
            _follow_cursor();
            // A motion with a mark live has moved the region's far end, and
            // nothing else on this path repaints a thing.
            _sync_region();
            _place_cursor(_map_cursor_pos(_pos));
        }

        /** @brief Move the cursor @p delta display rows, holding the goal
         *  column. Clamped to the layout; a move that cannot go further falls
         *  to the buffer start/end instead. */
        void _move_row(int32_t delta);

        /** @brief Offset one word left / right of the cursor. Where
         *  move_word_left() / move_word_right() land, and what kill_word_back()
         *  cuts back to. */
        uint16_t _word_start() const;
        uint16_t _word_end() const;

        /** @brief Cut `[from, to)` into the kill ring and remove exactly the
         *  bytes it took. Leaves the cursor at @p from.
         *  @return false if the span was empty or nothing fit. */
        bool _kill_span(uint16_t from, uint16_t to);

        /** @brief Write layout row @p i's content at the cursor, as up to three
         *  pen runs so the part inside the region comes out reverse-video.
         *  Always leaves the pen plain, so the caller's clear_to_eol() does not
         *  stamp an inverted bar out to the margin.
         *
         *  An EMPTY region (mark just dropped, cursor not moved) paints one
         *  inverted cell at the mark, since a zero-width highlight would leave
         *  Ctrl-Space with nothing to show for itself. */
        void _write_row_content(uint16_t i);

        /** @brief Bring the highlight on screen into line with the region.
         *  Re-stamps the visible rows, and returns without touching a cell when
         *  what is painted is already right - which is every call with no mark,
         *  so the no-region byte cost is unchanged by construction. */
        void _sync_region();

        /** @brief Declare the visible rows as painted with the region as it
         *  stands. For whoever has just drawn them all: _sync_region() itself,
         *  and redraw(). */
        void _record_painted();

        /** @} */

        /** @brief Declare which rows the editor occupies now. Called whenever the
         *  extent may have moved. */
        void _place_rows();


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

        /** @brief Where an edit lands in the forward walk. `edit_row` is the
         *  first affected row. Rows at or below `absorbed_post` are already
         *  correct (shifted by the IL/DL), so the per-row walk only splices
         *  `[edit_row, absorbed_post)`. */
        struct SpliceSpan {
            uint16_t edit_row;      /**< R: first affected post row */
            /** A: post idx where the absorbed tail begins */
            uint16_t absorbed_post;
            uint16_t absorbed_pre;  /**< P: matching pre idx of that boundary */
        };

        /** @brief Locate the edit row and the absorbed-tail boundary by diffing
         *         `_pre` against `_layout` (longest shifted-identical
         *         suffix). */
        SpliceSpan _find_splice_span(uint16_t span_start, uint16_t span_len,
                                     bool is_insert, int32_t l_shift,
                                     int32_t row_delta) const;

        /** @brief Emit the single IL/DL that repositions the absorbed tail and
         *  opens the blank rows inserted content paints into. Bounded to the
         *  viewport, so rows outside it are neither scrolled nor rewritten. */
        void _reposition_tail(const SpliceSpan& span, int32_t row_delta);

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

        /** @brief Full-paint row `post_idx` (prompt + content + clear): an
         *  IL-opened blank, or a row whose old glyphs are unrelated to the
         *  wanted content. */
        void _new_row(uint16_t post_idx);

        /** @brief Reconcile one row: splice `amount` chars at column `col`
         *  (insert if >0, delete if <0), then length-align the tail to `want`.
         *  The edit row passes `col = cursor`; rows below pass `col = prompt`,
         *  `amount = displacement`. `shown_len`/`shown_prompt_l` describe what
         *  the row physically shows now. */
        void _splice_row(uint16_t post_idx, uint16_t col, int32_t amount,
                         uint16_t shown_len, uint16_t shown_prompt_l,
                         const RowView& want);

        /** @brief Signed count of content chars the edit splices onto its own
         *  row: the span clipped to that row's content (0 for a closing `\n`),
         *  positive for insert, negative for remove. */
        static int32_t _edit_row_amount(uint16_t span_start, uint16_t span_len,
                                        bool is_insert, const RowView& post_row,
                                        const RowView& pre_row);

        /** @} */
    };

}}

#endif // CONSOLE_EDITOR_H
