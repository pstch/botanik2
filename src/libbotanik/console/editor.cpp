// REVIEW_35: TODO
/**
 * @file editor.cpp
 * @brief TextEditor - byte-efficient multi-row line editor. Each edit mutates
 *        the buffer, rebuilds the row layout (`_layout`), and reconciles it
 *        against the pre-edit layout (`_pre`): one IL/DL for the row-count
 *        change, then a forward per-row splice walk from the edit row.
 */
#include <cctype>
#include <cstdint>
#include <cstring>

#include "editor.h"
#include "console.h"


namespace Botanik { namespace Terminal {

    TextEditor::TextEditor(Console* console, const EditorStorage& store)
        : _console(console), _terminal(&console->terminal()),
          _buf(store.buf), _buf_cap(store.buf_cap) {
        _layout.rows = store.layout;
        _layout.cap  = store.max_rows;
        _pre.rows    = store.pre;
        _pre.cap     = store.max_rows;
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
        while (_layout.count < _layout.cap) {
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

    void TextEditor::_snapshot_pre() {
        _pre.count = _layout.count;
        if (_layout.count > 0)
            memcpy(_pre.rows, _layout.rows, (size_t)_layout.count * sizeof(RowView));
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
        _place_rows();          // a page switch handed the band back; reclaim it
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
        // One byte is reserved for the NUL, so usable content is _buf_cap-1:
        // the layout asserts size rows against the full buffer capacity, one
        // byte of slack.
        if (len == 0 || (uint32_t)_len + 1 >= _buf_cap) return;
        if ((uint32_t)_len + len >= _buf_cap) len = (uint16_t)(_buf_cap - _len - 1);
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

        _snapshot_pre();
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

        _snapshot_pre();
        _rebuild_layout();
        _reconcile(pos, len, false);
        _place_cursor(_map_cursor_pos(_pos));
    }

    // grow/shrink helpers
    // ------------------------------------------------------------------------

    void TextEditor::_place_rows() {
        uint16_t rows = _console->rows();
        if (rows == 0) return;
        uint16_t bottom = (uint16_t)(rows - 1);
        // Release the content band, then claim exactly the rows occupied now -
        // not everything below _origin, because the editor is not always at the
        // bottom of the screen (the PARAMS page edits a row mid-table, and the
        // rows under it belong to the table).
        //
        // Releasing the whole band is only safe while no page keeps a class of
        // its own on a row an editor shares a screen with. Nothing does today,
        // and a page re-declares in focus() anyway.
        _terminal->set_row_priority(1, bottom, PRIO_STRUCT);
        if (_layout.count == 0 || _origin > bottom) return;
        uint16_t last = (uint16_t)(_origin + _layout.count - 1);
        if (last > bottom) last = bottom;
        _terminal->set_row_priority(_origin, last, PRIO_INPUT);
    }

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

    // reconciliation helpers
    // ------------------------------------------------------------------------

    int32_t TextEditor::_edit_row_amount(uint16_t span_start, uint16_t span_len,
                                         bool is_insert, const RowView& post_row,
                                         const RowView& pre_row) {
        const RowView& row = is_insert ? post_row : pre_row;
        uint16_t row_end = (uint16_t)(row.buf_start + row.buf_len);
        uint16_t avail   = (uint16_t)(row_end - span_start);
        uint16_t head    = span_len < avail ? span_len : avail;
        return is_insert ? (int32_t)head : -(int32_t)head;
    }

    void TextEditor::_splice_row(uint16_t post_idx, uint16_t col, int32_t amount,
                                 uint16_t shown_len, uint16_t shown_prompt_l,
                                  const RowView& want) {
        uint16_t hw     = (uint16_t)(_origin + post_idx);
        uint16_t prompt = _prompt_len(_row_prompt(post_idx));
        uint16_t cap    = _row_cap(post_idx);

        // Prompt-class flip (PS2<->PS3): rewrite the gutter. A longer prompt's
        // push drops trailing content off the right edge, so shrink shown_len
        // accordingly before the length-align below.
        if (shown_prompt_l != prompt && hw < _console->rows()) {
            RowPrompt p = _row_prompt(post_idx);
            _terminal->move_to({hw, _indent});
            if (prompt <= shown_prompt_l) {
                _terminal->write_chars(_prompt_buf(p), prompt);
                _terminal->delete_chars((uint16_t)(shown_prompt_l - prompt));
            } else {
                _terminal->write_chars(_prompt_buf(p), prompt, /*insert=*/true);
                _terminal->delete_chars(shown_prompt_l);
                uint32_t total = (uint32_t)shown_prompt_l + prompt + shown_len;
                uint16_t cols  = _terminal->cols();
                if (total > cols) shown_len -= (uint16_t)(total - cols);
            }
        }

        // Splice `amount` content chars at `col`. Chars pushed off the right
        // edge reflow into the next row (prepended next iteration). Move only
        // when there is a splice - an absorbed row (amount 0, no length/prompt
        // change) emits nothing.
        if (amount > 0) {
            _terminal->move_to({hw, (uint16_t)(_indent + col)});
            _terminal->write_chars(&_buf[want.buf_start + (col - prompt)],
                                   (uint16_t)amount, /*insert=*/true);
        } else if (amount < 0) {
            _terminal->move_to({hw, (uint16_t)(_indent + col)});
            _terminal->delete_chars((uint16_t)(-amount));
        }

        // Length-align the tail. `after` is what the row shows post-splice; a
        // shrink here is what clears a row that just gained a hard break.
        int32_t  after_i = (int32_t)shown_len + amount;
        uint16_t after   = after_i < 0 ? 0 : (after_i > (int32_t)cap ? cap : (uint16_t)after_i);
        if (want.buf_len > after) {
            _terminal->move_to({hw, (uint16_t)(_indent + prompt + after)});
            _terminal->write_chars(&_buf[want.buf_start + after],
                                   (uint16_t)(want.buf_len - after));
        } else if (want.buf_len < after) {
            _terminal->move_to({hw, (uint16_t)(_indent + prompt + want.buf_len)});
            _terminal->clear_to_eol();
        }
    }

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

    TextEditor::SpliceSpan
    TextEditor::_find_splice_span(uint16_t span_start, uint16_t span_len,
                                  bool is_insert, int32_t l_shift,
                                  int32_t row_delta) const {
        // First affected row. Insert: the post owner (where the new bytes land
        // - a `\n` inserted at a soft-wrap boundary closes the row *above* the
        // pre owner). Remove: the pre owner (the deleted bytes lived there).
        uint16_t edit_row;
        if (is_insert) edit_row = _owner_row(span_start, _layout);
        else           edit_row = _owner_row(span_start, _pre);

        // Absorption boundary: longest bottom suffix of shifted-identical rows
        // (same buf_len + hard_break, buf_start displaced by exactly l_shift,
        // lying strictly past the edit). `absorbed_post` is the post index
        // where the tail is already correct; `absorbed_pre` the matching pre
        // index.
        uint16_t span_end_old;
        if (is_insert) span_end_old = span_start;
        else           span_end_old = (uint16_t)(span_start + span_len);

        uint16_t t = 0;
        while (t < _pre.count && t < _layout.count) {
            const RowView& pre = _pre.rows[_pre.count - 1 - t];
            const RowView& po  = _layout.rows[_layout.count - 1 - t];
            if (po.buf_len != pre.buf_len || po.hard_break != pre.hard_break) break;
            if ((int32_t)po.buf_start != (int32_t)pre.buf_start + l_shift) break;
            if (pre.buf_start < span_end_old) break;
            t++;
        }
        uint16_t absorbed_post = (uint16_t)(_layout.count - t);
        uint16_t absorbed_pre  = (uint16_t)((int32_t)absorbed_post - row_delta);
        return { edit_row, absorbed_post, absorbed_pre };
    }

    void TextEditor::_reposition_tail(const SpliceSpan& span, int32_t row_delta,
                                      uint16_t scr_rows) {
        // A single contiguous edit needs at most one IL/DL: it slides the
        // absorbed tail (and opens the blank rows inserted content paints
        // into). Nothing to do when there is no tail or no row-count change.
        if (span.absorbed_post >= _layout.count || row_delta == 0) return;

        uint16_t at;
        if (row_delta > 0) at = span.absorbed_pre;
        else               at = span.absorbed_post;
        uint16_t n = (uint16_t)(row_delta > 0 ? row_delta : -row_delta);

        uint16_t row = (uint16_t)(_origin + at);
        if (row >= scr_rows) return;
        // Slide the absorbed tail to its new rows (insert also opens the blank
        // rows the forward walk paints inserted content into). Bounded to
        // [row..content bottom] so rows below the editor are left untouched.
        // Grow (row_delta > 0) scrolls the tail down; shrink scrolls it up.
        _terminal->scroll_region(row, (uint16_t)(scr_rows - 1), /*up=*/row_delta < 0, n);
    }

    void TextEditor::_reconcile(uint16_t span_start, uint16_t span_len, bool is_insert) {
        // Keystroke echo has to feel immediate however busy the screen is. The
        // editor is bottom-pinned, so its extent moves: hand the whole content
        // band back to the page first, then claim the rows it actually occupies
        // now. Otherwise rows the editor has grown past keep its class forever.
        _place_rows();
        int32_t l_shift;
        if (is_insert) l_shift = (int32_t)span_len;
        else           l_shift = -(int32_t)span_len;

        int32_t  row_delta = (int32_t)_layout.count - (int32_t)_pre.count;
        uint16_t scr_rows  = _console->rows();

        // Scroll up if the editor grew, so the added rows fit.
        uint16_t new_origin = _grow_editor();

        // Find the edit row and the absorbed tail: the bottom run of rows that
        // the edit only shifted, needing the IL/DL below but no per-row work.
        SpliceSpan span = _find_splice_span(span_start, span_len, is_insert,
                                            l_shift, row_delta);

        // One IL/DL slides that tail to its new rows (and, on insert, opens the
        // blank rows the forward walk paints the new content into).
        _reposition_tail(span, row_delta, scr_rows);

        // Edit-row splice geometry, computed once (the walk's first row is the
        // edit row): the cursor column, and the signed count of content chars
        // the edit adds/removes on that row.
        const RowView& edit_post = _layout.rows[span.edit_row];
        const RowView& edit_pre  = _pre.rows[span.edit_row];
        uint16_t edit_col    = (uint16_t)(_prompt_len(_row_prompt(span.edit_row))
                                          + (span_start - edit_post.buf_start));
        int32_t  edit_amount = _edit_row_amount(span_start, span_len, is_insert,
                                                edit_post, edit_pre);

        // Forward walk from the edit row. Each row maps to the pre row now
        // physically present (or is fresh: an IL-opened blank, or a row whose
        // old glyphs are unrelated to the wanted content).
        for (uint16_t i = span.edit_row; i < _layout.count; i++) {
            const RowView& want  = _layout.rows[i];
            uint16_t want_prompt = _prompt_len(_row_prompt(i));

            // Map post row i to the pre row physically present after the IL/DL.
            // Rows in the IL-opened gap [absorbed_pre, absorbed_post) have no
            // pre image (has_pre false) and drop through to the repaint below.
            // The gap is empty unless the edit grew the row count.
            bool     has_pre = !(i >= span.absorbed_pre && i < span.absorbed_post);
            uint16_t pre_idx = (i < span.absorbed_post)
                ? i : (uint16_t)((int32_t)i - row_delta);
            const RowView& shown  = _pre.rows[pre_idx];
            uint16_t shown_prompt = _prompt_len(_row_prompt_in(_pre, pre_idx));

            // How far the present pre row's content slid, and whether the
            // displaced slice still overlaps the wanted one (only meaningful
            // when has_pre - the conditions below gate on it).
            int32_t ss = (int32_t)shown.buf_start + l_shift;
            int32_t sl = (int32_t)shown.buf_len;
            int32_t ws = (int32_t)want.buf_start;
            int32_t wl = (int32_t)want.buf_len;

            if (has_pre && i == span.edit_row)
                // Edit row: splice the inserted/removed run at the cursor.
                _splice_row(i, edit_col, edit_amount, shown.buf_len, shown_prompt, want);
            else if (has_pre && sl == 0 && wl == 0)
                // Both rows empty: nothing to write but a possible prompt flip.
                _splice_row(i, want_prompt, 0, 0, shown_prompt, want);
            else if (has_pre && (ws < ss + sl) && (ss < ws + wl))
                // Overlap: cheap left splice by the displacement, cascading the
                // pushed-off char into the next row (the next iteration
                // prepends).
                _splice_row(i, want_prompt, ss - ws, shown.buf_len, shown_prompt, want);
            else
                // Fresh blank (no pre image) or content reflowed to unrelated
                // columns: a full repaint is byte-optimal either way.
                _new_row(i);
        }

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
