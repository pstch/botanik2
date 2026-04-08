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
        _serial.write(CSI, 2);
        write_uint16(_serial, cursor.row + 1);
        _serial.write(";", 1);
        write_uint16(_serial, cursor.col + 1);
        _serial.write(MOVE, 1);
    }

    void move_left(BufferedSerial& _serial, uint16_t count) {
        if (count <= 3) {
            for (uint16_t i = 0; i < count; ++i) _serial.write("\b", 1);
        } else {
            _serial.write(CSI, 2);
            write_uint16(_serial, count);
            _serial.write(CUB, 1);
        }
    }

    void move_right(BufferedSerial& _serial, uint16_t count) {
        _serial.write(CSI, 2);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(CUF, 1);
    }

    void move_up(BufferedSerial& _serial, uint16_t count) {
        _serial.write(CSI, 2);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(CUU, 1);
    }

    void move_down(BufferedSerial& _serial, uint16_t count) {
        _serial.write(CSI, 2);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(CUD, 1);
    }

    void move_line_begin(BufferedSerial& _serial) {
        _serial.write("\r", 1);
    }

    void set_insert_mode(BufferedSerial& _serial, bool active) {
        _serial.write(CSI, 2);
        if (active) _serial.write(SM4, 2);
        else        _serial.write(RM4, 2);
    }

    void insert_rows(BufferedSerial& _serial, uint16_t count) {
        _serial.write(CSI, 2);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(IL, 1);
    }

    void delete_rows(BufferedSerial& _serial, uint16_t count) {
        _serial.write(CSI, 2);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(DL, 1);
    }

    void delete_chars(BufferedSerial& _serial, uint16_t count) {
        _serial.write(CSI, 2);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(DCH, 1);
    }

    void clear_to_eol(BufferedSerial& _serial) {
        _serial.write(CSI, 2);
        _serial.write(EL, 1);
    }

    bool is_printable(char chr) { 
        uint8_t b = (uint8_t)chr;
        return b >= 0x20 && b <= 0x7E;
    }
}}}
