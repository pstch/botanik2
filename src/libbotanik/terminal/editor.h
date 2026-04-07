#ifndef TERMINAL_EDITOR_H
#define TERMINAL_EDITOR_H

#include <cstdint>

#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Terminal {

    namespace {
        using namespace Botanik::Utils;
        using Botanik::Terminal::Ansi::CursorPos;
    }

    struct ChangeScript {
        uint16_t    head_pos;
        uint16_t    head_cols;

        uint16_t    body_pos;
        uint16_t    body_rows;

        uint16_t    tail_pos;
        uint16_t    tail_cols;

        int16_t     delta;
        uint16_t    span;
    };

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

        // TODO
        void _optimized_move(const CursorPos& target);

        // TODO
        CursorPos _map_cursor_pos(uint16_t pos) const;

        // TODO
        uint16_t _write_prompt_for_row(uint16_t row);

        // TODO
        void _trigger_hardware_scroll();

        // TODO
        CursorPos _ensure_visible(uint16_t pos);

        // TODO
        ChangeScript _generate_script(uint16_t pos, int16_t delta, uint16_t len = 0);

        // TODO
        void _write_text(uint16_t pos, uint16_t len);

        // TODO
        void _execute_script(const ChangeScript& script, bool appending);

        // TODO
        void _refresh(uint16_t start_pos, int16_t delta, bool clear, bool append);
    public:
        // TODO
        InputEditor(BufferedSerial& serial, uint16_t cols, uint16_t rows);

        // TODO
        uint16_t occupied_rows() const;

        // TODO
        void set_ps1(char* buf, uint16_t len);
        // TODO
        void set_ps2(char* buf, uint16_t len);

        // TODO
        void set_origin(uint16_t row, int16_t shift = 0);
        // TODO
        void set_contents(char* buf, uint16_t len);

        // TODO
        void reset();
        // TODO
        void redraw(bool clear = true);

        // TODO
        void remove_characters(uint16_t len);
        // TODO
        void insert_characters(char* buf, uint16_t len);
        // TODO
        void select_characters(uint16_t anchor, uint16_t len);

    };

}}

#endif  // TERMINAL_EDITOR_H
