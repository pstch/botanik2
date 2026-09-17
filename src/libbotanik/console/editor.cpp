/**
 * @file editor.cpp
 * @brief TextEditor - multi-row line editor. Each edit mutates the buffer,
 *        rebuilds the row layout (`_rows`) and repaints the viewport; the
 *        terminal's diff pays for what actually changed.
 */
#include "libbotanik/console/editor.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE

#include <cctype>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "libbotanik/console/console.h"
#include "libbotanik/console/kill_ring.h"


namespace Botanik { namespace Terminal {

    // lifecycle

    TextEditor::TextEditor(Console* console, const EditorStorage& store)
        : _console(console), _terminal(&console->terminal()),
          _buf(store.buf), _buf_cap(store.buf_cap), _max_rows(store.max_rows),
          _rows(store.layout), _painted(store.painted) {
        _buf[0] = '\0';
        _rows[0] = {0, 0, false};
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
        uint16_t pos = 0;
        for (_row_count = 0; _row_count < _max_rows;) {
            const RowView row = _scan_row(pos, _row_cap(_row_count));
            _rows[_row_count++] = row;
            pos = (uint16_t)(pos + row.buf_len + (row.hard_break ? 1 : 0));
            if (!row.hard_break && pos >= _len) return false;
        }
        return true;        // rows ran out before the buffer did
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
        // The text is replaced, so no edit relates the two; what the rows did on
        // screen still can, and every hint is checked against BACK before it goes.
        _edit_pos   = 0;
        _edit_delta = 0;
        _mark_set   = false;        // the offsets it anchors are being replaced
        if (_rebuild_layout()) {
            // Past the row cap: drop the tail nobody could see or reach.
            const RowView& last = _rows[_row_count - 1];
            _len = (uint16_t)(last.buf_start + last.buf_len);
            _buf[_len] = '\0';
            _rebuild_layout();
            fitted = false;
        }
        _pos = cursor < _len ? cursor : _len;
        return fitted;
    }

    CursorPos TextEditor::_map_cursor_pos(uint16_t pos) const {
        uint16_t i      = _owner_row(pos);
        uint16_t prompt = _prompt_len(_row_prompt(i));
        uint16_t col    = (uint16_t)(_indent + prompt + (pos - _rows[i].buf_start));
        return { _hw_row(i), col };
    }

    // viewport and geometry

    uint16_t TextEditor::_view_height() const {
        if (_view_rows != 0) return _view_rows;
        const uint16_t rows = _console->rows();
        return rows > _origin ? (uint16_t)(rows - _origin) : 0;
    }

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
        uint16_t row = _owner_row(_pos);

        uint16_t want = _view_top;
        if (row < _view_top)                       want = row;
        else if (row >= (uint16_t)(_view_top + h)) want = (uint16_t)(row - h + 1);

