/**
 * @file editor.h
 * @brief Multi-row line editor for a low-bandwidth serial terminal. Auto-wrap
 *        is off: the editor owns wrapping. See @ref dev_editor.
 */
#ifndef CONSOLE_EDITOR_H
#define CONSOLE_EDITOR_H

#include <cstdint>
#include <cstring>

#include "libbotanik/config/terminal.h"

#include "libbotanik/console/terminal.h"


namespace Botanik { namespace Terminal {

    class Console;

    using Ansi::CursorPos;

    /** @brief Class of prompt drawn at the start of a row. */
    enum class RowPrompt : uint8_t {
        PS1, /**< First editor row */
        PS2, /**< Row that continues a soft-wrapped row */
        PS3, /**< Row that follows a hard `\\n` */
    };

    /** @brief Which edge of its band the editor is pinned to. */
    enum class Anchor : uint8_t {
        BOTTOM, /**< Last row on the content bottom; growing scrolls the band
                 *   up. The shell prompt. */
        TOP,    /**< Origin fixed; a viewport scrolls over the text. The NOTES
                 *   page. */
    };

    /** @brief One screen row's slice of the buffer. */
    struct RowView {
        uint16_t buf_start;  /**< Offset of the slice in the buffer */
        uint16_t buf_len;    /**< Content bytes, no trailing `\\n` */
        bool     hard_break; /**< True iff the slice is closed by `\\n` */
    };

    /** @brief The buffers a TextEditor works over, owned by whoever built it. */
    struct EditorStorage {
        char*    buf;      /**< `buf_cap` bytes, one reserved for the NUL. */
        uint16_t buf_cap;
        RowView* layout;   /**< `max_rows` entries. */
        uint16_t max_rows;
    };

    /** @brief Inline storage for a TextEditor. Declare it BEFORE the editor
     *  it feeds. */
    template <uint16_t BUF, uint16_t ROWS>
    struct EditorBuffers {
        char    buf[BUF];
        RowView layout[ROWS];

        EditorStorage storage() { return EditorStorage{ buf, BUF, layout, ROWS }; }
    };

    /**
     * @class TextEditor
     * @brief Multi-row line editor. An edit relays out the buffer, tells the
     *        terminal what the layout did (a band of rows that moved, a run of
     *        cells that rippled) and repaints the viewport.
     * @note An insert that would cross the owner's row cap is refused.
     */
    class TextEditor {

        /** @name State
         *  @{ */

        Console*      _console;
        AnsiTerminal* _terminal;

        char*    _buf;                 /**< Borrowed. */
        uint16_t _buf_cap;             /**< NUL included. */
        uint16_t _max_rows;            /**< Capacity of the row array. */
        uint16_t _len    = 0;          /**< Bytes in _buf. */
        uint16_t _pos    = 0;          /**< Cursor offset in _buf. */
        uint16_t _origin = 0;          /**< Screen row of the viewport's top. */
        uint16_t _indent = 0;          /**< Columns [0.._indent) are a caller-drawn label. */
        bool     _single_line = false; /**< Refuse newlines and any wrap. */

        uint16_t _view_top  = 0;       /**< First layout row on screen. */
        uint16_t _view_rows = 0;       /**< Viewport height; 0 = to the content bottom. */
        Anchor   _anchor = Anchor::BOTTOM;

        /** @brief Column vertical motion aims for; any other motion or edit
         *  drops it. */
        uint16_t _goal_col   = 0;
        bool     _goal_valid = false;

        /** @brief Region anchor; any edit drops it. */
        uint16_t _mark     = 0;
        bool     _mark_set = false;

        /** @brief Set by every edit, cleared by the owner once it has saved. */
        bool     _dirty = false;

        RowView* _rows;                /**< The buffer sliced into rows; borrowed. */
        uint16_t _row_count = 1;

        /** @brief Prompt bytes and lengths, indexed by RowPrompt. */
        char     _ps_buf[3][Config::EDITOR_PROMPT_MAX];
        uint16_t _ps_len[3] = {0, 0, 0};

        /** @} */

    public:

        /** @name Lifecycle
         *  @{ */

        /** @brief Bound to @p console, over the caller-owned buffers in
         *  @p store, which must outlive it. */
        TextEditor(Console* console, const EditorStorage& store);

        /** @brief Discard the text and show an empty prompt. Marks dirty. */
        void reset();

