
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string.h>

#include "libbotanik/terminal/ansi.h"
#include "libbotanik/terminal/editor.h"

using Botanik::Terminal::Ansi::CursorPos;

namespace Botanik { namespace Terminal {

    // Constructors
    // =======================================================================

    InputEditor::InputEditor(AnsiTerminal* terminal) : 
        _terminal(terminal) {
            memset(_buf, 0, sizeof(_buf));
        }

    // InputEditor implementation
    // =======================================================================

    // Private methods
    // -----------------------------------------------------------------------

    // Map a position to an absolute on-screen cursor position
    CursorPos InputEditor::_map_cursor_pos(uint16_t pos) const {
        uint16_t row = _origin;
        uint16_t col = _ps1_len;
        uint16_t width = _terminal->cols();

        for (uint16_t i = 0; i < pos; i++) {
            // line-wrap happens if we hit \n or the right margin
            if (_buf[i] == '\n' || col >= width) {
                row++;
                col = _ps2_len;
            }
            
            // only increment the column for printable characters
            if (_buf[i] != '\n') {
                col++;
            }
        }
        return { row, col };
    }

    uint16_t InputEditor::_get_row_cap(uint16_t row) const {
        uint16_t cols = _terminal->cols();
        return (row == 0) ? cols - _ps1_len : cols - _ps2_len;
    }

    RowView InputEditor::_scan_row(uint16_t pos, uint16_t cap) const {
        // This function is designed to not know the row's starting buffer position,
        // so it returns a buf_start of 0, which the caller (_build_rows) will adjust.
        uint16_t len = 0;
        while (len < cap && (pos + len) < _len && _is_newline(pos + len)) len++;
        bool hard_break = (pos + len < _len) && _is_newline(pos + len);
        return RowView { 0, len, hard_break };
    }    

    TextView InputEditor::_build_rows(uint16_t buf_pos) const {
        TextView res;
        uint16_t pos = 0;
        // Calculate geometry for the current row
        for (uint16_t i = 0; pos <= _len && res.row_count < TextView::MAX_ROWS; ++i) {
            RowView row = _scan_row(pos, _get_row_cap(i));
            uint16_t stride = row.buf_len + (row.hard_break ? 1 : 0);
            // Only store rows that contain or follow the edit position
            bool row_contains_or_follows_cursor = (pos + stride > buf_pos);
            bool both_at_end_of_buffer          = (pos == _len && buf_pos == _len);
            if (row_contains_or_follows_cursor || both_at_end_of_buffer) {
                row.buf_start = pos;
                res.rows[res.row_count++] = row;
            }
            // Advance to the start of the next row
            pos += stride;
            if (pos > _len || stride == 0) break;
        }
        return res;
    }

    // Public interface
    // -----------------------------------------------------------------------

    void InputEditor::set_contents(const char* buf, uint16_t len) {
        bool clear = len < _len;
        memcpy(_buf, buf, len);
        _buf[len] = '\0';
        _len = len;
        _pos = len;
        redraw(clear);
    }

    void InputEditor::reset() {
        _terminal->move_to({_origin, 0});
        _occupied = _map_cursor_pos(_len).row - _origin + 1;
        set_contents("", 0);
    }

    // Input edition methods
    // -----------------------------------------------------------------------at

    void InputEditor::remove_characters(uint16_t len) {
        // return early if length is zero
        if (len == 0 || _pos >= _len) return;
        // restrict to existing characters in buffer
        if (_pos + len > _len) len = _len - _pos;
        // remove removed characters and update logical state
        uint16_t tail_len = _len - (_pos + len);
        if (tail_len > 0) memmove(&_buf[_pos], &_buf[_pos + len], tail_len);
        _len -= len;
        _buf[_len] = '\0';

        // TODO: reflect change on the screen
    }

    void InputEditor::insert_characters(char* buf, uint16_t len) {
        // return early if length is zero
        if (len == 0) return;
        // truncate if exceeding buffer size
        if ((_len + len) >= sizeof(_buf)) len = sizeof(_buf) - _len - 1;
        
        // shift existing data to the right
        if (_pos < _len) memmove(&_buf[_pos + len], &_buf[_pos], _len - _pos);
        // add inserted characters and update logical state
        memcpy(&_buf[_pos], buf, len);
        _len += len;
        _buf[_len] = '\0';

        // TODO: reflect change on the screen
    }

    void InputEditor::replace_characters(char* buf, uint16_t len) {
        // return early if length is zero or nothing to replace
        if (len == 0 || _pos >= _len) return;

        // Clamp 'len' to what can be replaced in place without growing the buffer.
        uint16_t replace_len = std::min(len, (uint16_t)(_len - _pos));
        if (replace_len == 0) return;

        memcpy(&_buf[_pos], buf, replace_len);
        _pos += replace_len;

        // TODO: reflect change on the screen.
        // NOTE: no ripple is needed if the replacement doesn't change line
        // breaks, but a full redraw is the simplest way to handle all cases.
    }

    void InputEditor::append_characters(char* buf, uint16_t len) {
        // return early if length is zero
        if (len == 0) return;
        // truncate if exceeding buffer size
        if ((_len + len) >= sizeof(_buf)) {
            len = sizeof(_buf) - _len - 1;
        }
        if (len == 0) return;

        memcpy(&_buf[_len], buf, len);
        _len += len;
        _pos = _len;
        _buf[_len] = '\0';

        // TODO: reflect change on the screen. This is a simpler case of insertion.
    }

    void InputEditor::select_characters(uint16_t anchor, uint16_t len) {
        // TODO: implement with :
        //  - a new utility function to calculate the selection damage
        //  - dummy replace_characters() on the damaged range
    }

    void InputEditor::redraw(bool clear) {
        // TODO: implement manually
    }

    // Ripple methods
    // -----------------------------------------------------------------------

    void InputEditor::_insertion_ripple(CursorPos edit_pos, const TextView& text, uint16_t pressure) {
        // TODO: implement
    }

    void InputEditor::_removal_ripple(CursorPos edit_pos, const TextView& text, uint16_t pressure) {
        // TODO: implement
    }

}}
