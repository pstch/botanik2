/**
 * @file editor.cpp
 * @brief TextEditor - multi-row line editor. Each edit mutates the buffer,
 *        rebuilds the row layout (`_layout`) and repaints the viewport; the
 *        terminal's diff pays for what actually changed.
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

    // lifecycle

    TextEditor::TextEditor(Console* console, const EditorStorage& store)
        : _console(console), _terminal(&console->terminal()),
          _buf(store.buf), _buf_cap(store.buf_cap), _max_rows(store.max_rows) {
        _layout.rows = store.layout;
        _buf[0] = '\0';
        _layout.rows[0] = {0, 0, false};
        _layout.count = 1;
    }

    void TextEditor::reset() {
        (void)_load(nullptr, 0, 0);
        _dirty = true;              // reset() discards content; commit() does not
        _reconcile();
    }

    void TextEditor::commit() {
        (void)_load(nullptr, 0, 0);
        _reconcile();
    }

    void TextEditor::redraw() {
        // Width first: console_cols is a parameter, so a redraw may be the
        // first thing to run at a new geometry and the row breaks are stale.
        _rebuild_layout();
        _reconcile();           // a page switch handed the band back; reclaim it
    }

    // prompts

    RowPrompt TextEditor::_row_prompt(uint16_t row_idx) const {
        return _row_prompt_in(_layout, row_idx);
    }

    // layout

    uint16_t TextEditor::_row_cap(uint16_t row_idx) const {
        uint16_t cols = _terminal->cols();
        uint16_t used = (uint16_t)(_prompt_len(_row_prompt(row_idx)) + _indent);
        return cols > used ? (uint16_t)(cols - used) : 1;
    }

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
        return !done;       // rows ran out before the buffer did
    }

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
        if (_rebuild_layout()) {
            // Past the row cap: drop the tail nobody could see or reach.
            const RowView& last = _layout.rows[_layout.count - 1];
            _len = (uint16_t)(last.buf_start + last.buf_len);
            _buf[_len] = '\0';
            _rebuild_layout();
            fitted = false;
        }
        _pos = cursor < _len ? cursor : _len;
        return fitted;
    }

    CursorPos TextEditor::_map_cursor_pos(uint16_t pos) const {
        uint16_t i      = _owner_row(pos, _layout);
        uint16_t prompt = _prompt_len(_row_prompt(i));
        uint16_t col    = (uint16_t)(_indent + prompt + (pos - _layout.rows[i].buf_start));
        return { _hw_row(i), col };
    }

    // viewport and geometry

    uint16_t TextEditor::_console_rows() const { return _console->rows(); }

    uint16_t TextEditor::_view_bottom_hw() const {
        uint16_t rows = _console->rows();
        if (rows == 0) return 0;
        uint16_t bottom = (uint16_t)(rows - 1);
        uint16_t h      = _view_height();
        if (h == 0) return bottom;
        uint16_t last = (uint16_t)(_origin + h - 1);
        return last < bottom ? last : bottom;
    }

    void TextEditor::_follow_cursor() {
        // Bottom-anchored, `max_top` is 0 until the text is taller than the band.
        uint16_t h = _view_height();
        if (h == 0) return;
        uint16_t row = _owner_row(_pos, _layout);

        uint16_t want = _view_top;
        if (row < _view_top)                       want = row;
        else if (row >= (uint16_t)(_view_top + h)) want = (uint16_t)(row - h + 1);

        // Never past the end: blank rows below the text would be lost screen.
        uint16_t max_top = _layout.count > h ? (uint16_t)(_layout.count - h) : 0;
        _view_top = want < max_top ? want : max_top;
    }

    void TextEditor::set_origin(uint16_t row) {
        uint16_t max_row = (uint16_t)(_console->rows() - 1);
        _origin = row > max_row ? max_row : row;
    }

    // painting

    void TextEditor::_write_prompt(uint16_t row_idx) {
        RowPrompt p = _row_prompt(row_idx);
        _terminal->write_chars(_prompt_buf(p), _prompt_len(p));
    }

    void TextEditor::_paint_view() {
        // Pen plain, or clear_to_eol stamps an inverted bar to the margin.
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
        // A live region marks the edit point itself; a cursor would hide a cell.
        _terminal->set_cursor_inhibited(_mark_set);
    }

    void TextEditor::_paint_region() {
        uint16_t from = 0, to = 0;
        if (!region(from, to)) return;
        _terminal->set_inverted(true);
        if (from == to) {
            // One inverted cell at the anchor; past the last character there is
            // no glyph to invert, so make one.
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

    void TextEditor::_place_rows() {
        uint16_t rows = _console->rows();
        if (rows == 0) return;

        if (_anchor == Anchor::TOP) {
            // Top-anchored: the viewport is STRUCT and the cursor's row INPUT.
            // The re-declaration releases the row the cursor left.
            uint16_t view_bottom = _view_bottom_hw();
            if (_origin > view_bottom) return;
            _terminal->set_row_priority(_origin, view_bottom, PRIO_STRUCT);
            const uint16_t cur = _map_cursor_pos(_pos).row;
            if (cur >= _origin && cur <= view_bottom)
                _terminal->set_row_priority(cur, PRIO_INPUT);
            return;
        }

        // Bottom-anchored: release the content band, reclaim the rows occupied.
        uint16_t bottom = (uint16_t)(rows - 1);
        _terminal->set_row_priority(1, bottom, PRIO_STRUCT);
        if (_layout.count <= _view_top || _origin > bottom) return;
        uint16_t last = (uint16_t)(_origin + (_layout.count - _view_top) - 1);
        if (last > bottom) last = bottom;
        _terminal->set_row_priority(_origin, last, PRIO_INPUT);
    }

    // editing

    void TextEditor::insert_characters(const char* buf, uint16_t len) {
        // One byte is reserved for the NUL, so usable content is _buf_cap-1.
        if (len == 0 || (uint32_t)_len + 1 >= _buf_cap) return;
        if ((uint32_t)_len + len >= _buf_cap) len = (uint16_t)(_buf_cap - _len - 1);
        if (len == 0) return;

        if (_single_line) {
            // Stop at the first newline, and at the row end.
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

        if (_rebuild_layout()) {
            // Past the row cap: refuse the keystroke. Nothing has been painted.
            memmove(&_buf[pos], &_buf[pos + len], (size_t)(_len - (pos + len)));
            _len -= len;
            _buf[_len] = '\0';
            _pos = pos;
            (void)_rebuild_layout();
            return;
        }

        _dirty = true;
        _goal_valid = false;
        _mark_set   = false;    // transient mark: an edit drops the region
        _reconcile();
    }

    void TextEditor::remove_characters(uint16_t len) {
        if (len == 0 || _pos >= _len) return;
        if (_pos + len > _len) len = (uint16_t)(_len - _pos);

        uint16_t safe_len = (uint16_t)(_len - (_pos + len));

        if (safe_len > 0) memmove(&_buf[_pos], &_buf[_pos + len], safe_len);
        _len -= len;
        _buf[_len] = '\0';

        _rebuild_layout();
        _dirty = true;
        _goal_valid = false;
        _mark_set   = false;    // transient mark: an edit drops the region
        _reconcile();
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

    // bulk content

    bool TextEditor::commit_loaded(uint16_t len) {
        const bool fitted = _load(_buf, len, 0);
        _dirty = false;
        _reconcile();
        return fitted;
    }

    void TextEditor::replace_all(const char* text, uint16_t len) {
        if (text == nullptr) len = 0;
        (void)_load(text, len, len);    // a recalled line is ready to be extended
        _dirty = true;
        _reconcile();
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

        // Latched on the first of a run of vertical moves.
        if (!_goal_valid) {
            _goal_col   = (uint16_t)(_pos - _layout.rows[row].buf_start);
            _goal_valid = true;
        }

        int32_t last   = (int32_t)_layout.count - 1;
        int32_t target = (int32_t)row + delta;
        if (target < 0)    target = 0;
        if (target > last) target = last;

        if (target == (int32_t)row) {
            // Already on the edge row: up on the first line means the start.
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

    // kill and yank

    bool TextEditor::_kill_span(uint16_t from, uint16_t to) {
        if (to <= from) return false;
        // Remove only what the ring took, so a yank puts back exactly what went.
        uint16_t taken = _console->kill_ring().set(_buf + from, (uint16_t)(to - from));
        if (taken == 0) return false;
        _pos = from;
        remove_characters(taken);
        return true;
    }

    bool TextEditor::kill_to_line_end() {
        if (_pos >= _len) return false;
        // On the newline, the newline is the kill: the line closes up onto the next.
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

    // mark and region

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

    bool TextEditor::region(uint16_t& from, uint16_t& to) const {
        if (!_mark_set) return false;
        from = _mark < _pos ? _mark : _pos;
        to   = _mark < _pos ? _pos : _mark;
        return true;
    }

    bool TextEditor::kill_region() {
        uint16_t from = 0, to = 0;
        if (!region(from, to)) return false;
        // An empty region removes nothing, so nothing drops the mark for it.
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

    bool TextEditor::kill_region_or_word() {
        return _mark_set ? kill_region() : kill_word_back();
    }

    // reconciliation

    void TextEditor::_reconcile() {
        // Bottom-anchored, the text's last row stays on the content bottom and
        // it grows upwards, never above row 1 (the status bar's row is 0).
        if (_anchor == Anchor::BOTTOM) {
            const uint16_t rows   = _console->rows();
            const uint16_t origin = rows > _layout.count ? (uint16_t)(rows - _layout.count) : 1;
            // Rows given back are blanked; an owner painting above repaints them.
            _terminal->set_inverted(false);
            for (uint16_t r = _origin; r < origin; r++) {
                _terminal->move_to({r, 0});
                _terminal->clear_to_eol();
            }
            _origin = origin;
        }
        _place_rows();
        _follow_cursor();
        _paint_view();
        _place_cursor(_map_cursor_pos(_pos));
    }

}}

#endif // BOTANIK_FEATURE_CONSOLE
