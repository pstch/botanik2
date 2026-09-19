/**
 * @file editor.cpp
 * @brief TextEditor - multi-row line editor. Each edit mutates the buffer,
 *        rebuilds the row layout (`_rows`) and repaints the viewport; the
 *        terminal's diff pays for what actually changed.
 */
#include "libbotanik/utils/minmax.h"
#include "libbotanik/console/editor.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE

#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "libbotanik/console/console.h"
#include "libbotanik/console/kill_ring.h"


namespace Botanik { namespace Terminal {

    // The terminal's prices, as the announcers weigh a realisation.
    static constexpr int32_t MOVE_BYTES  = AnsiTerminal::MOVE_BYTES;
    static constexpr int32_t SHIFT_BYTES = AnsiTerminal::SHIFT_BYTES;
    static constexpr int32_t SLIDE_BYTES = AnsiTerminal::SLIDE_BYTES;
    static constexpr int32_t PAIR_BYTES  = 2 * SLIDE_BYTES;

    // lifecycle

    TextEditor::TextEditor(Console* console, const EditorStorage& store)
        : _console(console), _terminal(&console->terminal()),
          _buf(store.buf), _buf_cap(store.buf_cap), _max_rows(store.max_rows),
          _rows(store.layout) {
        _buf[0] = '\0';
        _rows[0] = {0, 0, false};
    }

    void TextEditor::reset() {
        (void)_load(nullptr, 0, 0);
        _dirty = true;              // reset() discards content; commit() does not
        _reconcile(Edit::LOAD);
    }

    void TextEditor::commit() {
        (void)_load(nullptr, 0, 0);
        _reconcile(Edit::LOAD);
    }

    void TextEditor::redraw() {
        // Width first: console_cols is a parameter, so a redraw may be the
        // first thing to run at a new geometry and the row breaks are stale.
        _rebuild_layout();
        _reconcile();           // a page switch handed the band back; reclaim it
    }

    // layout

    int32_t TextEditor::_cap(RowPrompt p) const {
        const int32_t cols = _terminal->cols();
        return Utils::max<int32_t>(cols - _lead(p), 1);
    }

