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
        uint16_t pos  = 0;
        bool     done = false;
        _layout.count = 0;
        while (_layout.count < _layout.cap) {
            uint16_t cap = _row_cap(_layout.count);
            RowView  row = _scan_row(pos, cap);
            _layout.rows[_layout.count++] = row;
            uint16_t stride = row.buf_len + (row.hard_break ? 1 : 0);
            if (stride == 0) { done = true; break; }
            pos += stride;
            if (pos >= _len && !row.hard_break) { done = true; break; }
            if (pos > _len) { done = true; break; }
        }
        // Ran out of rows before running out of buffer: whatever follows cannot
        // be laid out, so it cannot be shown or reached.
        _layout_overflow = !done;
    }

    void TextEditor::_snapshot_pre() {
        _pre.count     = _layout.count;
        _pre_view_top  = _view_top;
        _pre_view_rows = _view_height();
        if (_layout.count > 0)
            memcpy(_pre.rows, _layout.rows, (size_t)_layout.count * sizeof(RowView));
    }

    CursorPos TextEditor::_map_cursor_pos(uint16_t pos) const {
        uint16_t i      = _owner_row(pos, _layout);
        uint16_t prompt = _prompt_len(_row_prompt(i));
        uint16_t col    = (uint16_t)(_indent + prompt + (pos - _layout.rows[i].buf_start));
        return { _hw_row(i), col };
    }

    // viewport
    // ------------------------------------------------------------------------

    uint16_t TextEditor::_console_rows() const { return _console->rows(); }

    uint16_t TextEditor::_view_bottom_hw() const {
        uint16_t rows = _console_rows();
        if (rows == 0) return 0;
        uint16_t bottom = (uint16_t)(rows - 1);
        uint16_t h      = _view_height();
        if (h == 0) return bottom;
        uint16_t last = (uint16_t)(_origin + h - 1);
        return last < bottom ? last : bottom;
    }

    bool TextEditor::_follow_cursor() {
        // Bottom-anchored, the text moves the screen rather than the other way
        // round: _grow_editor scrolls the whole content band so the prompt stays
        // put. Scrolling a window over the text as well would double up.
        if (_anchor != Anchor::TOP) return false;
        uint16_t h = _view_height();
        if (h == 0) return false;
        uint16_t row = _owner_row(_pos, _layout);

        uint16_t want = _view_top;
        if (row < _view_top)                       want = row;
        else if (row >= (uint16_t)(_view_top + h)) want = (uint16_t)(row - h + 1);

        // Never scroll past the end: blank rows below the text while more of it
        // sits above the window is just lost screen.
        uint16_t max_top = _layout.count > h ? (uint16_t)(_layout.count - h) : 0;
        if (want > max_top) want = max_top;
        if (want == _view_top) return false;

        int32_t  delta = (int32_t)want - (int32_t)_view_top;
        uint16_t n     = (uint16_t)(delta > 0 ? delta : -delta);
        _view_top = want;

        uint16_t bottom = _view_bottom_hw();
        if (n >= h) {
            // Nothing on screen survives the jump; a scroll would cost an
            // escape and still leave every row to repaint.
            _terminal->clear_rows(_origin, bottom);
            redraw();
            return true;
        }

        // Slide what stays, then paint only what the scroll uncovered.
        _terminal->scroll_region(_origin, bottom, /*up=*/delta > 0, n);
        uint16_t first = delta > 0 ? (uint16_t)(_view_top + h - n) : _view_top;
        uint16_t stop  = (uint16_t)(first + n);
        if (stop > _layout.count) stop = _layout.count;
        for (uint16_t i = first; i < stop; i++) _new_row(i);
        return true;
    }

    // lifecycle
    // ------------------------------------------------------------------------

    void TextEditor::_reset_state() {
        _len = _pos = 0;
        _buf[0]    = '\0';
        // Bottom-anchored, the origin is derived, so re-pin it. Top-anchored it
        // is the caller's (set_origin()) and must survive a reset.
        if (_anchor == Anchor::BOTTOM)
            _origin = (uint16_t)(_console_rows() - 1);
        _view_top   = 0;
        _goal_valid = false;
        _layout.rows[0]   = {0, 0, false};
        _layout.count = 1;
        _layout_overflow  = false;
    }

    void TextEditor::reset() {
        // Clear exactly the band the editor occupies (clear_rows leaves the
        // alarm bar below it untouched).
        _terminal->clear_rows(_origin, _view_bottom_hw());
        uint16_t old_origin = _origin;
        _reset_state();
        _dirty = true;              // reset() discards content; commit() does not
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
        uint16_t last = (uint16_t)(_view_top + _view_height());
        if (last > _layout.count) last = _layout.count;
        for (uint16_t i = _view_top; i < last; i++) {
            _terminal->move_to({_hw_row(i), _indent});
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

        if (_layout_overflow) {
            // More rows than the layout can hold: the tail would be text the
            // operator can neither see nor reach. Refuse the keystroke instead,
            // the way _single_line refuses one past the row end.
            memmove(&_buf[pos], &_buf[pos + len], (size_t)(_len - (pos + len)));
            _len -= len;
            _buf[_len] = '\0';
            _pos = pos;
            _rebuild_layout();
            return;
        }

        _dirty = true;
        _goal_valid = false;
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
        _dirty = true;
        _goal_valid = false;
        _reconcile(pos, len, false);
        _place_cursor(_map_cursor_pos(_pos));
    }

    bool TextEditor::load(const char* buf, uint16_t len) {
        bool fitted = true;
        if (len > (uint16_t)(_buf_cap - 1)) { len = (uint16_t)(_buf_cap - 1); fitted = false; }
        if (len > 0) memcpy(_buf, buf, len);
        _len = len;
        _buf[_len] = '\0';
        _pos = _view_top = 0;
        _goal_valid = false;
        _rebuild_layout();

        // Drop what the row cap could not lay out rather than carry text the
        // operator cannot see: an invisible tail that a later save writes back
        // truncated is the worse failure.
        if (_layout_overflow) {
            const RowView& last = _layout.rows[_layout.count - 1];
            _len = (uint16_t)(last.buf_start + last.buf_len);
            _buf[_len] = '\0';
            _rebuild_layout();
            fitted = false;
        }

        _dirty = false;
        _terminal->clear_rows(_origin, _view_bottom_hw());
        redraw();
        return fitted;
    }

    // grow/shrink helpers
    // ------------------------------------------------------------------------

    void TextEditor::_place_rows() {
        uint16_t rows = _console_rows();
        if (rows == 0) return;

        if (_anchor == Anchor::TOP) {
            // The viewport is fixed and the editor owns all of it, so claim
            // exactly that and leave the rest of the band alone. Releasing the
            // whole band here would demote a row the page keeps for itself (a
            // status line under the editor) on every keystroke.
            uint16_t view_bottom = _view_bottom_hw();
            if (_origin > view_bottom) return;
            _terminal->set_row_priority(_origin, view_bottom, PRIO_INPUT);
            return;
        }

        // Bottom-anchored the extent moves as the text grows, so release the
        // content band and reclaim exactly the rows occupied now - not
        // everything below _origin, since the rows under a mid-screen editor
        // belong to whoever drew them.
        //
        // Releasing the whole band is only safe while no page keeps a class of
        // its own on a row a bottom-anchored editor shares a screen with.
        // Nothing does today, and a page re-declares in focus() anyway.
        uint16_t bottom = (uint16_t)(rows - 1);
        _terminal->set_row_priority(1, bottom, PRIO_STRUCT);
        if (_layout.count == 0 || _origin > bottom) return;
        uint16_t last = (uint16_t)(_origin + _layout.count - 1);
        if (last > bottom) last = bottom;
        _terminal->set_row_priority(_origin, last, PRIO_INPUT);
    }

    uint16_t TextEditor::_grow_editor() {
        // Top-anchored, growing the text never moves the editor: the viewport
        // scrolls over it instead (_follow_cursor).
        if (_anchor == Anchor::TOP) return _origin;

        // Bottom-pinned origin: pull the top up by the rows the editor grew,
        // clamped at the screen top.
        uint16_t scr_rows = _console_rows();
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
        if (_anchor == Anchor::TOP) return;
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
        if (!_visible(post_idx)) return;
        uint16_t hw     = _hw_row(post_idx);
        uint16_t prompt = _prompt_len(_row_prompt(post_idx));
        uint16_t cap    = _row_cap(post_idx);

        // Prompt-class flip (PS2<->PS3): rewrite the gutter. A longer prompt's
        // push drops trailing content off the right edge, so shrink shown_len
        // accordingly before the length-align below.
        if (shown_prompt_l != prompt) {
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
        if (!_visible(post_idx)) return;
        const RowView& want = _layout.rows[post_idx];
        _terminal->move_to({_hw_row(post_idx), _indent});
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

    void TextEditor::_reposition_tail(const SpliceSpan& span, int32_t row_delta) {
        // A single contiguous edit needs at most one IL/DL: it slides the
        // absorbed tail (and opens the blank rows inserted content paints
        // into). Nothing to do when there is no tail or no row-count change.
        if (span.absorbed_post >= _layout.count || row_delta == 0) return;

        uint16_t at;
        if (row_delta > 0) at = span.absorbed_pre;
        else               at = span.absorbed_post;
        uint16_t n = (uint16_t)(row_delta > 0 ? row_delta : -row_delta);

        // The tail begins at or below the edit row, and the edit row is the
        // cursor's, which the viewport always contains - so `at` is normally
        // inside it. Clamp anyway rather than scroll from a negative row.
        uint16_t bottom = _view_bottom_hw();
        uint16_t row    = at >= _view_top ? _hw_row(at) : _origin;
        if (row > bottom) return;               // tail lies entirely below the view
        // Slide the absorbed tail to its new rows (insert also opens the blank
        // rows the forward walk paints inserted content into). Bounded to the
        // viewport, so nothing outside the editor moves. Grow (row_delta > 0)
        // scrolls the tail down; shrink scrolls it up.
        _terminal->scroll_region(row, bottom, /*up=*/row_delta < 0, n);
    }

    void TextEditor::_reconcile(uint16_t span_start, uint16_t span_len, bool is_insert) {
        // Keystroke echo has to feel immediate however busy the screen is, so
        // re-declare which rows are input rows before touching any of them.
        // Bottom-anchored the extent moves with the text, and rows the editor
        // has grown past would otherwise keep its class forever.
        _place_rows();
        int32_t l_shift;
        if (is_insert) l_shift = (int32_t)span_len;
        else           l_shift = -(int32_t)span_len;

        int32_t  row_delta = (int32_t)_layout.count - (int32_t)_pre.count;

        // Scroll up if the editor grew, so the added rows fit.
        uint16_t new_origin = _grow_editor();

        // Find the edit row and the absorbed tail: the bottom run of rows that
        // the edit only shifted, needing the IL/DL below but no per-row work.
        SpliceSpan span = _find_splice_span(span_start, span_len, is_insert,
                                            l_shift, row_delta);

        // One IL/DL slides that tail to its new rows (and, on insert, opens the
        // blank rows the forward walk paints the new content into).
        _reposition_tail(span, row_delta);

        // Edit-row splice geometry, computed once (the walk's first row is the
        // edit row): the cursor column, and the signed count of content chars
        // the edit adds/removes on that row.
        const RowView& edit_post = _layout.rows[span.edit_row];
        const RowView& edit_pre  = _pre.rows[span.edit_row];
        uint16_t edit_col    = (uint16_t)(_prompt_len(_row_prompt(span.edit_row))
                                          + (span_start - edit_post.buf_start));
        int32_t  edit_amount = _edit_row_amount(span_start, span_len, is_insert,
                                                edit_post, edit_pre);

        // Forward walk from the edit row, bounded to the viewport. Each row maps
        // to the pre row now physically present (or is fresh: an IL-opened
        // blank, or a row whose old glyphs are unrelated to the wanted
        // content). Rows carry no state across iterations - each is spliced from
        // its own `_pre`/`_layout` pair - so skipping ones off screen is safe.
        uint16_t walk_from = span.edit_row > _view_top ? span.edit_row : _view_top;
        uint16_t walk_to   = (uint16_t)(_view_top + _view_height());
        if (walk_to > _layout.count) walk_to = _layout.count;
        for (uint16_t i = walk_from; i < walk_to; i++) {
            const RowView& want  = _layout.rows[i];
            uint16_t want_prompt = _prompt_len(_row_prompt(i));

            // Map post row i to the pre row physically present after the IL/DL.
            // Rows in the IL-opened gap [absorbed_pre, absorbed_post) have no
            // pre image (has_pre false) and drop through to the repaint below.
            // The gap is empty unless the edit grew the row count.
            uint16_t pre_idx = (i < span.absorbed_post)
                ? i : (uint16_t)((int32_t)i - row_delta);
            // A pre row that was outside the viewport has no glyphs on screen,
            // whatever its slice says: after a DL the rows sliding up into the
            // window came from below it, so the physical row is blank. Splicing
            // against what it "shows" would corrupt it; repaint instead.
            bool has_pre = !(i >= span.absorbed_pre && i < span.absorbed_post)
                           && _was_visible(pre_idx);
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

        // The edit may have pushed the cursor out of the window (a wrap onto a
        // row below it, or a delete that pulled the text up past the top).
        _follow_cursor();
    }

    // cursor motion
    // ------------------------------------------------------------------------

    void TextEditor::move_left() {
        if (_pos == 0) return;
        _pos--;
        _settle_cursor();
    }

    void TextEditor::move_right() {
        if (_pos >= _len) return;
        _pos++;
        _settle_cursor();
    }

    void TextEditor::move_to_line_start() {
        while (_pos > 0 && _buf[_pos - 1] != '\n') _pos--;
        _settle_cursor();
    }

    void TextEditor::move_to_line_end() {
        while (_pos < _len && _buf[_pos] != '\n') _pos++;
        _settle_cursor();
    }

    void TextEditor::move_to_start() { _pos = 0;    _settle_cursor(); }
    void TextEditor::move_to_end()   { _pos = _len; _settle_cursor(); }

    void TextEditor::_move_row(int32_t delta) {
        if (_layout.count == 0 || delta == 0) return;
        uint16_t row = _owner_row(_pos, _layout);

        // Latch the column on the first of a run of vertical moves, so passing
        // through a short line does not shorten the ones after it.
        if (!_goal_valid) {
            _goal_col   = (uint16_t)(_pos - _layout.rows[row].buf_start);
            _goal_valid = true;
        }

        int32_t last   = (int32_t)_layout.count - 1;
        int32_t target = (int32_t)row + delta;
        if (target < 0)    target = 0;
        if (target > last) target = last;

        if (target == (int32_t)row) {
            // Already on the edge row: fall to the end of the text, which is
            // what pressing up on the first line or down on the last means.
            _pos = delta < 0 ? 0 : _len;
            _settle_cursor();
            return;
        }

        const RowView& r = _layout.rows[target];
        uint16_t col = _goal_col < r.buf_len ? _goal_col : r.buf_len;
        _pos = (uint16_t)(r.buf_start + col);
        _settle_cursor(/*keep_goal=*/true);
    }

    void TextEditor::move_up()   { _move_row(-1); }
    void TextEditor::move_down() { _move_row(+1); }

    void TextEditor::page_up() {
        uint16_t h = _view_height();
        _move_row(-(int32_t)(h > 1 ? h - 1 : 1));
    }

    void TextEditor::page_down() {
        uint16_t h = _view_height();
        _move_row((int32_t)(h > 1 ? h - 1 : 1));
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
        _settle_cursor();
    }

    void TextEditor::move_word_right() {
        while (_pos < _len &&  _is_word_char(_buf[_pos])) _pos++;
        while (_pos < _len && !_is_word_char(_buf[_pos])) _pos++;
        _settle_cursor();
    }

}}
