#ifndef TERMINAL_ANSI_H
#define TERMINAL_ANSI_H

/**
 * @file ansi.h
 * @brief Stateless ANSI escape-code helpers, plus the CursorPos and
 *        MoveDirection types shared across the terminal layer.
 */

#include <cstdint>
#include "libbotanik/utils/serial.h"


namespace Botanik { namespace Terminal { namespace Ansi {

    /** @name Types and constants
     *  @{ */

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

    /** @} 
     *  @name Generic utilities
     *  @{ */

    /** @brief Determine if a character is a printable ASCII character */
    bool is_printable(uint8_t b);

    /** @brief Write a 16-bit integer as its decimal representation */
    void write_uint16(BufferedSerial& _serial, uint16_t val);

    /** @}
     *  @name State management
     *  @{ */

    // FIXME: we should probably have a handler for the "inverted" mode here ?

    /** @brief Set insertion mode (IRM) to @p active , using SM4/RM4.
     *  @note When the TX buffer can't hold the command, emit nothing and return false. */
    bool set_insert_mode(BufferedSerial& _serial, bool active);

    /** @brief Enable or disable terminal auto-wrap (DECAWM, CSI ? 7 h/l). */
    void set_auto_wrap(BufferedSerial& _serial, bool enable);

    /** @}
     *  @name Cursor movement
     *  @{ */

    /** @brief Move to the given absolute position. */
    void goto_absolute(BufferedSerial& _serial, const CursorPos& cursor);

    /** @brief Move the cursor left @p count times. */
    void move_left(BufferedSerial& _serial, uint16_t count);

    /** @brief Move the cursor right @p count times. */
    void move_right(BufferedSerial& _serial, uint16_t count);

    /** @brief Move the cursor up @p count times. */
    void move_up(BufferedSerial& _serial, uint16_t count);

    /** @brief Move the cursor down @p count times. */
    void move_down(BufferedSerial& _serial, uint16_t count);

    /** @brief Move to the beginning of the row. */
    void move_row_start(BufferedSerial& _serial);

    /** @}
     *  @name Content edition
     *  @{ */

    /** @brief Insert entire rows
     *  @note Shifts remaining rows down
     *  @warning When called at the last row, scrolls the entire screen upwards */
    void insert_rows(BufferedSerial& _serial, uint16_t count);

    /** @brief Delete rows
     *  @note Shifts remaining rows up */
    void delete_rows(BufferedSerial& _serial, uint16_t count);

    /** @brief Delete characters
     *  @note Shifts remaining columns left */
    void delete_chars(BufferedSerial& _serial, uint16_t count);

    /** @}
     *  @name Content clearing
     *  @{ */

    /** @brief Clear characters from the cursor to the end of line 
     *  @note Columns before the cursor are untouched. */
    void clear_to_eol(BufferedSerial& _serial);

    /** @brief Erase from the cursor to the end of the display 
     *  @note Rows above the cursor are untouched. */
    void clear_to_eos(BufferedSerial& _serial);

    /** @brief Erase entire display and move cursor to home position */
    void clear_screen(BufferedSerial& _serial);

    /** @} */

}}}

#endif // TERMINAL_ANSI_H