        /** @brief Start a fresh line after a submitted one; the rows the text
         *  gave up are the owner's to repaint. */
        void commit();

        /** @brief Repaint the viewport from the buffer, blank rows included. */
        void redraw();

        /** @} */

        /** @name Prompts
         *  @{ */

        void set_ps1(const char* buf, uint16_t len) { _set_ps(RowPrompt::PS1, buf, len); }
        void set_ps2(const char* buf, uint16_t len) { _set_ps(RowPrompt::PS2, buf, len); }
        void set_ps3(const char* buf, uint16_t len) { _set_ps(RowPrompt::PS3, buf, len); }

        const char* ps1() const { return _ps_buf[0]; }
        uint16_t    ps1_len() const { return _ps_len[0]; }
        const char* ps2() const { return _ps_buf[1]; }
        uint16_t    ps2_len() const { return _ps_len[1]; }

        /** @} */

        /** @name Geometry
         *  @{ */

        /** @brief Screen position one past the last character. */
        CursorPos end_pos() const { return _map_cursor_pos(_len); }

        /** @brief Home the terminal at the edit point and declare it the
         *  visible cursor's place. A page drawing around the editor calls it
         *  last. */
        void place_cursor() { _place_cursor(_map_cursor_pos(_pos)); }

        uint16_t occupied_rows() const { return _row_count; }

        /** @brief Pin the viewport's top row. Bottom-anchored, the editor
         *  re-pins it to the content bottom as the text grows and shrinks. */
        void set_origin(uint16_t row);

        void set_anchor(Anchor a) { _anchor = a; }

        /** @brief Viewport height from the origin; 0 (the default) runs to the
         *  content bottom. */
        void set_view_rows(uint16_t n) { _view_rows = n; }

        /** @brief Reserve @p col columns at the left for a caller-drawn label. */
        void set_indent(uint16_t col) { _indent = col; }

        /** @brief Refuse newlines and any insert past the row end. */
        void set_single_line(bool v) { _single_line = v; }

        /** @} */

        /** @name Editing
         *  @{ */

        void insert_characters(const char* buf, uint16_t len);

        /** @brief Delete @p count chars before / at the cursor, one reconcile. */
        void backspace(uint16_t count = 1);
        void delete_forward(uint16_t count = 1);

        /** @} */

        /** @name Cursor motion
         *  Every one of these ends in _settle_cursor(): viewport, repaint,
         *  cursor. None of them changes the buffer. @{ */

        void move_left();
        void move_right();
        void move_to_line_start();
        void move_to_line_end();

        void move_word_left();
        void move_word_right();

        /** @brief One display row up / down, holding the goal column; on the
         *  first / last row, to the start / end of the buffer. */
        void move_up();
        void move_down();

        /** @brief One viewport height up / down. */
        void page_up();
        void page_down();

        void move_to_start();
        void move_to_end();

        /** @} */

        /** @name Kill and yank
         *  Over the console-wide kill ring. A kill takes only what the ring
         *  holds; each returns false when there was nothing to do. @{ */

        /** @brief Cut to the end of the logical line (Ctrl-K); on the newline
         *  itself, cut the newline. */
        bool kill_to_line_end();

        /** @brief Cut the word before the cursor (Ctrl-W), with the whitespace
         *  between it and the cursor. */
        bool kill_word_back();

        /** @brief Insert the ring's contents at the cursor (Ctrl-Y). */
        bool yank();

        /** @} */

        /** @name Mark and region
         *  Ctrl-Space drops a mark; the region is the span to the cursor. Any
         *  edit drops the mark. @{ */

        void set_mark();
        void clear_mark();
        void toggle_mark();

        bool copy_region();

        /** @brief The region when there is one, else the word before the
         *  cursor (Ctrl-W). */
        bool kill_region_or_word();

        /** @} */

        /** @name Bulk content
         *  @{ */

        /** @brief Buffer to read stored text straight into, and the bytes of
         *  it usable. Pair with commit_loaded(). */
        char*    load_buffer()         { return _buf; }
        uint16_t load_capacity() const { return (uint16_t)(_buf_cap - 1); }

        /** @brief Adopt @p len bytes written into load_buffer(), cursor at
         *  the start, not dirty.
         *  @return false if a tail was dropped, for want of buffer or rows. */
        bool commit_loaded(uint16_t len);

