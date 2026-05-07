
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik/terminal/ansi.h"
#include "libbotanik/terminal/editor.h"


namespace Botanik { namespace Terminal {

    namespace {
        using Botanik::Terminal::Ansi::CursorPos;
    }

    InputEditor::InputEditor(AnsiTerminal* terminal) : _terminal(terminal) {
        _buf[0] = '\0';
        _terminal->set_auto_wrap(false);
    }

    CursorPos InputEditor::_map_cursor_pos(uint16_t pos) const {
        uint16_t row = _origin;
        uint16_t col = _ps1_len;
        uint16_t width = _terminal->cols();

        for (uint16_t i = 0; i < pos; i++) {
            if (_buf[i] == '\n') {
                row++; col = _ps2_len;
            } else {
                if (col >= width) { row++; col = _ps2_len; }
                col++;
            }
        }
        return { row, col };
    }

    uint16_t InputEditor::_get_row_cap(uint16_t row) const {
        return (row == 0) ? _terminal->cols() - _ps1_len : _terminal->cols() - _ps2_len;
    }

    RowView InputEditor::_scan_row(uint16_t pos, uint16_t cap) const {
        // Returns buf_start = 0; caller (_build_rows) fills it in.
        uint16_t len = 0;
        while (len < cap && (pos + len) < _len && !_is_newline(pos + len)) len++;
        bool hard_break = (pos + len < _len) && _is_newline(pos + len);
        return RowView { 0, len, hard_break };
    }

    TextView InputEditor::_build_rows(uint16_t buf_pos) const {
        TextView res;
        uint16_t pos = 0;
        for (uint16_t i = 0; pos <= _len && res.row_count < TextView::MAX_ROWS; ++i) {
            RowView row = _scan_row(pos, _get_row_cap(i));
            uint16_t stride = row.buf_len + (row.hard_break ? 1 : 0);
            // Only store rows that contain or follow the edit position
            if (pos + stride > buf_pos || (pos == _len && buf_pos == _len)) {
                row.buf_start = pos;
                if (res.row_count == 0) res.start_row = i;
                res.rows[res.row_count++] = row;
            }
            // Advance to the start of the next row
            pos += stride;
            if (pos > _len || stride == 0) break;
        }
        return res;
    }

    void InputEditor::set_contents(const char* buf, uint16_t len) {
        if (len >= sizeof(_buf)) len = sizeof(_buf) - 1;
        bool clear = len < _len;
        memcpy(_buf, buf, len);
        _buf[len] = '\0';
        _len = _pos = len;
        redraw(clear);
    }

    void InputEditor::reset() {
        _terminal->move_to({_origin, 0});
        set_contents("", 0);
        _occupied = 1;
    }

    void InputEditor::remove_characters(uint16_t len) {
        if (len == 0 || _pos >= _len) return;
        if (_pos + len > _len) len = _len - _pos;

        TextView old_geo  = _build_rows(_pos);
        CursorPos edit_pos = _map_cursor_pos(_pos);

        uint16_t tail_len = _len - (_pos + len);
        if (tail_len > 0) memmove(&_buf[_pos], &_buf[_pos + len], tail_len);
        _len -= len;
        _buf[_len] = '\0';

        _do_removal(edit_pos, old_geo, len);
        _occupied = _map_cursor_pos(_len).row - _origin + 1;
    }

    bool InputEditor::_remove_selection() {
        if (_sel_len == 0) return false;
        _pos = _sel_anchor;
        remove_characters(_sel_len);
        _sel_anchor = 0;
        _sel_len    = 0;
        _mark_active = false;
        _terminal->move_to(_map_cursor_pos(_pos));
        return true;
    }

    void InputEditor::insert_characters(char* buf, uint16_t len) {
        _remove_selection();
        if (len == 0) return;
        if ((_len + len) >= sizeof(_buf)) len = sizeof(_buf) - _len - 1;

        TextView old_geo   = _build_rows(_pos);
        CursorPos edit_pos = _map_cursor_pos(_pos);

        if (_pos < _len) memmove(&_buf[_pos + len], &_buf[_pos], _len - _pos);
        memcpy(&_buf[_pos], buf, len);
        _len += len;
        _buf[_len] = '\0';

        _pos += len;
        _do_insertion(edit_pos, old_geo, len);
        _occupied = _map_cursor_pos(_len).row - _origin + 1;
    }

    void InputEditor::replace_characters(char* buf, uint16_t len) {
        if (len == 0 || _pos >= _len) return;
        uint16_t replace_len = std::min(len, (uint16_t)(_len - _pos));
        memcpy(&_buf[_pos], buf, replace_len);
        _pos += replace_len;
        CursorPos start = _map_cursor_pos(_pos - replace_len);
        _terminal->move_to(start);
        _terminal->write_chars(&_buf[_pos - replace_len], replace_len);
    }

