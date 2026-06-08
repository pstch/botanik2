/**
 * @file ansi.cpp
 * @brief ANSI helper implementations — integer-to-decimal emission and the
 *        escape-sequence writers that back the Ansi namespace.
 */
#include <cstdint>

#include "libbotanik/terminal/ansi.h"

namespace Botanik { namespace Terminal { namespace Ansi {
    using BufferedSerial = Botanik::Utils::BufferedSerial;

    void write_uint16(BufferedSerial& _serial, uint16_t val) {
        char buf[5];
        int8_t i = 4;
        if (val == 0) { _serial.write("0", 1); return; }
        while (val > 0 && i >= 0) {
            buf[i--] = (val % 10) + '0';
            val /= 10;
        }
        _serial.write(&buf[i + 1], 4 - i);
    }

    void goto_absolute(BufferedSerial& _serial, const CursorPos& cursor) {
        _serial.write(CSI, sizeof(CSI) - 1);
        write_uint16(_serial, cursor.row + 1);
        _serial.write(";", 1);
        write_uint16(_serial, cursor.col + 1);
        _serial.write(MOVE, sizeof(MOVE) - 1);
    }

    void move_left(BufferedSerial& _serial, uint16_t count) {
        if (count <= 3) {
            for (uint16_t i = 0; i < count; ++i) _serial.write("\b", 1);
        } else {
            _serial.write(CSI, sizeof(CSI) - 1);
            write_uint16(_serial, count);
            _serial.write(CUB, sizeof(CUB) - 1);
        }
    }

    void move_right(BufferedSerial& _serial, uint16_t count) {
        if (count == 0) return;
        _serial.write(CSI, sizeof(CSI) - 1);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(CUF, sizeof(CUF) - 1);
    }

    void move_up(BufferedSerial& _serial, uint16_t count) {
        if (count == 0) return;
        _serial.write(CSI, sizeof(CSI) - 1);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(CUU, sizeof(CUU) - 1);
    }

    void move_down(BufferedSerial& _serial, uint16_t count) {
        if (count == 0) return;
        _serial.write(CSI, sizeof(CSI) - 1);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(CUD, sizeof(CUD) - 1);
    }

    void move_line_begin(BufferedSerial& _serial) {
        _serial.write("\r", 1);
    }

    void set_insert_mode(BufferedSerial& _serial, bool active) {
        _serial.write(CSI, sizeof(CSI) - 1);
        if (active) _serial.write(SM4, sizeof(SM4) - 1);
        else        _serial.write(RM4, sizeof(RM4) - 1);
    }

    void insert_rows(BufferedSerial& _serial, uint16_t count) {
        if (count == 0) return;
        _serial.write(CSI, sizeof(CSI) - 1);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(IL, sizeof(IL) - 1);
    }

    void delete_rows(BufferedSerial& _serial, uint16_t count) {
        if (count == 0) return;
        _serial.write(CSI, sizeof(CSI) - 1);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(DL, sizeof(DL) - 1);
    }

    void delete_chars(BufferedSerial& _serial, uint16_t count) {
        if (count == 0) return;
        _serial.write(CSI, sizeof(CSI) - 1);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(DCH, sizeof(DCH) - 1);
    }

    void clear_to_eol(BufferedSerial& _serial) {
        _serial.write(CSI, sizeof(CSI) - 1);
        _serial.write(EL, sizeof(EL) - 1);
    }

    void clear_to_eos(BufferedSerial& _serial) {
        _serial.write(CSI, sizeof(CSI) - 1);
        _serial.write(ED, sizeof(ED) - 1);       // ESC[J: erase from cursor to end of display
    }

    void clear_screen(BufferedSerial& _serial) {
        _serial.write(CSI, sizeof(CSI) - 1);
        _serial.write("2", 1);
        _serial.write(ED, sizeof(ED) - 1);       // ESC[2J: erase entire display
        _serial.write(CSI, sizeof(CSI) - 1);
        _serial.write(MOVE, sizeof(MOVE) - 1);     // ESC[H: cursor to home (row 1, col 1)
    }

    void set_auto_wrap(BufferedSerial& _serial, bool enable) {
        _serial.write(CSI, sizeof(CSI) - 1);
        if (enable) _serial.write(DECAWM_SET,   sizeof(DECAWM_SET)   - 1);
        else        _serial.write(DECAWM_RESET,  sizeof(DECAWM_RESET) - 1);
    }

    bool is_printable(char chr) {
        uint8_t b = (uint8_t)chr;
        return b >= 0x20 && b <= 0x7E;
    }
}}}