        /** @brief Replace the whole text in one edit, cursor at the end, dirty.
         *  @p text may be a slice of the buffer itself. */
        void replace_all(const char* text, uint16_t len);

        /** @} */

        /** @name Buffer access
         *  @{ */

        const char* buffer() const     { return _buf; }
        uint16_t    buffer_len() const { return _len; }
        uint16_t    cursor_offset() const { return _pos; }

        bool dirty() const { return _dirty; }
        void clear_dirty() { _dirty = false; }

        /** @} */

    private:
        /** @name Prompts
         *  @{ */

        void _set_ps(RowPrompt p, const char* buf, uint16_t len) {
            if (len > Config::EDITOR_PROMPT_MAX) len = Config::EDITOR_PROMPT_MAX;
            memcpy(_ps_buf[(uint8_t)p], buf, len);
            _ps_len[(uint8_t)p] = len;
        }

        /** @brief PS1 first, PS3 after a hard break, PS2 otherwise. */
        RowPrompt _row_prompt(uint16_t idx) const {
            if (idx == 0) return RowPrompt::PS1;
            return _rows[idx - 1].hard_break ? RowPrompt::PS3 : RowPrompt::PS2;
        }

        const char* _prompt_buf(RowPrompt p) const { return _ps_buf[(uint8_t)p]; }
        uint16_t    _prompt_len(RowPrompt p) const { return _ps_len[(uint8_t)p]; }

        /** @} */

        /** @name Layout
         *  @{ */

        /** @brief Content columns on row @p row_idx. */
        uint16_t _row_cap(uint16_t row_idx) const;

        RowView _scan_row(uint16_t pos, uint16_t cap) const;

        /** @brief Relay out the buffer.
         *  @return true if the rows ran out before the buffer did. */
        bool _rebuild_layout();

        /** @brief Make @p len bytes of @p text (a slice of the buffer, or null)
         *  the whole buffer, cursor at @p cursor, and lay it out. No reconcile.
         *  @return false if a tail was dropped, for want of buffer or rows. */
        bool _load(const char* text, uint16_t len, uint16_t cursor);

        /** @brief Row holding buffer offset @p pos. The `\n` belongs to its
         *  row, a soft-wrap boundary to the continuation row, the end of the
         *  buffer to the last row. */
        uint16_t _owner_row(uint16_t pos) const {
            for (uint16_t i = 0; i < _row_count; i++) {
                const uint16_t stride = (uint16_t)(_rows[i].buf_len + (_rows[i].hard_break ? 1 : 0));
                if (pos < (uint16_t)(_rows[i].buf_start + stride)) return i;
            }
            return (uint16_t)(_row_count - 1);
        }

        CursorPos _map_cursor_pos(uint16_t pos) const;

        /** @} */

        /** @name Viewport
         *  @{ */

        /** @brief Viewport height: the one set, else down to the content bottom. */
        uint16_t _view_height() const;

        /** @brief Last screen row the editor may paint, never below the
         *  content area. */
        uint16_t _view_bottom_hw() const;

        /** @brief One edit: what the layout before it said about it, and
         *  what the announcer works out once the new layout is in. */
        struct Edit {
            enum Kind : uint8_t {
                NONE,    /**< A motion or a load: only the viewport moved. */
                PLAIN,   /**< Bytes with no newline: the line rippled. */
                SPLIT,   /**< One newline in: a row split at the edit point. */
                JOIN,    /**< One newline out: two lines became one. */
                OTHER,   /**< Newlines crossed: only the rows after moved as one. */
            };
            Kind     kind = NONE;
            uint16_t pos = 0;            /**< Buffer offset of the edit. */
            int16_t  delta = 0;          /**< Bytes added; negative for removed. */
            uint16_t a = 0, L = 0;       /**< Row holding pos, and the last row of
                                          *   the last line the edit touched. */
            RowView  row_a, row_b, row_L;/**< Rows a, a+1 and L before the edit. */
            uint16_t ps_a = 0;           /**< Prompt width row a had. */
            uint16_t count = 0;          /**< Rows before the edit. */
            uint16_t origin_was = 0, view_top_was = 0;
            /** @name Worked out by _announce() */
            int32_t  d = 0;              /**< Rows the edit added. */
            int32_t  m0 = 0, mB = 0;     /**< Screen rows the sides above and below the edit moved. */
            int32_t  lead2 = 0, lead3 = 0; /**< Text column of a PS2 and a PS3 row. */
            int32_t  cap2 = 0, cap3 = 0; /**< Text width of a PS2 and a PS3 row. */
            int32_t  col_a = 0, off = 0; /**< The edit's column on row a, and offset in its text. */
        };