    void InputEditor::append_characters(char* buf, uint16_t len) {
        if (len == 0) return;
        _pos = _len;
        insert_characters(buf, len);
    }

    void InputEditor::select_characters(uint16_t anchor, uint16_t len) {
        bool has_damage = (_sel_len > 0 || len > 0);
        uint16_t dmg_start, dmg_end;
        if (_sel_len > 0 && len > 0) {
            dmg_start = std::min(_sel_anchor, anchor);
            dmg_end   = std::max((uint16_t)(_sel_anchor + _sel_len), (uint16_t)(anchor + len));
        } else if (_sel_len > 0) {
            dmg_start = _sel_anchor; dmg_end = _sel_anchor + _sel_len;
        } else {
            dmg_start = anchor; dmg_end = anchor + len;
        }

        _sel_anchor = anchor;
        _sel_len    = len;

        if (has_damage && dmg_start < _len) {
            dmg_end = std::min(dmg_end, _len);
            uint16_t saved_pos = _pos;
            _pos = dmg_start;
            replace_characters(&_buf[dmg_start], dmg_end - dmg_start);
            _pos = saved_pos;
        }
    }

    void InputEditor::set_mark() {
        _mark_active = true;
        _mark_pos    = _pos;
        _sel_anchor  = _pos;
        _sel_len     = 0;
    }

    void InputEditor::move_left() {
        if (_pos == 0) { _mark_active = false; _sel_anchor = 0; _sel_len = 0; return; }
        _pos--;
        if (_mark_active) {
            uint16_t start = std::min(_mark_pos, _pos);
            select_characters(start, std::max(_mark_pos, _pos) - start);
        } else {
            _sel_anchor = 0; _sel_len = 0;
        }
        _terminal->move_to(_map_cursor_pos(_pos));
    }

    void InputEditor::move_right() {
        if (_pos >= _len) { _mark_active = false; _sel_anchor = 0; _sel_len = 0; return; }
        _pos++;
        if (_mark_active) {
            uint16_t start = std::min(_mark_pos, _pos);
            select_characters(start, std::max(_mark_pos, _pos) - start);
        } else {
            _sel_anchor = 0; _sel_len = 0;
        }
        _terminal->move_to(_map_cursor_pos(_pos));
    }

    void InputEditor::backspace() {
        if (_remove_selection()) return;
        if (_pos == 0) return;
        _pos--;
        remove_characters(1);
    }

    void InputEditor::redraw(bool clear) {
        if (clear) {
            for (uint16_t i = 0; i < _occupied; i++)
                _terminal->clear_row(_origin + i);
        }
        _terminal->move_to({_origin, 0});
        _write_ps1();
        if (_len > 0) {
            _do_insertion({_origin, _ps1_len}, TextView::empty(), _len);
            _occupied = _map_cursor_pos(_len).row - _origin + 1;
        }
        _terminal->move_to(_map_cursor_pos(_pos));
    }

    uint16_t InputEditor::_shift_insert(uint16_t row, uint16_t pos,
                                        uint16_t pressure, const TextView& geometry) {
        uint16_t prompt_row = geometry.start_row + row;
        uint16_t cap     = _get_row_cap(prompt_row);
        uint16_t old_len = (row < geometry.row_count) ? geometry.rows[row].buf_len : 0;
        uint16_t overflow = (old_len + pressure > cap) ? (old_len + pressure - cap) : 0;

        if (pressure < cap / 2) {
            // ICH via SM4: write 'pressure' chars in insert mode, opening a gap that
            // pushes the existing on-screen row content right; overflow falls off the edge.
            _terminal->write_chars(&_buf[pos], pressure, /*insert=*/true);
        } else {
            // IL: insert a blank line, pushing old row content down; rewrite the full row.
            _terminal->insert_rows(1);
            _terminal->write_chars(&_buf[pos], std::min((uint16_t)(old_len + pressure), cap));
        }
        return overflow;
    }

