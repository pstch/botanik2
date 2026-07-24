// REVIEW_35: TODO
/**
 * @file editor.cpp
 * @brief TextEditor - byte-efficient multi-row line editor. Each edit mutates
 *        the buffer, rebuilds the row layout (`_layout`), and reconciles the
 *        screen: one IL/DL when the row count changed (compared against `_pre`),
 *        then a plain repaint from the edit row down. The terminal's back-buffer
 *        diff turns that repaint into only the cells that actually moved.
 */
#include <cctype>
#include <cstdint>
#include <cstring>

#include "libbotanik/console/editor.h"
#include "libbotanik/console/console.h"


namespace Botanik { namespace Terminal {

    TextEditor::TextEditor(Console* console)
        : _console(console), _terminal(&console->terminal()) {
        _buf[0] = '\0';
        _layout.rows[0] = {0, 0, false};
        _layout.count = 1;
        // Auto-wrap is owned by the terminal (asserted on every screen
        // establish).
    }

    // prompt helpers

    RowPrompt TextEditor::_row_prompt(uint16_t row_idx) const {
        return _row_prompt_in(_layout, row_idx);
    }

    uint16_t TextEditor::_row_cap(uint16_t row_idx) const {
        uint16_t cols = _terminal->cols();
        uint16_t used = (uint16_t)(_prompt_len(_row_prompt(row_idx)) + _indent);
        return cols > used ? (uint16_t)(cols - used) : 1;
    }

    void TextEditor::_write_prompt(uint16_t row_idx) {
        RowPrompt p = _row_prompt(row_idx);
        _terminal->write_chars(_prompt_buf(p), _prompt_len(p));
    }

    // layout scanning
    // ------------------------------------------------------------------------

    RowView TextEditor::_scan_row(uint16_t pos, uint16_t cap) const {
        uint16_t len = 0;
        while (len < cap && (pos + len) < _len && _buf[pos + len] != '\n') len++;
        bool hard_break = (pos + len < _len) && _buf[pos + len] == '\n';
        return RowView { pos, len, hard_break };
    }

    void TextEditor::_rebuild_layout() {
        uint16_t pos = 0;
        _layout.count = 0;
        while (_layout.count < Config::EDITOR_MAX_ROWS) {
            uint16_t cap = _row_cap(_layout.count);
            RowView  row = _scan_row(pos, cap);
            _layout.rows[_layout.count++] = row;
            uint16_t stride = row.buf_len + (row.hard_break ? 1 : 0);
            if (stride == 0) break;
            pos += stride;
            if (pos >= _len && !row.hard_break) break;
            if (pos > _len) break;
        }
    }

    CursorPos TextEditor::_map_cursor_pos(uint16_t pos) const {
        uint16_t i      = _owner_row(pos, _layout);
        uint16_t prompt = _prompt_len(_row_prompt(i));
        uint16_t col    = (uint16_t)(_indent + prompt + (pos - _layout.rows[i].buf_start));
        return { (uint16_t)(_origin + i), col };
    }

    // lifecycle
    // ------------------------------------------------------------------------

    void TextEditor::_reset_state() {
        _len = _pos = 0;
        _buf[0]    = '\0';
        _origin    = (uint16_t)(_console->rows() - 1);
        _layout.rows[0]   = {0, 0, false};
        _layout.count = 1;
    }

    void TextEditor::reset() {
        // Bottom-anchored: rows run from _origin to the content bottom. Clear
        // exactly that band (clear_rows leaves the alarm bar below it
        // untouched).
        _terminal->clear_rows(_origin, (uint16_t)(_console->rows() - 1));
        uint16_t old_origin = _origin;
        _reset_state();
        uint16_t draw_row = _suppress_sync_redraw ? old_origin : _origin;
        _terminal->move_to({draw_row, _indent});
        _write_prompt(0);
        if (!_suppress_sync_redraw)
            _place_cursor(_map_cursor_pos(0));
    }

    void TextEditor::commit() {
        _reset_state();
        _terminal->move_to({_origin, _indent});
        _write_prompt(0);
        _place_cursor(_map_cursor_pos(0));
    }

    void TextEditor::set_origin(uint16_t row) {
        uint16_t max_row = (uint16_t)(_console->rows() - 1);
        _origin = row > max_row ? max_row : row;
    }

    // redraw
    // ------------------------------------------------------------------------

