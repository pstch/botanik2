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

    /** @brief Represents a physical coordinate on the display.
     *  @note  this struct's members are still 0-indexed, the conversion
     *  to 1-indexing happens when sending commands to the remote device. */
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

    static constexpr char DECTCEM_SET[]   = "?25h"; // Show the (blinking) cursor
    static constexpr char DECTCEM_RESET[] = "?25l"; // Hide the cursor

    static constexpr char ATTR_NEG[]  = "7m"; // Negative image SGR
    static constexpr char ATTR_NONE[] = "m";  // Normal text SGR

    // Minitel PRO2 setup sequences (téléinformatique mode over the DIN serial
    // port). A VT100/VT102 ignores them. PRO2 = ESC ':' (per STUM / PyMinitel /
    // the eserandour Minitel1B library):
    //   - 80-col téléinformatique mode       = PRO2 + 0x31 0x7D
    //   - lowercase keyboard (not caps-locked) = PRO2 + START(0x69) + MINUSCULES(0x45)
    //   - page mode (stop "rouleau" auto-scroll) = PRO2 + STOP(0x6A) + ROULEAU(0x43)
    // VERIFY ON HARDWARE. Note: téléinformatique mode is documented to default to
    // ASCII ("american") keycodes — the French set is the opt-in variant — and no
    // plain programmable byte for the keyboard charset surfaced in those sources.
    static constexpr char MINITEL_TELINFO[]   = "\x1B\x3A\x31\x7D";
    static constexpr char MINITEL_LOWERCASE[] = "\x1B\x3A\x69\x45";
    static constexpr char MINITEL_PAGE_MODE[] = "\x1B\x3A\x6A\x43";

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

    /** @}
     *  @name State management
     *  @{ */

    // FIXME: we should probably have a handler for the "inverted" mode here ?

    // Every escape writer below is all-or-nothing and returns true only when the
    // whole sequence reached the TX buffer (see file header in ansi.cpp). A false
    // return means nothing was emitted; the caller must not advance its tracking.

    /** @brief Set insertion mode (IRM) to @p active , using SM4/RM4.
     *  @note When the TX buffer can't hold the command, emit nothing and return false. */
    bool set_insert_mode(BufferedSerial& _serial, bool active);

    /** @brief Emit the one-time Minitel PRO2 handshake (80-col téléinformatique +
     *  lowercase keyboard + page mode). These are no-op sequences on a VT100/VT102.
     *  All-or-nothing: emits nothing and returns false if the TX buffer can't hold it. */
    bool minitel_setup(BufferedSerial& _serial);

    /** @brief One-time setup for a VT100/VT102 terminal. Currently a no-op (the
     *  persistent DECAWM-off state is re-asserted on every screen establish), kept
     *  as the symmetric counterpart to minitel_setup() and the home for any future
     *  one-time VT102 setup (e.g. LNM reset, DECSTBM scrolling region). */
    bool vt102_setup(BufferedSerial& _serial);

    /** @brief Enable or disable terminal auto-wrap (DECAWM, CSI ? 7 h/l). */
    bool set_auto_wrap(BufferedSerial& _serial, bool enable);

    /** @brief Show or hide the cursor (DECTCEM, CSI ? 25 h/l). The cursor, when
     *  shown, blinks — there is no steady-cursor mode, so "stop blinking" means
     *  hiding it. A Minitel driven in téléinformatique mode is expected to honour
     *  DECTCEM like the rest of the VT100 subset we already emit; VERIFY ON
     *  HARDWARE. All-or-nothing: emits nothing and returns false if the TX buffer
     *  can't hold it. */
    bool set_cursor_visible(BufferedSerial& _serial, bool show);

    /** @}
     *  @name Cursor movement
     *  @{ */

    /** @brief Move to the given absolute position. */
    bool goto_absolute(BufferedSerial& _serial, const CursorPos& cursor);

    /** @brief Move the cursor left @p count times. */
    bool move_left(BufferedSerial& _serial, uint16_t count);

    /** @brief Move the cursor right @p count times. */
    bool move_right(BufferedSerial& _serial, uint16_t count);

    /** @brief Move the cursor up @p count times. */
    bool move_up(BufferedSerial& _serial, uint16_t count);

    /** @brief Move the cursor down @p count times. */
    bool move_down(BufferedSerial& _serial, uint16_t count);

    /** @brief Move to the beginning of the row. */
    bool move_row_start(BufferedSerial& _serial);

    /** @brief Drop to the start of the row @p count lines below (CR + line feeds).
     *  @warning The caller must ensure the feeds won't scroll the screen. */
    bool down_to_row_start(BufferedSerial& _serial, uint16_t count);

    /** @}
     *  @name Content edition
     *  @{ */

    /** @brief Insert entire rows
     *  @note Shifts remaining rows down
     *  @warning When called at the last row, scrolls the entire screen upwards */
    bool insert_rows(BufferedSerial& _serial, uint16_t count);

    /** @brief Delete rows
     *  @note Shifts remaining rows up */
    bool delete_rows(BufferedSerial& _serial, uint16_t count);

    /** @brief Delete characters
     *  @note Shifts remaining columns left */
    bool delete_chars(BufferedSerial& _serial, uint16_t count);

    /** @}
     *  @name Content clearing
     *  @{ */

    /** @brief Clear characters from the cursor to the end of line
     *  @note Columns before the cursor are untouched. */
    bool clear_to_eol(BufferedSerial& _serial);

    /** @brief Erase from the cursor to the end of the display
     *  @note Rows above the cursor are untouched. */
    bool clear_to_eos(BufferedSerial& _serial);

    /** @brief Erase entire display and move cursor to home position */
    bool clear_screen(BufferedSerial& _serial);

    /** @} */

}}}

#endif // TERMINAL_ANSI_H