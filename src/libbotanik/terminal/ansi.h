#ifndef TERMINAL_ANSI_H
#define TERMINAL_ANSI_H

#include <cstdint>
#include "libbotanik/utils/serial.h"

using namespace Botanik::Utils;

namespace Botanik { 
namespace Terminal { 
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
    void write_uint16(BufferedSerial& _serial, uint16_t val);

    // Move to the given absolute position
    void goto_absolute(BufferedSerial& _serial, const CursorPos& cursor);

    // Move the cursor left, `count` times
    void move_left(BufferedSerial& _serial, uint16_t count);

    // Move the cursor left, `count` times
    void move_right(BufferedSerial& _serial, uint16_t count);

    // Move the cursor up, `count` times
    void move_up(BufferedSerial& _serial, uint16_t count);

    // Move the cursor down, `count` times
    void move_down(BufferedSerial& _serial, uint16_t count);

    // Move to the beginning of the line
    void move_line_begin(BufferedSerial& _serial);

    // Enable/disable insertion mode (shifts remaining cols right)
    void set_insert_mode(BufferedSerial& _serial, bool active);

    // Insert entire rows, shifting remaining rows down
    //  (when called at the last row, scrolls the entire screen upwards)
    void insert_rows(BufferedSerial& _serial, uint16_t count);

    // Delete rows, shifting remaining rows up
    void delete_rows(BufferedSerial& _serial, uint16_t count);

    // Delete characters, shifting remaining cols left
    void delete_chars(BufferedSerial& _serial, uint16_t count);

    // Clear characters from the cursor to the end of line
    void clear_to_eol(BufferedSerial& _serial);

}
}
}

#endif // TERMINAL_ANSI_H