    uint16_t InputEditor::_shift_remove(uint16_t row, uint16_t pos,
                                        uint16_t vacuum, const TextView& geometry) {
        uint16_t prompt_row   = geometry.start_row + row;
        uint16_t cap          = _get_row_cap(prompt_row);
        uint16_t next_buf_len = ((row + 1) < geometry.row_count) ? geometry.rows[row + 1].buf_len : 0;
        uint16_t fill         = std::min(vacuum, next_buf_len);
        RowView  new_row      = _scan_row(pos, cap);

        if (vacuum < cap / 2) {
            // DCH: delete 'vacuum' chars at cursor, shifting the rest of the line left;
            // a blank gap of 'vacuum' chars opens at the right edge.
            _terminal->delete_chars(vacuum);
            // Advance past the already-correct content to the right-edge gap, then fill it.
            const char* fill_src = &_buf[pos + new_row.buf_len + (new_row.hard_break ? 1 : 0)];
            if (fill > 0) {
                if (new_row.buf_len > 0)
                    _terminal->move_cursor(RIGHT, new_row.buf_len);
                _terminal->write_chars(fill_src, fill);
            }
        } else {
            // DL: delete this line, pulling subsequent rows up; old content of the next
            // row is now on screen at this row and will be corrected by the next ripple step.
            _terminal->delete_rows(1);
            _terminal->write_chars(&_buf[pos], new_row.buf_len);
            // Clear remaining chars that were pulled up from the old next row.
            _terminal->clear_to_eol();
        }
        return vacuum - fill;
    }

    int16_t InputEditor::_shift_row(uint16_t row, uint16_t pos,
                                    int16_t pressure, const TextView& geometry) {
        if (pressure > 0)
            return (int16_t)_shift_insert(row - geometry.start_row, pos, (uint16_t)pressure, geometry);
        if (pressure < 0)
            return -(int16_t)_shift_remove(row - geometry.start_row, pos, (uint16_t)-pressure, geometry);
        return 0;
    }

    void InputEditor::_do_insertion(CursorPos pos, const TextView& geometry,
                                    uint16_t pressure) {
        uint16_t new_buf_pos = geometry.rows[0].buf_start;

        for (uint16_t i = 0; i < geometry.row_count; i++) {
            uint16_t prompt_row = geometry.start_row + i;
            uint16_t col        = (prompt_row == 0) ? _ps1_len : _ps2_len;
            uint16_t cap        = _get_row_cap(prompt_row);
            uint16_t shift_pos  = new_buf_pos;
            CursorPos target    = { (uint16_t)(_origin + prompt_row), col };
            if (i == 0 && pressure < cap / 2) {
                shift_pos = new_buf_pos + (pos.col - col);
                target    = pos;
            }
            _terminal->move_to(target);

            pressure = (uint16_t)_shift_row(prompt_row, shift_pos, (int16_t)pressure, geometry);
            RowView row = _scan_row(new_buf_pos, cap);
            new_buf_pos += row.buf_len + (row.hard_break ? 1 : 0);
            if (pressure == 0) break;
        }

        // Overflow past the captured geometry: write remaining chars on new screen lines.
        for (uint16_t j = 0; pressure > 0; j++) {
            uint16_t prompt_row = geometry.start_row + geometry.row_count + j;
            _terminal->line_feed();
            _write_ps2();
            RowView row = _scan_row(new_buf_pos, _get_row_cap(prompt_row));
            _terminal->write_chars(&_buf[new_buf_pos], row.buf_len);
            uint16_t stride = row.buf_len + (row.hard_break ? 1 : 0);
            if (stride == 0) break;
            pressure -= std::min(pressure, stride);
            new_buf_pos += stride;
        }
    }

    void InputEditor::_do_removal(CursorPos pos, const TextView& old,
                                  uint16_t pressure) {
        uint16_t new_buf_pos = old.rows[0].buf_start;

        for (uint16_t i = 0; i < old.row_count; i++) {
            uint16_t prompt_row = old.start_row + i;
            uint16_t col        = (prompt_row == 0) ? _ps1_len : _ps2_len;
            uint16_t cap        = _get_row_cap(prompt_row);
            uint16_t shift_pos  = new_buf_pos;
            CursorPos target    = { (uint16_t)(_origin + prompt_row), col };
            if (i == 0 && pressure < cap / 2) {
                shift_pos = new_buf_pos + (pos.col - col);
                target    = pos;
            }
            _terminal->move_to(target);

            int16_t remaining = _shift_row(prompt_row, shift_pos, -(int16_t)pressure, old);
            pressure = (uint16_t)(-remaining);
            RowView row = _scan_row(new_buf_pos, cap);
            new_buf_pos += row.buf_len + (row.hard_break ? 1 : 0);
            if (remaining == 0) break;
        }

        // Clear any screen rows that are no longer occupied after the deletion.
        // This covers rows vacated by the content shrinking AND rows left stale by DL
        // pull-up operations that moved content above the end of the new buffer.
        uint16_t new_last_row = _map_cursor_pos(_len).row;
        for (uint16_t r = new_last_row + 1; r < _origin + _occupied; r++)
            _terminal->clear_row(r);
    }

}}