    uint16_t TextEditor::_row_cap(uint16_t row_idx) const {
        return (uint16_t)_cap(_row_prompt(row_idx));
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

    uint16_t TextEditor::row_text(uint16_t i, char* out, uint16_t cap) const {
        if (i >= _row_count) return 0;
        const RowPrompt p    = _row_prompt(i);
        const uint16_t  plen = Utils::min(_prompt_len(p), cap);
        const uint16_t  tlen = Utils::min(_rows[i].buf_len, (uint16_t)(cap - plen));
        memcpy(out, _prompt_buf(p), plen);
        memcpy(out + plen, &_buf[_rows[i].buf_start], tlen);
        return (uint16_t)(plen + tlen);
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

    void TextEditor::_set_origin(uint16_t row) {
        uint16_t max_row = (uint16_t)(_console->rows() - 1);
        _origin = row > max_row ? max_row : row;
    }

    void TextEditor::set_prompts(const char* ps1, const char* ps2, const char* ps3) {
        _set_ps(RowPrompt::PS1, ps1, (uint16_t)strlen(ps1));
        _set_ps(RowPrompt::PS2, ps2, (uint16_t)strlen(ps2));
        _set_ps(RowPrompt::PS3, ps3, (uint16_t)strlen(ps3));
    }

    void TextEditor::as_prompt(uint16_t bottom_row) {
        _anchor = Anchor::BOTTOM; _view_rows = 0; _indent = 0; _single_line = false;
        _set_origin(bottom_row);
    }

    void TextEditor::as_viewport(uint16_t top_row, uint16_t rows) {
        _anchor = Anchor::TOP; _view_rows = rows; _indent = 0; _single_line = false;
        _set_origin(top_row);
    }

    void TextEditor::as_field(uint16_t row, uint16_t indent) {
        _anchor = Anchor::BOTTOM; _view_rows = 0; _indent = indent; _single_line = true;
        set_prompts("", "", "");
        _set_origin(row);
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
        Edit e = _capture(_pos, 0, buf, len);

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
        _reconcile(e);
    }

    void TextEditor::remove_characters(uint16_t len) {
        if (len == 0 || _pos >= _len) return;
        if (_pos + len > _len) len = (uint16_t)(_len - _pos);

        Edit e = _capture(_pos, len, nullptr, 0);
        uint16_t safe_len = (uint16_t)(_len - (_pos + len));

        if (safe_len > 0) memmove(&_buf[_pos], &_buf[_pos + len], safe_len);
        _len -= len;
        _buf[_len] = '\0';

        _rebuild_layout();
        _dirty = true;
        _goal_valid = false;
        _mark_set   = false;    // transient mark: an edit drops the region
        _reconcile(e);
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

    TextEditor::Key TextEditor::key_kind(InputEvent::Command cmd) const {
        using Command = InputEvent::Command;
        switch (cmd) {
            case Command::BACKSPACE: case Command::DELETE: case Command::CTRL_D:
            case Command::CTRL_K:    case Command::CTRL_W: case Command::CTRL_Y:
                return Key::CHANGED;
            case Command::LEFT:  case Command::CTRL_B: case Command::RIGHT: case Command::CTRL_F:
            case Command::HOME:  case Command::CTRL_A: case Command::END:   case Command::CTRL_E:
            case Command::ALT_B: case Command::ALT_F:
            case Command::CTRL_SPACE: case Command::CTRL_G: case Command::ALT_W:
                return Key::MOVED;
            case Command::UP:   case Command::CTRL_P: case Command::DOWN: case Command::CTRL_N:
            case Command::PAGE_UP: case Command::PAGE_DOWN:
                return _single_line ? Key::IGNORED : Key::MOVED;     // no rows to move between
            default:
                return Key::IGNORED;
        }
    }

    TextEditor::Key TextEditor::handle_key(InputEvent::Command cmd, uint16_t count) {
        using Command = InputEvent::Command;
        const Key kind = key_kind(cmd);
        if (kind == Key::IGNORED) return kind;
        switch (cmd) {
            case Command::BACKSPACE:  backspace(count);             break;
            case Command::DELETE:
            case Command::CTRL_D:     delete_forward(count);        break;
            case Command::CTRL_K:     (void)kill_to_line_end();     break;
            case Command::CTRL_W:     (void)kill_region_or_word();  break;
            case Command::CTRL_Y:     (void)yank();                 break;
            case Command::LEFT:
            case Command::CTRL_B:     move_left();                  break;
            case Command::RIGHT:
            case Command::CTRL_F:     move_right();                 break;
            case Command::HOME:
            case Command::CTRL_A:     move_to_line_start();         break;
            case Command::END:
            case Command::CTRL_E:     move_to_line_end();           break;
            case Command::ALT_B:      move_word_left();             break;
            case Command::ALT_F:      move_word_right();            break;
            case Command::CTRL_SPACE: toggle_mark();                break;
            case Command::CTRL_G:     clear_mark();                 break;
            case Command::ALT_W:      (void)copy_region();          break;
            case Command::UP:
            case Command::CTRL_P:     move_up();                    break;
            case Command::DOWN:
            case Command::CTRL_N:     move_down();                  break;
            case Command::PAGE_UP:    page_up();                    break;
            case Command::PAGE_DOWN:  page_down();                  break;
            default: break;
        }
        return kind;
    }

    // bulk content

    bool TextEditor::commit_loaded(uint16_t len) {
        const bool fitted = _load(_buf, len, 0);
        _dirty = false;
        _reconcile(Edit::LOAD);
        return fitted;
    }

    void TextEditor::replace_all(const char* text, uint16_t len) {
        if (text == nullptr) len = 0;
        (void)_load(text, len, len);    // a recalled line is ready to be extended
        _dirty = true;
        _reconcile(Edit::LOAD);
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

    // what the edit did

    TextEditor::Edit TextEditor::_capture(uint16_t pos, uint16_t removed,
                                          const char* text, uint16_t len) const {
        Edit e;
        e.pos   = pos;
        e.delta = removed != 0 ? (int16_t)-(int16_t)removed : (int16_t)len;
        e.a     = _owner_row(pos);
        e.L     = _line_end(_owner_row((uint16_t)(pos + removed)));
        e.row_a = _rows[e.a];
        e.row_b = e.a + 1 < _row_count ? _rows[e.a + 1] : RowView{ _len, 0, false };
        e.row_L = _rows[e.L];
        e.ps_a  = _prompt_len(_row_prompt(e.a));
        e.count = _row_count;
        const bool newline = removed != 0 ? memchr(&_buf[pos], '\n', removed) != nullptr
                                          : memchr(text, '\n', len) != nullptr;
        if (!newline)                       e.kind = Edit::PLAIN;
        else if (removed == 0 && len == 1)  e.kind = Edit::SPLIT;
        else if (removed == 1)              e.kind = Edit::JOIN;
        else                                e.kind = Edit::OTHER;
        return e;
    }

    void TextEditor::_band(const Edit& e, int32_t T, int32_t B) {
        const int32_t lo = Utils::min(_origin, e.origin_was);
        const int32_t hi = _view_bottom_hw();
        int32_t top = lo, bottom = hi, n = e.m0;
        if (e.m0 != e.mB) {
            // The side above the edit and the side below moved differently; the
            // band is whichever holds more rows, and the walk writes the other.
            // A band moving up ends on the last row it empties, one moving down
            // starts on the first, so the new row lands where the old one was.
            int32_t t_bot = -1, b_top = hi + 1;
            if (e.m0 != 0 && T >= 0)      t_bot = Utils::max(e.was(T), _now(T));
            if (e.mB != 0 && B < e.count) b_top = Utils::min(e.was(B), _now(B + e.d));
            if (t_bot - lo >= hi - b_top) { bottom = t_bot; }
            else                          { top = b_top; n = e.mB; }
        }
        if (top < lo) top = lo;
        if (bottom > hi) bottom = hi;
        if (n == 0 || top >= bottom) return;
        _terminal->change(LayoutChange::scroll((uint16_t)top, (uint16_t)bottom, (int16_t)n));
    }

    void TextEditor::_ripple(int32_t top, int32_t bottom, int32_t from, int32_t to, int32_t tail) {
        const int32_t  lead = _lead(RowPrompt::PS2);
        const int32_t  n    = to - from;
        const int32_t  mag  = std::abs(n);
        const uint16_t cols = _terminal->cols();
        if (n == 0 || mag >= cols - lead) return;
        // Above the viewport the run starts on its first row, as a continuation
        // row; below it, it ends on the last, which is then a full one.
        if (top < (int32_t)_origin) { top = _origin; from = n < 0 ? lead + mag : lead; to = from + n; }
        if (bottom > (int32_t)_view_bottom_hw()) { bottom = _view_bottom_hw(); tail = cols - lead - mag; }
        if (top > bottom || tail < 0) return;
        _terminal->change(LayoutChange::ripple((uint16_t)top, (uint16_t)bottom,
                                               (uint16_t)Utils::min(from, to), (uint16_t)lead,
                                               (uint16_t)tail, (int16_t)n));
    }

    // What a shift on an old row carries: its text less the |n| cells pulled
    // off it. n is the ripple's own n, so a realisation prices and emits off
    // one number.
    static inline int32_t _carried(int32_t len, int32_t n) { return n < 0 ? len + n : len; }

    // What one rippled row costs: the cells it moves, and the escape that moves
    // them. A right shift is an insert, and IRM survives cursor motion, so one
    // SM4 serves the frame; a left shift carries its own DCH. A ripple of no
    // cells is never emitted.
    static inline int32_t _rippled(int32_t n) {
        return n == 0 ? 0 : (n > 0 ? MOVE_BYTES : SHIFT_BYTES) + std::abs(n);
    }

    void TextEditor::_complete(Edit& e) const {
        e.m0 = ((int32_t)_origin - e.origin_was) - ((int32_t)_view_top - e.view_top_was);
        e.d  = (int32_t)_row_count - e.count;
        e.mB = e.m0 + e.d;
        // A motion and a load captured no rows, so there is no edit point to
        // place and row_a holds nothing to read.
        if (e.kind == Edit::NONE || e.kind == Edit::LOAD) return;
        e.off   = (int32_t)e.pos - e.row_a.buf_start;
        e.col_a = _indent + e.ps_a + e.off;
    }

    void TextEditor::_announce(const Edit& e) {
        switch (e.kind) {
        // A load moved no content, but a bottom-anchored editor that shrank
        // gave rows back: the band blanks them and keeps the prompt row.
        case Edit::LOAD:  if (_anchor == Anchor::BOTTOM && e.m0 > 0) _band(e, 0, 0); return;
        case Edit::NONE:  if (e.m0 != 0) _band(e, 0, 0); return;
        case Edit::PLAIN: _announce_plain(e); return;
        case Edit::SPLIT: _announce_split(e); return;
        case Edit::JOIN:  _announce_join(e);  return;
        case Edit::OTHER: _band(e, e.a, e.L + 1); return;
        }
    }

    void TextEditor::_announce_plain(const Edit& e) {
        // Cells entered or left at the edit point and every row of the line
        // down to its end took the overflow of the row above; the rows after
        // the line moved as one. One realisation, so nothing to price.
        const int32_t a = e.a, L = e.L, n = e.delta, mag = std::abs(n), now_L = L + e.d;
        _band(e, Utils::min(L, L + e.d), L + 1);
        int32_t carry;
        if (n > 0) carry = e.d > 0 ? 0 : (now_L == a ? e.row_a.buf_len - e.off : e.row_L.buf_len);
        else       carry = now_L == a ? e.row_a.buf_len - e.off - mag
                                      : _carried(now_L == L ? e.row_L.buf_len : _cap(RowPrompt::PS2), n);
        _ripple(_now(a), _now(now_L), n < 0 ? e.col_a + mag : e.col_a, n < 0 ? e.col_a : e.col_a + mag, carry);
    }

    void TextEditor::_announce_split(const Edit& e) {
        // Row a split at the edit point. Two ways to put that on screen: the
        // tail is pushed onto the head of the next row and the line ripples on
        // from there; or a row opens over the tail, takes the head, and the
        // tail slides to the PS3 column. Each sum below is the screen work its
        // realisation needs, at the prices the terminal bills for it.
        const int32_t a = e.a, L = e.L, d = e.d, len_L = e.row_L.buf_len;
        const int32_t head = e.off, tail = e.row_a.buf_len - e.off;
        const int32_t below_a = L - a, now_L = L + d;
        const int32_t lead2 = _lead(RowPrompt::PS2), lead3 = _lead(RowPrompt::PS3);

        // push: old row a + 1 becomes the PS3 row, so its text starts n_push
        // further right and every row of the line under it follows.
        const int32_t n_push   = (lead3 + tail) - lead2;
        const int32_t new_row  = SLIDE_BYTES + MOVE_BYTES + (lead3 - _indent) + tail;
        const int32_t pushed   = below_a * _rippled(n_push);
        const int32_t reflowed = d == 1 && L + 1 < e.count ? PAIR_BYTES : 0;
        const int32_t push     = new_row + pushed + reflowed;

        // over: a row opens above row a and takes its prompt and head; old
        // row a keeps the tail alone, n_over cells left of where it sat.
        const int32_t n_over   = lead3 - e.col_a;
        const int32_t opened   = PAIR_BYTES + e.ps_a + head;
        const int32_t tail_row = _rippled(n_over);
        const int32_t followed = below_a * _rippled(n_over);
        const int32_t over     = opened + tail_row + followed;

        // Under the carry floor the terminal writes a row rather than shift
        // it, and the push is priced for a shift it would not get.
        if (push <= over && _cap(RowPrompt::PS2) - n_push >= (int32_t)AnsiTerminal::SHIFT_MIN_CARRY) {
            _band(e, L, L + 1);
            _ripple(_now(a + 1), _now(L), lead2, lead3 + tail, len_L);
        } else {
            _band(e, a - 1, a);
            if (below_a == 0) _ripple(_now(a + 1), _now(a + 1), e.col_a, lead3, tail);
            else if (d == 1)  _ripple(_now(a + 1), _now(now_L), e.col_a, lead3, _carried(len_L, n_over));
        }
    }

    void TextEditor::_announce_join(const Edit& e) {
        // The line after row a joined it. Two ways to put that on screen: row
        // a keeps its place and takes what fits of the next row, which is
        // written, and the rest of the line pulls left after it; or row a's
        // screen row goes, its text is written over the head of the next row,
        // whose text slides right by that much, and the rest of the line
        // pushes on from there. Priced the way a split's two are.
        const int32_t a = e.a, L = e.L, d = e.d;
        const int32_t len_a = e.row_a.buf_len, len_b = e.row_b.buf_len, len_L = e.row_L.buf_len;
        const int32_t below_a = L - a;
        const int32_t lead2 = _lead(RowPrompt::PS2), lead3 = _lead(RowPrompt::PS3);
        const int32_t cap_a = Utils::max<int32_t>(_terminal->cols() - _indent - e.ps_a, 1);
        const int32_t take  = Utils::min(len_b, cap_a - len_a);   // cells of the next row that fit after row a's text
        const int32_t col_p = _indent + e.ps_a + len_a;           // column row a's text ends at
        const bool    whole = below_a == 1 && take == len_b;      // the next row fits on row a

        // pull: what row a takes is written after its text, and the rest of
        // the line comes n_pull cells left to close the gap it left.
        const int32_t n_pull = lead2 - (lead3 + take);
        const int32_t taken  = MOVE_BYTES + take;
        const int32_t closed = whole ? PAIR_BYTES : below_a * _rippled(n_pull);
        const int32_t pull   = taken + closed;

        // push: row a's screen row goes, and on old row a + 1 the walk writes
        // the cells left of the shift column before the text there goes
        // n_push right, the rest of the line following.
        const int32_t n_push  = col_p - lead3;
        const int32_t dropped = PAIR_BYTES;
        const int32_t ahead   = MOVE_BYTES + (lead3 - _indent);
        const int32_t pushed  = below_a * _rippled(n_push);
        const int32_t push    = dropped + ahead + pushed;

        if (push < pull && (whole ? take : len_L) >= (int32_t)AnsiTerminal::SHIFT_MIN_CARRY) {
            // Old row a+1 becomes row a, its line's rows move up one; when the
            // push overflows old row L into a new row, the rows after the line
            // stay and the band is the line's rows alone.
            if (d == -1) _band(e, a - 1, a + 1);
            else _terminal->change(LayoutChange::scroll((uint16_t)Utils::max<int32_t>(e.was(a), _origin),
                                                        (uint16_t)Utils::min<int32_t>(e.was(L), _view_bottom_hw()), -1));
            _ripple(_now(a), _now(L - 1), lead3, col_p, len_L);
            return;
        }
        if (whole) { _band(e, a, a + 2); return; }
        _band(e, Utils::min(L, L + d), L + 1);
        const int32_t now_L = L + d;
        const int32_t carry = now_L == a + 1 ? len_b - take
                                             : _carried(now_L == L ? len_L : _cap(RowPrompt::PS2), n_pull);
        _ripple(_now(a + 1), _now(now_L), lead3 + take, lead2, carry);
    }

    // reconciliation

    void TextEditor::_reconcile(Edit& e) {
        e.origin_was = _origin; e.view_top_was = _view_top;
        if (e.kind == Edit::NONE || e.kind == Edit::LOAD) e.count = _row_count;     // nothing added: the layout is the one painted
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
        // _complete() reads _origin and _view_top where they landed, against
        // the origin_was and view_top_was taken above: the order is the
        // measurement.
        _complete(e);
        _announce(e);
        _paint_view();
        _place_cursor(_map_cursor_pos(_pos));
    }

}}

#endif // BOTANIK_FEATURE_CONSOLE