    void TextEditor::redraw() {
        // Destination rows assumed already blank; only write prompt + content.
        for (uint16_t i = 0; i < _layout.count; i++) {
            uint16_t hw_row = _origin + i;
            if (hw_row >= _console->rows()) break;
            _terminal->move_to({hw_row, _indent});
            _write_prompt(i);
            if (_layout.rows[i].buf_len > 0)
                _terminal->write_chars(&_buf[_layout.rows[i].buf_start], _layout.rows[i].buf_len);
        }
        _place_cursor(_map_cursor_pos(_pos));
    }

    // editing
    // ------------------------------------------------------------------------

    void TextEditor::insert_characters(const char* buf, uint16_t len) {
        // One byte is reserved for the NUL, so usable content is
        // sizeof(_buf)-1: the layout asserts size rows against the full
        // SHELL_INPUT_LEN, one byte of slack.
        if (len == 0 || (size_t)_len + 1 >= sizeof(_buf)) return;
        if ((uint32_t)_len + len >= sizeof(_buf)) len = (uint16_t)(sizeof(_buf) - _len - 1);
        if (len == 0) return;

        if (_single_line) {
            // Refuse newlines (stop at the first) and any content past the row
            // end, so the editor never wraps to a second row.
            for (uint16_t i = 0; i < len; i++)
                if (buf[i] == '\n') { len = i; break; }
            uint16_t cap = _row_cap(0);
            if (_len >= cap) return;
            if ((uint16_t)(_len + len) > cap) len = (uint16_t)(cap - _len);
            if (len == 0) return;
        }

        uint16_t pos = _pos;

        if (_pos < _len) memmove(&_buf[_pos + len], &_buf[_pos], _len - _pos);
        memcpy(&_buf[_pos], buf, len);
        _len += len;
        _buf[_len] = '\0';
        _pos += len;

        _pre = _layout;
        _rebuild_layout();
        _reconcile(pos, len, /*is_insert=*/true);
        _place_cursor(_map_cursor_pos(_pos));
    }

    void TextEditor::remove_characters(uint16_t len) {
        if (len == 0 || _pos >= _len) return;
        if (_pos + len > _len) len = (uint16_t)(_len - _pos);

        uint16_t pos = _pos;
        uint16_t safe_len = (uint16_t)(_len - (_pos + len));

        if (safe_len > 0) memmove(&_buf[_pos], &_buf[_pos + len], safe_len);
        _len -= len;
        _buf[_len] = '\0';

        _pre = _layout;
        _rebuild_layout();
        _reconcile(pos, len, false);
        _place_cursor(_map_cursor_pos(_pos));
    }

    // grow/shrink helpers
    // ------------------------------------------------------------------------

    uint16_t TextEditor::_grow_editor() {
        // Bottom-pinned origin: pull the top up by the rows the editor grew,
        // clamped at the screen top.
        uint16_t scr_rows = _console->rows();
        uint16_t new_origin;
        if (scr_rows >= _layout.count) new_origin = (uint16_t)(scr_rows - _layout.count);
        else                           new_origin = 0;

        if (new_origin < _origin) {
            // Scroll the content band (rows 1..bottom) up by the grown row
            // count so the editor's added rows fit; bounded so the status bar
            // (row 0) - and any reserved bottom row - stay put.
            _terminal->scroll_region(1, (uint16_t)(scr_rows - 1), /*up=*/true,
                                     (uint16_t)(_origin - new_origin));
            _origin = new_origin;
        }
        return new_origin;
    }

    void TextEditor::_shrink_editor(uint16_t new_origin) {
        if (new_origin <= _origin) return;
        // Under suppress the caller repaints, so only move the origin.
        if (!_suppress_sync_redraw && _origin < _console->rows()) {
            // Open blank rows at the old origin to push the editor back down to
            // the bottom; bounded to [_origin..content bottom] so it never
            // spills below.
            _terminal->scroll_region(_origin, (uint16_t)(_console->rows() - 1),
                                     /*up=*/false, (uint16_t)(new_origin - _origin));
        }
        _origin = new_origin;
    }

    // reconciliation
    // ------------------------------------------------------------------------

    void TextEditor::_new_row(uint16_t post_idx) {
        uint16_t hw = (uint16_t)(_origin + post_idx);
        if (hw >= _console->rows()) return;
        const RowView& want = _layout.rows[post_idx];
        _terminal->move_to({hw, _indent});
        _write_prompt(post_idx);
        if (want.buf_len > 0)
            _terminal->write_chars(&_buf[want.buf_start], want.buf_len);
        _terminal->clear_to_eol();
    }

