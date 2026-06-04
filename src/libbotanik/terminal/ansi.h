#ifndef TERMINAL_ANSI_H
#define TERMINAL_ANSI_H

#include <cstdint>
#include "libbotanik/utils/serial.h"


namespace Botanik { namespace Terminal { namespace Ansi {

    using BufferedSerial = Utils::BufferedSerial;


    /** Represents a physical coordinate on the display.
     *
     * Note that this struct's members are still 0-indexed, the conversion
     * to 1-indexing happens when sending commands to the remote device.
     */
    struct CursorPos {
        uint16_t row; uint16_t col;
    public:
        inline bool operator==(const CursorPos& other) const {
            return (row == other.row && col == other.col);
        }
    };

    enum MoveDirection {
        LEFT, RIGHT, UP, DOWN
    };

    // ESC
    static constexpr char ESC[] = "\x1B";

    // Minitel special keys prefix
    static constexpr char SEP[] = "\x13";

    // X3.64 CSI prefix
    static constexpr char CSI[]  = "\x1B[";

    // Mode Settings
    static constexpr char SM4[]  = "4h"; // Set Mode 4: Insertion
    static constexpr char RM4[]  = "4l"; // Reset Mode 4: Replacement (Overwrite)

    // DEC private mode sequences (CSI ? ...)
    static constexpr char DECAWM_SET[]   = "?7h"; // Enable auto-wrap (default)
    static constexpr char DECAWM_RESET[] = "?7l"; // Disable auto-wrap

    static constexpr char ATTR_NEG[]  = "7m"; // Negative image SGR
    static constexpr char ATTR_NONE[] = "m";  // Normal text SGR

    // Movement
    static constexpr char CUU[] = "A";  // Cursor Up
    static constexpr char CUD[] = "B";  // Cursor Down
    static constexpr char CUF[] = "C";  // Cursor Forward (Right)
    static constexpr char CUB[] = "D";  // Cursor Backward (Left)
    static constexpr char MOVE[] = "H"; // Cursor Move

    // Content modification
    static constexpr char ED[]  = "J";  // Erase in Display
    static constexpr char IL[]  = "L";  // Insert Line
    static constexpr char DL[]  = "M";  // Delete Line
    static constexpr char DCH[] = "P";  // Delete Character
    static constexpr char EL[]  = "K";  // Erase in Line (Cursor to EOL)

    // Write a 16-bit integer as its decimal representation
    void write_uint16(BufferedSerial& _serial, uint16_t val);

    // Move to the given absolute position
    void goto_absolute(BufferedSerial& _serial, const CursorPos& cursor);

    // Move the cursor left, `count` times
    void move_left(BufferedSerial& _serial, uint16_t count);

    // Move the cursor right, `count` times
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

    // Erase from the cursor to the end of the display (rows below stay untouched above)
    void clear_to_eos(BufferedSerial& _serial);

    // Erase entire display and move cursor to home position
    void clear_screen(BufferedSerial& _serial);

    // Enable or disable terminal auto-wrap (DECAWM)
    void set_auto_wrap(BufferedSerial& _serial, bool enable);

    // Determine if a character is a printable ASCII character
    bool is_printable(char b);
}}}

#endif // TERMINAL_ANSI_H