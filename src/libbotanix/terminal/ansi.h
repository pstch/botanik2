#include <cstdint>
#include "serial.cpp"

namespace Ansi {
    // ---- X3.64 CSI prefix
    static const char* CSI  = "\x1B[";     // CSI

    // --- Mode Settings ---
    static const char* SM4  = "4h"; // Set Mode 4: Insertion
    static const char* RM4  = "4l"; // Reset Mode 4: Replacement (Overwrite)

    static const char* ATTR_NEG  = "7m"; // Negative image SGR
    static const char* ATTR_NONE  = "m"; // Normal text SGR

    // --- Movement ---
    static const char* CUU  = "A";  // Cursor Up
    static const char* CUD  = "B";  // Cursor Down
    static const char* CUF  = "C";  // Cursor Forward (Right)
    static const char* CUB  = "D";  // Cursor Backward (Left)
    static const char* MOVE  = "H";  // Cursor Move

    // --- Content Modification ---
    static const char* ED   = "J";  // Erase in Display
    static const char* IL   = "L";  // Insert Line
    static const char* DL   = "M";  // Delete Line
    static const char* DCH  = "P";  // Delete Character
    static const char* EL   = "K";  // Erase in Line (Cursor to EOL)


    /** Represents a physical coordinate on the display.
     * 
     * Note that this struct's members are still 0-indexed, the conversion
     * to 1-indexing happens when sending commands to the remote device.
     */
    struct CursorPos {
        uint16_t row; uint16_t col;

        bool operator==(const CursorPos& other) const {
            return (row == other.row && col == other.col);
        }
    };

    // Write a 16-bit integer as its decimal representation
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

    // Move to the given absolute position
    void goto_absolute(BufferedSerial& _serial, const CursorPos& cursor) {
        _serial.write(CSI, 2);
        write_uint16(_serial, cursor.row + 1);
        _serial.write(";", 1);
        write_uint16(_serial, cursor.col + 1);
        _serial.write(MOVE, 1);
    }

    // Move the cursor left, `count` times
    void move_left(BufferedSerial& _serial, uint16_t count) {
        if (count <= 3) {
            for (uint16_t i = 0; i < count; ++i) _serial.write("\b", 1);
        } else {
            _serial.write(CSI, 2);
            write_uint16(_serial, count);
            _serial.write(CUB, 1);
        }
    }

    // Move the cursor left, `count` times
    void move_right(BufferedSerial& _serial, uint16_t count) {
        _serial.write(CSI, 2);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(CUF, 1);
    }

    // Move the cursor up, `count` times
    void move_up(BufferedSerial& _serial, uint16_t count) {
        _serial.write(CSI, 2);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(CUU, 1);
    }

    // Move the cursor down, `count` times
    void move_down(BufferedSerial& _serial, uint16_t count) {
        _serial.write(CSI, 2);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(CUD, 1);
    }

    // Move to the beginning of the line
    void move_line_begin(BufferedSerial& _serial) {
        _serial.write("\r", 1);
    }

    // Enable/disable insertion mode (shifts remaining cols right)
    void set_insert_mode(BufferedSerial& _serial, bool active) {
        _serial.write(CSI, 2);
        if (active) _serial.write(SM4, 2);
        else        _serial.write(RM4, 2);
    }

    // Insert entire rows, shifting remaining rows down
    //  (when called at the last row, scrolls the entire screen upwards)
    void insert_rows(BufferedSerial& _serial, uint16_t count) {
        _serial.write(CSI, 2);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(IL, 1);
    }

    // Delete rows, shifting remaining rows up
    void delete_rows(BufferedSerial& _serial, uint16_t count) {
        _serial.write(CSI, 2);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(DL, 1);
    }

    // Delete characters, shifting remaining cols left
    void delete_chars(BufferedSerial& _serial, uint16_t count) {
        _serial.write(CSI, 2);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(DCH, 1);
    }

    // Clear characters from the cursor to the end of line
    void clear_to_eol(BufferedSerial& _serial) {
        _serial.write(CSI, 2);
        _serial.write(EL, 1);
    }
}
