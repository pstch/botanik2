/**
 * @file editor.cpp
 * @brief TextEditor - byte-efficient multi-row line editor. Each edit mutates
 *        the buffer, rebuilds the row layout (`_layout`), announces the one
 *        IL/DL that slides the rows it only displaced, and repaints the
 *        viewport; the terminal's diff pays for what actually changed.
 */
#include "libbotanik/console/editor.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE

#include <cctype>
#include <cstdint>
#include <cstring>

#include "libbotanik/console/console.h"
#include "libbotanik/console/kill_ring.h"


namespace Botanik { namespace Terminal {

    TextEditor::TextEditor(Console* console, const EditorStorage& store)
        : _console(console), _terminal(&console->terminal()),
          _buf(store.buf), _buf_cap(store.buf_cap), _max_rows(store.max_rows) {
        _layout.rows = store.layout;
        _pre.rows    = store.pre;
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

    RowView TextEditor::_scan_row(uint16_t pos, uint16_t cap) const {
        uint16_t len = 0;
        while (len < cap && (pos + len) < _len && _buf[pos + len] != '\n') len++;
        bool hard_break = (pos + len < _len) && _buf[pos + len] == '\n';
        return RowView { pos, len, hard_break };
    }

    bool TextEditor::_rebuild_layout() {
        uint16_t pos  = 0;
        bool     done = false;
        _layout.count = 0;
        while (_layout.count < _max_rows) {
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
        return !done;
    }

    void TextEditor::_snapshot_pre() {
        _pre.count = _layout.count;
        if (_layout.count > 0)
            memcpy(_pre.rows, _layout.rows, (size_t)_layout.count * sizeof(RowView));
    }

    uint16_t TextEditor::_console_rows() const { return _console->rows(); }

    void TextEditor::_restore_pre() {
        _layout.count = _pre.count;
        if (_pre.count > 0)
            memcpy(_layout.rows, _pre.rows, (size_t)_pre.count * sizeof(RowView));
    }

    CursorPos TextEditor::_map_cursor_pos(uint16_t pos) const {
        uint16_t i      = _owner_row(pos, _layout);
        uint16_t prompt = _prompt_len(_row_prompt(i));
        uint16_t col    = (uint16_t)(_indent + prompt + (pos - _layout.rows[i].buf_start));
        return { _hw_row(i), col };
    }

    // viewport

    uint16_t TextEditor::_view_bottom_hw() const {
        uint16_t rows = _console->rows();
        if (rows == 0) return 0;
        uint16_t bottom = (uint16_t)(rows - 1);
        uint16_t h      = _view_height();
        if (h == 0) return bottom;
        uint16_t last = (uint16_t)(_origin + h - 1);
        return last < bottom ? last : bottom;
    }

    bool TextEditor::_follow_cursor() {
        // Bottom-anchored the text fits the band until it is taller than the
        // screen, and `max_top` below is 0 while it does: this only ever moves
        // a window over text that _grow_editor could not make room for.
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

        // Slide what stays, so the paint behind us finds it in place and the
        // diff pays only for the rows the scroll uncovered. A jump of a whole
        // window or more keeps nothing, and the paint alone is cheaper.
        if (n < h) _terminal->scroll_region(_origin, _view_bottom_hw(), /*up=*/delta > 0, n);
        return true;
    }

    // lifecycle

    bool TextEditor::_load(const char* text, uint16_t len, uint16_t cursor) {
        bool fitted = true;
        if (len > load_capacity()) { len = load_capacity(); fitted = false; }
        // memmove, not memcpy: a caller may hand back a slice of this buffer.
        if (text != nullptr && text != _buf && len > 0) memmove(_buf, text, len);
        _len = len;
        _buf[_len] = '\0';
        _view_top   = 0;
        _goal_valid = false;
        _mark_set   = false;        // the offsets it anchors are being replaced
        _snapshot_pre();
        if (_rebuild_layout()) {
            // Past what the row cap can lay out: drop the tail rather than keep
            // text the operator can neither see nor reach, or that a later
            // save would write back truncated.
            const RowView& last = _layout.rows[_layout.count - 1];
            _len = (uint16_t)(last.buf_start + last.buf_len);
            _buf[_len] = '\0';
            _rebuild_layout();
            fitted = false;
        }
        _pos = cursor < _len ? cursor : _len;
        return fitted;
    }

    void TextEditor::reset() {
        (void)_load(nullptr, 0, 0);
        _dirty = true;              // reset() discards content; commit() does not
        _reconcile(0, 0, /*is_insert=*/true);
    }

    void TextEditor::commit() {
        // No reconcile: the caller line-fed past the submitted text, which is
        // the transcript's now, and the band re-pins to the bottom row below it.
        (void)_load(nullptr, 0, 0);
        if (_anchor == Anchor::BOTTOM) _origin = (uint16_t)(_console->rows() - 1);
        _paint_view();
        _place_cursor(_map_cursor_pos(0));
    }

    void TextEditor::set_origin(uint16_t row) {
        uint16_t max_row = (uint16_t)(_console->rows() - 1);
        _origin = row > max_row ? max_row : row;
    }

    // painting

    void TextEditor::redraw() {
        _place_rows();          // a page switch handed the band back; reclaim it
        _paint_view();
        _place_cursor(_map_cursor_pos(_pos));
    }

    void TextEditor::_paint_view() {
        // Say the pen, and leave it plain: an inherited inverted one would
        // stamp a bar out to the margin.
        _terminal->set_inverted(false);
        const uint16_t bottom = _view_bottom_hw();
        const uint16_t end    = (uint16_t)(_view_top + _view_height());
        for (uint16_t i = _view_top; i < end; i++) {
            const uint16_t hw = _hw_row(i);
            if (hw > bottom) break;
            _terminal->move_to({hw, _indent});
            if (i < _layout.count) {
                _write_prompt(i);
                const RowView& r = _layout.rows[i];
                if (r.buf_len > 0) _terminal->write_chars(&_buf[r.buf_start], r.buf_len);
            }
            _terminal->clear_to_eol();
        }
        _paint_region();
        // A region IS the edit point, drawn: a cursor blinking inside the
        // highlight says nothing the far end of it does not, and hides the cell
        // it sits on while it blinks.
        _terminal->set_cursor_inhibited(_mark_set);
    }

    void TextEditor::_paint_region() {
        uint16_t from = 0, to = 0;
        if (!region(from, to)) return;
        _terminal->set_inverted(true);
        if (from == to) {
            // Nothing selected yet: one inverted cell at the anchor, or
            // Ctrl-Space would have nothing to show for itself. Past the last
            // character there is no glyph to invert, so make one.
            const uint16_t i = _owner_row(from, _layout);
            const RowView& r = _layout.rows[i];
            if (_visible(i)) {
                _terminal->move_to(_map_cursor_pos(from));
                _terminal->write_chars(from < (uint16_t)(r.buf_start + r.buf_len) ? &_buf[from] : " ", 1);
            }
        } else {
            for (uint16_t i = _view_top, e = _visible_end(); i < e; i++) {
                const RowView& r = _layout.rows[i];
                const uint16_t start = r.buf_start, end = (uint16_t)(start + r.buf_len);
                const uint16_t l  = from > start ? from : start;
                const uint16_t rr = to   < end   ? to   : end;
                if (l >= rr) continue;
                _terminal->move_to({_hw_row(i), (uint16_t)(_indent + _prompt_len(_row_prompt(i)) + (l - start))});
                _terminal->write_chars(&_buf[l], (uint16_t)(rr - l));
            }
        }
        _terminal->set_inverted(false);
    }
    // editing

    void TextEditor::insert_characters(const char* buf, uint16_t len) {
        // One byte is reserved for the NUL, so usable content is _buf_cap-1.
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

        if (_rebuild_layout()) {
            // More rows than the layout can hold: the tail would be text the
            // operator can neither see nor reach, so refuse the keystroke.
            // Nothing has reached the wire yet - the reconcile below is what
            // emits.
            memmove(&_buf[pos], &_buf[pos + len], (size_t)(_len - (pos + len)));
            _len -= len;
            _buf[_len] = '\0';
            _pos = pos;
            _restore_pre();          // _pre is the pre-edit layout, exactly
            return;
        }

        _dirty = true;
        _goal_valid = false;
        _mark_set   = false;    // transient mark: an edit drops the region
        _reconcile(pos, len, /*is_insert=*/true);
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
        _mark_set   = false;    // transient mark: an edit drops the region
        _reconcile(pos, len, false);
    }

    bool TextEditor::commit_loaded(uint16_t len) {
        const bool fitted = _load(_buf, len, 0);
        _dirty = false;
        _reconcile(0, _len, /*is_insert=*/true);
        return fitted;
    }

    void TextEditor::replace_all(const char* text, uint16_t len) {
        if (text == nullptr) len = 0;
        (void)_load(text, len, len);    // a recalled line is ready to be extended
        _dirty = true;
        _reconcile(0, _len, /*is_insert=*/true);
    }
    // grow/shrink helpers

    void TextEditor::_place_rows() {
        uint16_t rows = _console->rows();
        if (rows == 0) return;

        if (_anchor == Anchor::TOP) {
            // The viewport is fixed and the editor owns all of it: claim exactly
            // that, and no more. Releasing the whole band would demote a row the
            // page keeps for itself (a status line under the editor) every
            // keystroke.
            uint16_t view_bottom = _view_bottom_hw();
            if (_origin > view_bottom) return;
            // Only the EDIT POINT is input, not the whole note. Claiming the
            // viewport meant a full-screen editor outranked PRIO_CHROME, so on
            // the NOTES page the text went out ahead of the bars framing it -
            // the one page where the frame arrived last. The re-declaration is
            // what releases the row the cursor was on, so nothing is tracked.
            _terminal->set_row_priority(_origin, view_bottom, PRIO_STRUCT);
            const uint16_t cur = _map_cursor_pos(_pos).row;
            if (cur >= _origin && cur <= view_bottom)
                _terminal->set_row_priority(cur, PRIO_INPUT);
            return;
        }

        // Bottom-anchored the extent moves as the text grows, so release the
        // content band and reclaim exactly the rows occupied now. Safe only
        // while no page keeps a class of its own on a row a bottom-anchored
        // editor shares a screen with; a page re-declares in focus() anyway.
        uint16_t bottom = (uint16_t)(rows - 1);
        _terminal->set_row_priority(1, bottom, PRIO_STRUCT);
        if (_layout.count <= _view_top || _origin > bottom) return;
        uint16_t last = (uint16_t)(_origin + (_layout.count - _view_top) - 1);
        if (last > bottom) last = bottom;
        _terminal->set_row_priority(_origin, last, PRIO_INPUT);
    }

    uint16_t TextEditor::_grow_editor() {
        // Top-anchored, growing the text never moves the editor: the viewport
        // scrolls over it instead (_follow_cursor).
        if (_anchor == Anchor::TOP) return _origin;

        // Bottom-pinned origin: pull the top up by the rows the editor grew,
        // clamped at the first content row (row 0 is the status bar). Text
        // taller than the band stays put there, and the viewport follows the
        // cursor over it instead.
        uint16_t scr_rows = _console->rows();
        uint16_t new_origin;
        if (scr_rows > _layout.count) new_origin = (uint16_t)(scr_rows - _layout.count);
        else                          new_origin = 1;

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
        // Slide the content band (rows 1..bottom) down by the rows the editor
        // gave back, the mirror of _grow_editor: the editor lands on the rows
        // already showing it, and whatever stood above it follows, so the owner
        // of those rows only paints the ones opened at the top. Measured
        // against blanking the vacated rows and repainting at the new origin,
        // the slide wins by 4 to 48 bytes.
        if (_origin < _console->rows())
            _terminal->scroll_region(1, (uint16_t)(_console->rows() - 1),
                                     /*up=*/false, (uint16_t)(new_origin - _origin));
        _origin = new_origin;
    }

    // reconciliation

    static int32_t _common(int32_t a0, int32_t a1, int32_t b0, int32_t b1) {
        const int32_t lo = a0 > b0 ? a0 : b0;
        const int32_t hi = a1 < b1 ? a1 : b1;
        return hi > lo ? hi - lo : 0;
    }

    int32_t TextEditor::_kept(uint16_t i, uint16_t j, uint16_t span_start,
                              uint16_t span_len, bool is_insert) const {
        // Bytes of post row i that pre row j showed: what lies before the edit
        // kept its offset, what lies after moved by the edit's length.
        const RowView& p = _layout.rows[i];
        const RowView& q = _pre.rows[j];
        const int32_t ps = p.buf_start, pe = ps + p.buf_len;
        const int32_t qs = q.buf_start, qe = qs + q.buf_len;
        const int32_t after = is_insert ? (int32_t)span_start + span_len : (int32_t)span_start;
        const int32_t moved = is_insert ? -(int32_t)span_len : (int32_t)span_len;
        int32_t n = _common(ps, pe < (int32_t)span_start ? pe : (int32_t)span_start, qs, qe);
        const int32_t from = ps > after ? ps : after;
        n += _common(from + moved, pe + moved, qs, qe);
        return n;
    }

    TextEditor::SpliceSpan
    TextEditor::_find_splice_span(uint16_t span_start, uint16_t span_len,
                                  bool is_insert, int32_t row_delta) const {
        // Post rows from the boundary down take the pre row `row_delta` away
        // from them, the ones above keep their own, and the IL/DL at the
        // boundary is what makes it so. Pick the boundary keeping the most
        // bytes on the row that already shows them, over rows on screen: a
        // row the diff can slide is worth its length, one it has to rewrite
        // is worth nothing however well its slice matched.
        // What the IL/DL pair itself costs, in bytes: two moves, two escapes.
        // A tail has to keep more than that to be worth announcing.
        constexpr int32_t SCROLL_COST = 24;
        const int32_t count = _layout.count, pre_count = _pre.count;
        int32_t  best     = INT32_MIN;
        uint16_t best_pre = (uint16_t)pre_count;
        for (int32_t a = 0; a <= pre_count; a++) {              // pre idx of the tail
            const int32_t b = a + row_delta;                     // post idx of the tail
            if (b < 0 || b > count) continue;
            const int32_t head = a < b ? a : b;                  // rows keeping their own
            int32_t kept = b < count ? -SCROLL_COST : 0;
            for (int32_t i = 0; i < count; i++) {
                if (i >= head && i < b) continue;                // opened by the IL
                const int32_t j = i < b ? i : i - row_delta;
                if (!_visible((uint16_t)i) || !_visible((uint16_t)j)) continue;
                kept += _kept((uint16_t)i, (uint16_t)j, span_start, span_len, is_insert);
            }
            // Ties go to the lowest boundary: fewest rows announced as moved.
            if (kept >= best) { best = kept; best_pre = (uint16_t)a; }
        }
        return { (uint16_t)((int32_t)best_pre + row_delta), best_pre };
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
        // Bounded to the viewport, so nothing outside the editor moves. Grow
        // scrolls the tail down; shrink scrolls it up.
        _terminal->scroll_region(row, bottom, /*up=*/row_delta < 0, n);
    }

    void TextEditor::_reconcile(uint16_t span_start, uint16_t span_len, bool is_insert) {
        // Keystroke echo must feel immediate however busy the screen is, so
        // re-declare the input rows before touching any of them. The extent
        // moves with the text; rows grown past would else keep the class.
        _place_rows();
        int32_t row_delta = (int32_t)_layout.count - (int32_t)_pre.count;

        // Bottom-anchored, a taller text scrolls the band up to make room.
        uint16_t new_origin = _grow_editor();

        // One IL/DL slides the rows the edit only displaced, so the paint
        // behind it finds them in place and the diff pays nothing for them.
        SpliceSpan span = _find_splice_span(span_start, span_len, is_insert, row_delta);
        _reposition_tail(span, row_delta);

        // Top-anchored, the viewport follows a cursor the edit pushed out of
        // it (a wrap onto a row below, a delete pulling the text up past the
        // top). Before the paint, which then covers what the slide revealed.
        _follow_cursor();
        _paint_view();

        // Bottom-anchored, a shorter text slides the band back down.
        _shrink_editor(new_origin);
        _place_cursor(_map_cursor_pos(_pos));
    }

    // cursor motion

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

    uint16_t TextEditor::_word_start() const {
        uint16_t p = _pos;
        while (p > 0 && !_is_word_char(_buf[p - 1])) p--;
        while (p > 0 &&  _is_word_char(_buf[p - 1])) p--;
        return p;
    }

    uint16_t TextEditor::_word_end() const {
        uint16_t p = _pos;
        while (p < _len &&  _is_word_char(_buf[p])) p++;
        while (p < _len && !_is_word_char(_buf[p])) p++;
        return p;
    }

    void TextEditor::move_word_left() {
        _pos = _word_start();
        _settle_cursor();
    }

    void TextEditor::move_word_right() {
        _pos = _word_end();
        _settle_cursor();
    }

    // kill / yank

    bool TextEditor::_kill_span(uint16_t from, uint16_t to) {
        if (to <= from) return false;
        // Take only what the ring can hold, and remove only what it took: the
        // rest stays in the buffer, so a yank puts back exactly what went.
        uint16_t taken = _console->kill_ring().set(_buf + from, (uint16_t)(to - from));
        if (taken == 0) return false;
        _pos = from;
        remove_characters(taken);
        return true;
    }

    bool TextEditor::kill_to_line_end() {
        if (_pos >= _len) return false;
        // On the newline, the newline IS the kill: that is what closes this
        // line up onto the next.
        if (_buf[_pos] == '\n') return _kill_span(_pos, (uint16_t)(_pos + 1));

        uint16_t end = _pos;
        while (end < _len && _buf[end] != '\n') end++;
        return _kill_span(_pos, end);
    }

    bool TextEditor::kill_word_back() {
        return _kill_span(_word_start(), _pos);
    }

    bool TextEditor::yank() {
        KillRing& ring = _console->kill_ring();
        if (ring.len() == 0) return false;
        insert_characters(ring.text(), ring.len());
        return true;
    }

    // mark / region

    void TextEditor::set_mark() {
        _mark     = _pos;
        _mark_set = true;
        _paint_view();
    }

    void TextEditor::clear_mark() {
        if (!_mark_set) return;
        _mark_set = false;
        _paint_view();
    }

    void TextEditor::toggle_mark() {
        if (_mark_set) clear_mark();
        else           set_mark();
    }

    bool TextEditor::kill_region_or_word() {
        return _mark_set ? kill_region() : kill_word_back();
    }

    bool TextEditor::region(uint16_t& from, uint16_t& to) const {
        if (!_mark_set) return false;
        from = _mark < _pos ? _mark : _pos;
        to   = _mark < _pos ? _pos : _mark;
        return true;
    }

    bool TextEditor::kill_region() {
        uint16_t from = 0, to = 0;
        if (!region(from, to)) return false;
        // _kill_span removes, which drops the mark on its way through
        // remove_characters(); an empty region has nothing to drop it.
        bool cut = _kill_span(from, to);
        clear_mark();
        return cut;
    }

    bool TextEditor::copy_region() {
        uint16_t from = 0, to = 0;
        if (!region(from, to) || to == from) { clear_mark(); return false; }
        _console->kill_ring().set(_buf + from, (uint16_t)(to - from));
        clear_mark();
        return true;
    }

}}

#endif // BOTANIK_FEATURE_CONSOLE
