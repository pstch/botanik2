
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string.h>
#include <cmsis_compiler.h>

#include "editor.h"
#include "ansi.h"

using std::min;
using std::max;

using namespace Ansi;

// Find the length of a string, up to a maximum number of characters.
// This is used to find the true end of a string that has been padded with null bytes.
inline size_t strnlen(const char *s, size_t maxlen) {
    size_t len = 0;
    while (len < maxlen && s[len] != '\0') len++;
    return len;
}

/** TODO
 * 
 */
class InputEditor {
private:
    // Serial device buffered wrapper
    BufferedSerial&     _serial;

    // Display size
    uint16_t            _cols;
    uint16_t            _rows;

    // Logical view of the current input
    char                _buf[512];
    uint16_t            _len = 0;
    uint16_t            _pos = 0;

    // Selection state
    uint16_t            _sel_anchor = 0;
    uint16_t            _sel_len = 0;

    // Internal state
    uint16_t            _origin = 0;
    uint16_t            _occupied = 1;
    CursorPos           _hw_pos = {0, 0};
    bool                _hw_inverted = false;
    
    // Command prompt prefix
    char                _ps1_buf[16];
    uint16_t            _ps1_len = 0;

    // Continuation prefix
    char                _ps2_buf[16];
    uint16_t            _ps2_len = 0;

    // Move the cursor to the given position using as few bytes as possible
    void _optimized_move(const CursorPos& target) {
        if (target == _hw_pos) { return; }
        else if (target.row == _hw_pos.row) {
            if (target.col == 0) {
                Ansi::move_line_begin(_serial);
            } else if (target.col < _hw_pos.col && (_hw_pos.col - target.col) < 4) {
                Ansi::move_left(_serial, _hw_pos.col - target.col);
            } else if (target.col > _hw_pos.col && (target.col - _hw_pos.col) < 4) {
                Ansi::move_right(_serial, target.col - _hw_pos.col);
            } else {
                Ansi::goto_absolute(_serial, target);
            }
        } else if (target.col == _hw_pos.col) {
            if (target.row < _hw_pos.row && (_hw_pos.row - target.row) < 4) {
                Ansi::move_up(_serial, _hw_pos.row - target.row);
            } else if (target.row > _hw_pos.row && (target.row - _hw_pos.row) < 4) {
                Ansi::move_down(_serial, target.row - _hw_pos.row);
            } else {
                Ansi::goto_absolute(_serial, target);
            }
        } else if (target.col == 0) {
            if (target.row < _hw_pos.row && (_hw_pos.row - target.row) < 4) {
                Ansi::move_line_begin(_serial);
                Ansi::move_up(_serial, _hw_pos.row - target.row);
            } else if (target.row > _hw_pos.row && (target.row - _hw_pos.row) < 4) {
                Ansi::move_line_begin(_serial);
                Ansi::move_down(_serial, _hw_pos.row - target.row);
            } else {
                Ansi::goto_absolute(_serial, target);
            }
        } else {
            Ansi::goto_absolute(_serial, target);
        }
        _hw_pos = target;
    }

    // Map a position to an absolute on-screen cursor position
    CursorPos _map_cursor_pos(uint16_t pos) const {
        // NOTE: this only supports ASCII characters, 
        //       multi-byte characters would need more work
        uint16_t l1_len = _cols - _ps1_len;
        uint16_t lN_len = _cols - _ps2_len;
        if (pos < l1_len) return { _origin, _ps1_len + pos };
        pos -= l1_len;
        return { _origin + 1 + (pos / lN_len), _ps2_len + (pos % lN_len) };
    }

    // Write the prompt for the given row
    uint16_t _write_prompt_for_row(uint16_t row) {
        if (row == _origin) {
            _serial.write(_ps1_buf, _ps1_len);
            return _ps1_len;
        } else {
            _serial.write(_ps2_buf, _ps2_len);
            return _ps2_len;
        }
    }    