        /** @brief Read what @p removed bytes at @p pos, or @p text of @p len
         *  bytes inserted there, are about to do to the layout. */
        Edit _capture(uint16_t pos, uint16_t removed, const char* text, uint16_t len) const;
        /** @brief Last row of the line holding row @p i. */
        uint16_t _line_end(uint16_t i) const {
            while (i + 1 < _row_count && !_rows[i].hard_break) i++;
            return i;
        }
        /** @brief Screen row of layout row @p i, which may lie outside the viewport. */
        int32_t _now(int32_t i) const { return (int32_t)_origin + i - (int32_t)_view_top; }

        /** @brief Tell the terminal what the edit did to the screen. */
        void _announce(Edit& e);
        void _announce_plain(const Edit& e);
        void _announce_split(const Edit& e);
        void _announce_join(const Edit& e);
        /** @brief Announce the band the edit moved as one: old rows [0, @p T]
         *  moved `m0` screen rows and old rows [@p B, count) moved `mB`, the
         *  rows between gone or new. One band, the larger side when they
         *  disagree. */
        void _band(const Edit& e, int32_t T, int32_t B);
        /** @brief Announce that on screen row @p top what started at column
         *  @p from now starts at @p to, and the rows down to @p bottom followed
         *  at the PS2 column; @p tail cells right of the change on @p bottom. */
        void _ripple(int32_t top, int32_t bottom, int32_t from, int32_t to, int32_t tail);

        uint16_t _hw_row(uint16_t i) const {
            return (uint16_t)(_origin + (i - _view_top));
        }

        bool _visible(uint16_t i) const {
            return i >= _view_top && (uint16_t)(i - _view_top) < _view_height();
        }

        /** @brief One past the last layout row on screen. */
        uint16_t _visible_end() const {
            uint16_t e = (uint16_t)(_view_top + _view_height());
            return e > _row_count ? _row_count : e;
        }

        /** @brief Move the viewport over the cursor if it left it. */
        void _follow_cursor();

        /** @} */

        /** @name Cursor motion
         *  @{ */

        /** @brief What every motion ends with: a reconcile. Drops the goal
         *  column unless @p keep_goal. */
        void _settle_cursor(bool keep_goal = false) {
            if (!keep_goal) _goal_valid = false;
            _reconcile();
        }

        /** @brief Move @p delta display rows, holding the goal column; past
         *  the layout, to the buffer start / end. */
        void _move_row(int32_t delta);

        uint16_t _word_start() const;
        uint16_t _word_end() const;

        /** @} */

        /** @name Kill and yank
         *  @{ */

        void remove_characters(uint16_t len);

        /** @brief The region as an ordered half-open span.
         *  @return false, @p from and @p to untouched, with no mark. */
        bool region(uint16_t& from, uint16_t& to) const;
        bool kill_region();

        /** @brief Cut `[from, to)` into the kill ring and remove what it took.
         *  @return false if the span was empty or nothing fit. */
        bool _kill_span(uint16_t from, uint16_t to);

        /** @} */

        /** @name Painting
         *  @{ */

        /** @brief Paint every row of the viewport into FRONT: prompt, content,
         *  a clear to the margin; the clear alone past the text. */
        void _paint_view();

        /** @brief Invert the region's cells over the rows _paint_view() wrote.
         *  An EMPTY region inverts the one cell at the mark. */
        void _paint_region();

        /** @brief Declare the rows the editor occupies for the diff's order. */
        void _place_rows();

        /** @brief Home the terminal at @p p and declare it the visible
         *  cursor's place. */
        void _place_cursor(const CursorPos& p) {
            _terminal->move_to(p);
            _terminal->set_cursor_pos(p);
        }

        void _write_prompt(uint16_t row_idx);

        /** @} */

        /** @name Reconciliation
         *  @{ */

        /** @brief After an edit: origin, viewport, what moved, repaint,
         *  cursor. A motion or a load passes no edit, and only the viewport's
         *  move is announced. */
        void _reconcile(Edit e);
        void _reconcile() { _reconcile(Edit()); }

        /** @} */
    };

}}

#endif // CONSOLE_EDITOR_H
