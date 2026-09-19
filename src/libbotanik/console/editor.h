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

#include "libbotanik/console/parser.h"
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
        BOTTOM, /**< Last row on the content bottom, growing upward: the shell. */
        TOP,    /**< Origin fixed, a viewport over the text: NOTES. */
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
     * @brief Multi-row line editor. An edit relays out the buffer, announces
     *        what the layout did (a band of rows, a ripple of cells) and
     *        repaints the viewport. An insert past the row cap is refused.
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

        /** @brief Column vertical motion aims for; any other motion drops it. */
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

        /** @brief Over the caller-owned buffers in @p store, which outlive it. */
        TextEditor(Console* console, const EditorStorage& store);

        /** @brief Discard the text and show an empty prompt. Marks dirty. */
        void reset();

        /** @brief Start a fresh line after a submitted one; the rows the text
         *  gave up are the owner's to repaint. */
        void commit();

        /** @brief Repaint the viewport from the buffer, blank rows included. */
        void redraw();

        /** @} */

        /** @name Embedding
         *  How an owner places the editor: once, before the first redraw, and
         *  again whenever its rows move. @{ */

        /** @brief The prompt on the first row, on a wrapped continuation and
         *  after a hard break; NUL-terminated, EDITOR_PROMPT_MAX at most. They
         *  may differ in width. */
        void set_prompts(const char* ps1, const char* ps2, const char* ps3);

        /** @brief A prompt: the last row sits on @p bottom_row and the text
         *  grows upward from it. The shell. */
        void as_prompt(uint16_t bottom_row);

        /** @brief A viewport of @p rows rows from @p top_row, scrolling over
         *  the text. NOTES. */
        void as_viewport(uint16_t top_row, uint16_t rows);

        /** @brief One unprompted line on @p row after a caller-drawn label
         *  @p indent columns wide: newlines and wrapping are refused. */
        void as_field(uint16_t row, uint16_t indent);

        /** @} */

        /** @name Geometry
         *  @{ */

        /** @brief Screen position one past the last character. */
        CursorPos end_pos() const { return _map_cursor_pos(_len); }

        /** @brief Home the terminal at the edit point and declare it the
         *  visible cursor's place; a page drawing around the editor calls it last. */
        void place_cursor() { _place_cursor(_map_cursor_pos(_pos)); }

        uint16_t occupied_rows() const { return _row_count; }

        /** @brief Layout row @p i as painted, prompt then text, into @p out.
         *  @return the bytes written, at most @p cap. */
        uint16_t row_text(uint16_t i, char* out, uint16_t cap) const;

        /** @} */

        /** @name Editing
         *  @{ */

        void insert_characters(const char* buf, uint16_t len);

        /** @brief What a key did, for an owner that repaints or re-filters on it. */
        enum class Key : uint8_t { IGNORED, MOVED, CHANGED };

        /** @brief What @p cmd would do, without doing it: motion, deletion,
         *  kill and yank, mark and region are the editor's keys; vertical
         *  motion is not, single-line. Enter, Ctrl-U and whatever else an
         *  owner binds are the owner's. */
        Key key_kind(InputEvent::Command cmd) const;

        /** @brief Apply an editing key. @param count repeats, for a coalesced
         *  BACKSPACE or DELETE. @return key_kind(@p cmd). */
        Key handle_key(InputEvent::Command cmd, uint16_t count = 1);

        /** @} */

        /** @name Bulk content
         *  @{ */

        /** @brief Buffer to read stored text straight into; pair with commit_loaded(). */
        char*    load_buffer()         { return _buf; }
        uint16_t load_capacity() const { return (uint16_t)(_buf_cap - 1); }

        /** @brief Adopt @p len bytes written into load_buffer(), cursor at
         *  the start, not dirty. @return false if a tail was dropped. */
        bool commit_loaded(uint16_t len);

        /** @brief Replace the whole text, cursor at the end, dirty. @p text
         *  may be a slice of the buffer itself. */
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
        /** @name Editing keys
         *  What handle_key() dispatches to. None changes the buffer unless it
         *  says so; each ends in a reconcile. @{ */

        /** @brief Delete @p count chars before / at the cursor, one reconcile. */
        void backspace(uint16_t count = 1);
        void delete_forward(uint16_t count = 1);

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

        /** @brief Over the console-wide kill ring; a kill takes only what the
         *  ring holds. Each returns false when there was nothing to do. */
        bool kill_to_line_end();
        bool kill_word_back();
        bool yank();

        /** @brief The span from the mark to the cursor; any edit drops the mark. */
        void set_mark();
        void clear_mark();
        void toggle_mark();
        bool copy_region();
        /** @brief The region when there is one, else kill_word_back(). */
        bool kill_region_or_word();

        /** @} */

        /** @name Prompts
         *  @{ */

        /** @brief Pin the viewport's top row, clamped to the screen. */
        void _set_origin(uint16_t row);

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

        /** @brief Relay out the buffer. */
        bool _relayout_overflowed();

        /** @brief Make @p len bytes of @p text (a slice of the buffer, or null)
         *  the whole buffer, cursor at @p cursor, laid out, not reconciled.
         *  @return false if a tail was dropped. */
        bool _load(const char* text, uint16_t len, uint16_t cursor);

        /** @brief Row holding offset @p pos: a `\n` belongs to its row, a wrap
         *  boundary to the continuation row, the end to the last row. */
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

        /** @brief The height set, else down to the content bottom. */
        uint16_t _view_height() const;
        /** @brief Last screen row the editor may paint. */
        uint16_t _view_bottom_hw() const;

        /** @brief One edit: what the layout before it said about it, and
         *  what _complete() works out once the new layout is in. */
        struct Edit {
            enum Kind : uint8_t {
                NONE,    /**< A motion: only the viewport moved. */
                LOAD,    /**< The text was replaced: nothing on screen moved. */
                PLAIN,   /**< Bytes with no newline: the line rippled. */
                SPLIT,   /**< One newline in: a row split at the edit point. */
                JOIN,    /**< One newline out: two lines became one. */
                OTHER,   /**< Newlines crossed: only the rows after moved as one. */
            };

            /** @name Captured by _capture(), off the layout the edit met
             *  @{ */
            Kind     kind = NONE;
            uint16_t pos = 0;            /**< Buffer offset of the edit. */
            int16_t  delta = 0;          /**< Bytes added; negative for removed. */
            uint16_t a = 0, L = 0;       /**< Row holding pos, and the last row of
                                          *   the last line the edit touched. */
            RowView  row_a, row_b, row_L;/**< Rows a, a+1 and L before the edit. */
            uint16_t ps_a = 0;           /**< Prompt width row a had. */
            uint16_t count = 0;          /**< Rows before the edit. */
            uint16_t origin_was = 0, view_top_was = 0;
            /** @} */

            /** @name Worked out by _complete(), off the layout the edit left
             *  @{ */
            int32_t  d = 0;              /**< Rows the edit added. */
            int32_t  m0 = 0, mB = 0;     /**< Screen rows the sides above and below the edit moved. */
            int32_t  col_a = 0, off = 0; /**< The edit's column on row a, and offset in its text. */
            /** @} */

            /** @brief Screen row layout row @p i had before the edit. */
            int32_t was(int32_t i) const { return (int32_t)origin_was + i - (int32_t)view_top_was; }
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
        /** @brief Text column, and text width, of a row with prompt @p p. */
        int32_t _lead(RowPrompt p) const { return _indent + _prompt_len(p); }
        int32_t _cap(RowPrompt p) const;

        /** @brief Fill in the half of @p e that only the layout after the
         *  edit can answer. */
        void _complete(Edit& e) const;
        /** @brief Tell the terminal what the edit did to the screen. */
        void _announce(const Edit& e);
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

        uint16_t _hw_row(uint16_t i) const { return (uint16_t)_now(i); }

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

        /** @brief A reconcile, dropping the goal column unless @p keep_goal. */
        void _settle_cursor(bool keep_goal = false) {
            if (!keep_goal) _goal_valid = false;
            _reconcile();
        }

        /** @brief @p delta rows, holding the goal column; past the layout,
         *  to the buffer's start or end. */
        void _move_row(int32_t delta);

        uint16_t _word_start() const;
        uint16_t _word_end() const;

        /** @} */

        /** @name Kill and yank
         *  @{ */

        void remove_characters(uint16_t len);

        /** @brief The region as an ordered half-open span; false with no mark. */
        bool region(uint16_t& from, uint16_t& to) const;
        bool kill_region();

        /** @brief Cut `[from, to)` into the ring and remove what it took. */
        bool _kill_span(uint16_t from, uint16_t to);

        /** @} */

        /** @name Painting
         *  @{ */

        /** @brief Every viewport row into FRONT: prompt, text, clear to the margin. */
        void _paint_view();
        /** @brief Invert the region's cells; an empty region inverts the mark's cell. */
        void _paint_region();
        /** @brief Declare the editor's rows' priority. */
        void _place_rows();
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
        void _reconcile(Edit& e);
        void _reconcile(Edit::Kind k = Edit::NONE) { Edit e; e.kind = k; _reconcile(e); }

        /** @} */
    };

}}

#endif // CONSOLE_EDITOR_H