    // Generate an edition script for the given
    // change size at the given position. This code is shared
    // between the character insertion and removal paths.
    ChangeScript _generate_script(uint16_t pos, int16_t delta, uint16_t len = 0) {
        // this algorithm will split the length in abs(delta) into
        // three sections : head, body and tail
        CursorPos cursor = _map_cursor_pos(pos);
        ChangeScript script;        
        script.span = (len > 0) ? len : abs(delta);

        // compute head slice
        uint16_t head_cols = _cols - cursor.col;
        script.head_pos = pos;
        script.head_cols = min(len, head_cols);
        len -= script.head_cols;
        
        // compute body slice
        uint16_t body_cols = _cols - _ps2_len;         
        script.body_pos = pos + script.head_cols;
        script.body_rows = len / body_cols;
        uint16_t body_len = script.body_rows * body_cols;
        len -= body_len;
        
        // compute tail slice
        script.tail_pos = script.body_pos + body_len;
        script.tail_cols = len;

        script.delta = delta;
        
        return script;        
    }

    void _write_text(uint16_t pos, uint16_t len) {
        uint16_t sel_start = min(_sel_anchor, (uint16_t)(_sel_anchor + _sel_len));
        uint16_t sel_end = max(_sel_anchor, (uint16_t)(_sel_anchor + _sel_len));
        if (_sel_len != 0) {
            for (uint16_t i = 0; i < len; ++i) {
                uint16_t char_pos = pos + i;
                bool in_sel = (char_pos >= sel_start && char_pos < sel_end);
                if (in_sel != _hw_inverted) {
                    _hw_inverted = in_sel;
                    _serial.write(CSI, 2);
                    if (_hw_inverted) _serial.write(ATTR_NEG, 2);
                    else              _serial.write(ATTR_NONE, 1);
                }
                _serial.write(&_buf[char_pos], 1);
            }
            if (_hw_inverted) {
                _serial.write(CSI, 2);
                _serial.write(ATTR_NONE, 1);
                _hw_inverted = false;
            }
        } else {
            _serial.write(&_buf[pos], len);
        }
    }

    void _execute_script(const ChangeScript& script, bool appending) {
        if (script.delta > 0 && !appending) {
            Ansi::set_insert_mode(_serial, true);
        }

        if (script.head_cols > 0) {
            _optimized_move(_map_cursor_pos(script.head_pos));
            if (script.delta >= 0) {
                _write_text(script.head_pos, script.head_cols);
                _hw_pos = _map_cursor_pos(script.head_pos + script.head_cols);
            } else {
                Ansi::delete_chars(_serial, abs(script.delta));
            }
        }

        if (script.body_rows) {
            if (script.delta >= 0) {
                for (uint16_t row = 0; row < script.body_rows; ++row) {
                    uint16_t row_len = _cols - _ps2_len;
                    uint16_t row_pos = script.body_pos + row * row_len;
                    Ansi::CursorPos cursor = _map_cursor_pos(row_pos);
                    _optimized_move({cursor.row, 0});
                    uint16_t ps_len = _write_prompt_for_row(cursor.row);
                    _hw_pos = {cursor.row, ps_len};
                    _write_text(row_pos, row_len);
                    _hw_pos = _map_cursor_pos(row_pos + row_len);
                };                
            } else if (script.delta < 0) {
                _optimized_move(_map_cursor_pos(script.body_pos));                
                Ansi::delete_rows(_serial, script.body_rows);
            };
        }

        if (script.tail_cols > 0) {
            _optimized_move(_map_cursor_pos(script.tail_pos));
            if (script.delta >= 0)  {
                _write_text(script.tail_pos, script.tail_cols);
                _hw_pos = _map_cursor_pos(script.tail_pos + script.tail_cols);                
            } else {
                Ansi::clear_to_eol(_serial);
            }
        }

        if (script.delta > 0 && !appending) {
            Ansi::set_insert_mode(_serial, false);
        }

    }