        // Never past the end: blank rows below the text would be lost screen.
        uint16_t max_top = _row_count > h ? (uint16_t)(_row_count - h) : 0;
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
            if (i < _row_count) {
                _write_prompt(i);
                const RowView& r = _rows[i];
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
            const uint16_t i = _owner_row(from);
            const RowView& r = _rows[i];
            if (_visible(i)) {
                _terminal->move_to(_map_cursor_pos(from));
                _terminal->write_chars(from < (uint16_t)(r.buf_start + r.buf_len) ? &_buf[from] : " ", 1);
            }
        } else {
            for (uint16_t i = _view_top, e = _visible_end(); i < e; i++) {
                const RowView& r = _rows[i];
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
        if (_row_count <= _view_top || _origin > bottom) return;
        uint16_t last = (uint16_t)(_origin + (_row_count - _view_top) - 1);
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
        _edit_pos = _pos; _edit_delta = (int16_t)len;

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
            _edit_delta = 0;
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

        _edit_pos = _pos; _edit_delta = (int16_t)-(int16_t)len;
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
        if (_row_count == 0 || delta == 0) return;
        uint16_t row = _owner_row(_pos);

        // Latched on the first of a run of vertical moves.
        if (!_goal_valid) {
            _goal_col   = (uint16_t)(_pos - _rows[row].buf_start);
            _goal_valid = true;
        }

        int32_t last   = (int32_t)_row_count - 1;
        int32_t target = (int32_t)row + delta;
        if (target < 0)    target = 0;
        if (target > last) target = last;

        if (target == (int32_t)row) {
            // Already on the edge row: up on the first line means the start.
            _pos = delta < 0 ? 0 : _len;
            _settle_cursor();
            return;
        }

        const RowView& r = _rows[target];
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
        _reconcile();
    }

    void TextEditor::clear_mark() {
        if (!_mark_set) return;
        _mark_set = false;
        _reconcile();
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

    // hints

    void TextEditor::_hint_moves() {
        const uint16_t first = _view_top, end = _visible_end();
        const int16_t  k     = _edit_delta;
        const uint16_t p     = _edit_pos;
        _edit_delta = 0;
        if (!_painted_valid || first >= end) { _remember_paint(); return; }

        // What each visible row is mostly made of: the painted row it shares the
        // most bytes with, found by mapping this row's bytes back through the
        // edit. A byte at or past the edit moved by k, so a row's old range is
        // its own less that. The row a break pushed text into is then read by
        // the row that text came from, not by the byte that rippled in first.
        int16_t  moved[Config::EDITOR_MAX_ROWS];
        int16_t  slid [Config::EDITOR_MAX_ROWS];
        uint16_t from_col[Config::EDITOR_MAX_ROWS];
        uint16_t n = 0;
        for (uint16_t j = first; j < end && n < Config::EDITOR_MAX_ROWS; j++, n++) {
            moved[n] = INT16_MIN;
            slid[n]  = 0;
            const RowView& row = _rows[j];
            // This row's range in the offsets the screen was painted from: bytes
            // before the edit kept theirs, bytes after moved by k, and the ones
            // an insert added have none.
            const int32_t s = row.buf_start, e = s + row.buf_len;
            const int32_t s_old = (s < (int32_t)p) ? s : std::max<int32_t>((int32_t)p, s - k);
            const int32_t e_old = (e <= (int32_t)p) ? e : e - k;
            if (e_old <= s_old) continue;

            // Each painted row it overlaps gives a run, split at the edit: the
            // bytes before it kept their place, the ones after took the shift.
            // The run worth hinting is ranked by what the wire can do with it,
            // and only then by length.
            int32_t best_overlap = 0;
            uint8_t best_tier    = 0;
            for (uint16_t i = 0; i < _painted_count; i++) {
                const int32_t rlo = std::max<int32_t>(s_old, _painted[i].buf_start);
                const int32_t rhi = std::min<int32_t>(e_old, (int32_t)(_painted[i].buf_start + _painted[i].buf_len));
                if (rhi <= rlo) continue;
                for (uint8_t half = 0; half < 2; half++) {
                    const int32_t lo = half == 0 ? rlo : std::max<int32_t>(rlo, (int32_t)p);
                    const int32_t hi = half == 0 ? std::min<int32_t>(rhi, (int32_t)p) : rhi;
                    if (hi <= lo) continue;
                    const int32_t now = (lo < (int32_t)p) ? lo : lo + k;
                    if (now < s || now >= e) continue;          // that run left this row
                    const int16_t  m  = (int16_t)((int32_t)_hw_row(j) - (int32_t)(_painted_origin + i - _painted_view_top));
                    const uint16_t wc = (uint16_t)(_indent + _painted_prompt_len(i) + (lo - _painted[i].buf_start));
                    const uint16_t nc = (uint16_t)(_indent + _prompt_len(_row_prompt(j)) + (now - s));
                    const int16_t  sl = (int16_t)((int32_t)nc - wc);
                    // One escape beats two, and two beat none: a whole row that
                    // moved is a band, a row that stayed and slid is a column
                    // move, a run that did both needs the pair, and a run that
                    // sat still carries nothing.
                    const uint8_t tier = (m != 0 && sl == 0) ? 3 : (m == 0 && sl != 0) ? 2 : (m != 0) ? 1 : 0;
                    if (best_overlap != 0 && (tier < best_tier || (tier == best_tier && (hi - lo) <= best_overlap)))
                        continue;
                    best_overlap = hi - lo; best_tier = tier;
                    // Both halves of the one move: the band brings the run to this
                    // row, the column shift places it. The terminal prices them
                    // apart, so a band it refuses drops its column move too.
                    moved[n] = m; slid[n] = sl; from_col[n] = wc;
                }
            }
            if (best_overlap == 0) continue;
        }

        // The rows that moved together are one band move over the rows this
        // editor owns; whatever moved differently the terminal writes out.
        uint16_t best_at = 0, best_len = 0;
        for (uint16_t a = 0; a < n;) {
            if (moved[a] == INT16_MIN || moved[a] == 0) { a++; continue; }
            uint16_t b = a;
            while (b < n && moved[b] == moved[a]) b++;
            if ((uint16_t)(b - a) > best_len) { best_len = (uint16_t)(b - a); best_at = a; }
            a = b;
        }
        if (best_len != 0) {
            // The band is the rows that moved and the rows they came from, as far
            // as this editor's own rows reach.
            const int16_t  d      = moved[best_at];
            const uint16_t bottom = _view_bottom_hw();
            const int32_t  to_hi  = (int32_t)_hw_row((uint16_t)(first + best_at + best_len - 1));
            const int32_t  to_lo  = (int32_t)_hw_row((uint16_t)(first + best_at));
            int32_t top = (d < 0 ? to_lo : to_lo - d);
            int32_t bot = (d < 0 ? to_hi - d : to_hi);
            // The rows this editor owns include the ones it is vacating: shrinking
            // moves its content down into them.
            const int32_t own_top = (int32_t)std::min(_origin, _painted_origin);
            if (top < own_top) top = own_top;
            if (bot > (int32_t)bottom)  bot = bottom;
            if (top < bot) _terminal->hint_rows((uint16_t)top, (uint16_t)bot, d);
        }
        // A row the band carries can have slid sideways too: its prompt may have
        // changed width, or text rippled into it. Its columns are the ones the
        // band leaves behind, which is what the terminal applies them to.
        const int16_t carried = best_len != 0 ? moved[best_at] : 0;
        for (uint16_t a = 0; a < n; a++)
            if (slid[a] != 0 && (moved[a] == 0 || moved[a] == carried))
                _terminal->hint_cols(_hw_row((uint16_t)(first + a)), from_col[a], slid[a]);
        _remember_paint();
    }

    void TextEditor::_remember_paint() {
        for (uint16_t i = 0; i < _row_count && i < _max_rows; i++) _painted[i] = _rows[i];
        _painted_count    = _row_count;
        _painted_origin   = _origin;
        _painted_view_top = _view_top;
        _painted_len      = _len;
        _painted_valid    = true;
    }

    // reconciliation

    void TextEditor::_reconcile() {
        // Bottom-anchored, the text's last row stays on the content bottom and
        // it grows upwards, never above row 1 (the status bar's row is 0).
        if (_anchor == Anchor::BOTTOM) {
            const uint16_t rows   = _console->rows();
            const uint16_t origin = rows > _row_count ? (uint16_t)(rows - _row_count) : 1;
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
        _hint_moves();
        _paint_view();
        _place_cursor(_map_cursor_pos(_pos));
    }

}}

#endif // BOTANIK_FEATURE_CONSOLE