    void TextEditor::_slide_tail(uint16_t edit_row, int32_t row_delta,
                                 uint16_t scr_rows) {
        if (row_delta == 0) return;
        // Nothing below the edit row to move - the row the edit added (or removed)
        // is the bottom one - so an IL/DL here would buy nothing and cost 4 bytes.
        if (_pre.count <= (uint16_t)(edit_row + 1)) return;
        uint16_t at = (uint16_t)(_origin + edit_row + 1);
        if (at >= scr_rows) return;
        uint16_t n = (uint16_t)(row_delta > 0 ? row_delta : -row_delta);
        // Grow scrolls the rows below the edit down, opening the n blank rows the
        // repaint then fills; shrink scrolls them up, closing the n it vacated.
        // Bounded to [at..content bottom], so the rows above the edit stay put.
        _terminal->scroll_region(at, (uint16_t)(scr_rows - 1), /*up=*/row_delta < 0, n);
    }

    void TextEditor::_reconcile(uint16_t span_start, uint16_t span_len, bool is_insert) {
        int32_t  row_delta = (int32_t)_layout.count - (int32_t)_pre.count;
        uint16_t scr_rows  = _console->rows();

        // Bottom-pinned grow first, so the added rows fit. It also normalises what
        // follows: whichever way the origin moved, pre row p now sits at post
        // index p relative to the new origin.
        uint16_t new_origin = _grow_editor();

        // First row the edit can touch; everything above it is untouched by
        // construction. Insert: the post owner (a `\n` inserted at a soft-wrap
        // boundary closes the row *above* the pre owner). Remove: the pre owner
        // (the deleted bytes lived there).
        uint16_t edit_row = is_insert ? _owner_row(span_start, _layout)
                                      : _owner_row(span_start, _pre);

        // The one thing the cell diff cannot deduce for itself: when the edit
        // changes the row count, every row below it slides. One IL/DL moves them,
        // and that is the whole byte win here - without it the diff would find
        // every row below the edit different and rewrite the entire tail.
        _slide_tail(edit_row, row_delta, scr_rows);

        // Then simply redraw from the edit row down. A row the IL/DL already put
        // in the right place redraws to identical cells, so the diff emits nothing
        // for it. That is what replaced the old absorbed-tail search and per-row
        // splicing: the back-buffer works it out per cell, for free, where the
        // splice could only ever express one contiguous shift per row.
        (void)span_len;
        for (uint16_t i = edit_row; i < _layout.count; i++) _new_row(i);

        _shrink_editor(new_origin);
    }


    // cursor motion
    // ------------------------------------------------------------------------

    void TextEditor::move_left() {
        if (_pos == 0) return;
        _pos--;
        _place_cursor(_map_cursor_pos(_pos));
    }

    void TextEditor::move_right() {
        if (_pos >= _len) return;
        _pos++;
        _place_cursor(_map_cursor_pos(_pos));
    }

    void TextEditor::move_to_line_start() {
        while (_pos > 0 && _buf[_pos - 1] != '\n') _pos--;
        _place_cursor(_map_cursor_pos(_pos));
    }

    void TextEditor::move_to_line_end() {
        while (_pos < _len && _buf[_pos] != '\n') _pos++;
        _place_cursor(_map_cursor_pos(_pos));
    }

    void TextEditor::backspace(uint16_t count) {
        if (count == 0 || _pos == 0) return;
        if (count > _pos) count = _pos;
        _pos -= count;
        remove_characters(count);
    }

    void TextEditor::delete_forward(uint16_t count) {
        if (count == 0 || _pos >= _len) return;
        uint16_t avail = (uint16_t)(_len - _pos);
        if (count > avail) count = avail;
        remove_characters(count);
    }

    static inline bool _is_word_char(char c) {
        unsigned char uc = (unsigned char)c;
        return isalnum(uc) || c == '_';
    }

    void TextEditor::move_word_left() {
        while (_pos > 0 && !_is_word_char(_buf[_pos - 1])) _pos--;
        while (_pos > 0 &&  _is_word_char(_buf[_pos - 1])) _pos--;
        _place_cursor(_map_cursor_pos(_pos));
    }

    void TextEditor::move_word_right() {
        while (_pos < _len &&  _is_word_char(_buf[_pos])) _pos++;
        while (_pos < _len && !_is_word_char(_buf[_pos])) _pos++;
        _place_cursor(_map_cursor_pos(_pos));
    }

}}