    void _refresh(uint16_t start_pos, int16_t delta, bool clear, bool append) {
        if (clear) {
            // Clear prompt lines
            for (int16_t i = _occupied - 1; i >= 0; --i) {
                // Move to the start of the row
                _optimized_move({(uint16_t)(_origin + i), 0});
                // Erase from cursor to end of line
                Ansi::clear_to_eol(_serial);
            }
            uint16_t ps_len = _write_prompt_for_row(_origin);
            _hw_pos = {_origin, ps_len};
        }
        _execute_script(_generate_script(start_pos, delta), append);
        _optimized_move(_map_cursor_pos(_pos));
    }


public:
    InputEditor(BufferedSerial& serial, uint16_t cols, uint16_t rows) : 
        _cols(cols), _rows(rows), _serial(serial) {
            memset(_buf, 0, sizeof(_buf));
        }

    void set_ps1(char* buf, uint16_t len) {
        len = min((uint16_t)(_cols - 1), len);
        memcpy(_ps1_buf, buf, len);
        _ps1_len = len;
    };

    void set_ps2(char* buf, uint16_t len) {
        len = min((uint16_t)(_cols - 1), len);
        memcpy(_ps2_buf, buf, len);
        _ps2_len = len;
    };

    void set_origin(uint16_t row, int16_t shift = 0) {
        row = min((uint16_t)(_rows - 1), row);
        _origin = row;
        _hw_pos.row += shift;
    }

    void set_contents(char* buf, uint16_t len) {
        bool clear = len < _len;
        memcpy(_buf, buf, len);
        _buf[len] = '\0';
        _len = len;
        _pos = len;
        redraw_prompt(clear);
    }

    void reset() {
        Ansi::goto_absolute(_serial, {_origin, 0});
        _occupied = _map_cursor_pos(_len).row - _origin + 1;
        set_contents("", 0);
    }

    /**
     * Return the amount of rows occupied by the prompt
     * 
     * Note that rows are not given back after deleting characters.
     */
    uint16_t occupied_rows() const {
        return _occupied;
    }

    void remove_characters(uint16_t len) {
        // return early if length is zero
        if (len == 0 || _pos >= _len) return;
        // restrict to existing characters in buffer
        if (_pos + len > _len) len = _len - _pos;
        // remove removed characters and update logical state
        uint16_t tail_len = _len - (_pos + len);
        if (tail_len > 0) memmove(&_buf[_pos], &_buf[_pos + len], tail_len);
        _len -= len;
        _buf[_len] = '\0';
        // refresh the terminal screen
        _refresh(_pos, -(int16_t)len, false, false);
    }

    void insert_characters(char* buf, uint16_t len) {
        // return early if length is zero
        if (len == 0) return;
        // truncate if exceeding buffer size
        if ((_len + len) >= sizeof(_buf)) len = sizeof(_buf) - _len - 1;
        // shift existing data to the right
        if (_pos < _len) memmove(&_buf[_pos + len], &_buf[_pos], _len - _pos);
        // detect if we are simply appending
        bool append = _pos == _len;
        // add inserted characters and update logical state
        memcpy(&_buf[_pos], buf, len);
        _len += len;
        _buf[_len] = '\0';
        _occupied = _map_cursor_pos(_len).row - _origin + 1;
        // refresh the terminal screen
        _pos += len;
        _refresh(_pos - len, len, false, append);
    }

    void select_characters(uint16_t anchor, uint16_t len) {
        // capture the pre-bounds
        uint16_t old_min = min(_sel_anchor, (uint16_t)(_sel_anchor + _sel_len));
        uint16_t old_max = max(_sel_anchor, (uint16_t)(_sel_anchor + _sel_len));
        // update selection state
        _sel_anchor = anchor;
        _sel_len = len;
        // capture the post-bounds
        uint16_t new_min = min(_sel_anchor, (uint16_t)(_sel_anchor + _sel_len));
        uint16_t new_max = max(_sel_anchor, (uint16_t)(_sel_anchor + _sel_len));
        // calculate the damaged range
        uint16_t dmg_start = min(old_min, new_min);
        uint16_t dmg_len   = max(old_max, new_max) - dmg_start;
        // update the damaged range using a zero-delta script over the damaged area
        if (dmg_len > 0) {
            _execute_script(_generate_script(dmg_start, 0, dmg_len), false);
            _optimized_move(_map_cursor_pos(_pos));
        }
    }

    void redraw_prompt(bool clear = true) {
        _refresh(0, (int16_t)_len, clear, true);
    }

};
