#ifndef CONSOLE_ANSI_H
#define CONSOLE_ANSI_H

/**
 * @file ansi.h
 * @brief Stateless ANSI escape-code helpers, plus the CursorPos and
 *        MoveDirection types shared across the terminal layer.
 */

#include <cstdint>

#include "libbotanik/config/terminal.h"

#include "libbotanik/utils/byte_sink.h"


namespace Botanik { namespace Terminal { namespace Ansi {

    /** @name Types and constants
     *  @{ */

    using ByteSink = Utils::ByteSink;

    /** @brief Physical display coordinate. 0-indexed; the conversion to
     *  1-indexing happens when commands are sent to the remote device. */
    struct CursorPos {
        uint16_t row; uint16_t col;
    public:
        inline bool operator==(const CursorPos& other) const {
            return (row == other.row && col == other.col);
        }
    };

    static constexpr char ESC[] = "\x1B";
    static constexpr char SEP[] = "\x13";   // Minitel special-keys prefix
    static constexpr char CSI[]  = "\x1B[";  // X3.64 CSI prefix

    // Cancel: aborts a control sequence in progress, returning the parser to
    // ground state. Emitted after a tx_discard (which can strand a half-sent
    // sequence on the wire) to resync the terminal.
    static constexpr char CAN[]  = "\x18";

    static constexpr char SM4[]  = "4h"; // Set Mode 4: Insertion
    // Reset Mode 4: Replacement (Overwrite)
    static constexpr char RM4[]  = "4l";

    static constexpr char DECAWM_SET[]   = "?7h"; // Enable auto-wrap (default)
    static constexpr char DECAWM_RESET[] = "?7l"; // Disable auto-wrap

    // Show the (blinking) cursor
    static constexpr char DECTCEM_SET[]   = "?25h";
    static constexpr char DECTCEM_RESET[] = "?25l"; // Hide the cursor

    static constexpr char ATTR_NEG[]  = "7m"; // Negative image SGR
    static constexpr char ATTR_NONE[] = "m";  // Normal text SGR

    // Minitel PRO2 setup sequences (teleinformatique mode over the DIN port; a
    // VT100/VT102 ignores them). PRO2 = ESC ':' (per STUM / PyMinitel /
    // Minitel1B):
    //   - 80-col teleinformatique      = PRO2 + 0x31 0x7D
    //   - lowercase keyboard           = PRO2 + START(0x69) + MINUSCULES(0x45)
    //   - page mode (stop auto-scroll) = PRO2 + STOP(0x6A) + ROULEAU(0x43)
    // VERIFY ON HARDWARE.
    static constexpr char MINITEL_TELINFO[]   = "\x1B\x3A\x31\x7D";
    static constexpr char MINITEL_LOWERCASE[] = "\x1B\x3A\x69\x45";
    static constexpr char MINITEL_PAGE_MODE[] = "\x1B\x3A\x6A\x43";

    static constexpr char CUU[] = "A";  // Cursor Up
    static constexpr char CUD[] = "B";  // Cursor Down
    static constexpr char CUF[] = "C";  // Cursor Forward (Right)
    static constexpr char CUB[] = "D";  // Cursor Backward (Left)
    static constexpr char MOVE[] = "H"; // Cursor Move

    static constexpr char ED[]  = "J";  // Erase in Display
    static constexpr char EL[]  = "K";  // Erase in Line
    static constexpr char IL[]  = "L";  // Insert Line
    static constexpr char DL[]  = "M";  // Delete Line
    static constexpr char DCH[] = "P";  // Delete Character

    /** @}
     *  @name Generic utilities
     *  @{ */

    /** @brief Determine if a character is a printable ASCII character */
    bool is_printable(uint8_t b);

    /** @}
     *  @name State management
     *  @{ */

    // Every escape writer below is all-or-nothing: returns true only when the
    // whole sequence reached the TX buffer (see file header in ansi.cpp). A
    // false return means nothing was emitted; the caller must not advance its
    // tracking.

    /** @brief Set insertion mode (IRM) to @p active, using SM4/RM4. */
    bool set_insert_mode(ByteSink& _serial, bool active);

    /** @brief Emit CAN to abort any control sequence the terminal is
     *  mid-parsing. Use after a tx_discard, which can strand a half-sent
     *  sequence on the wire that would otherwise swallow the next emit. */
    bool cancel_sequence(ByteSink& _serial);

    /** @brief The serial terminals the console knows how to drive.
     *
     *  The console emits plain X3.64 to all of them; the type selects the
     *  one-time setup handshake and is what any terminal-specific quirk branches
     *  on (see is_minitel()). */
    enum class TerminalType : uint8_t {
        VT100      = 0,  ///< plain X3.64, no setup handshake
        VT102      = 1,  ///< VT100 plus IRM/DCH, which sideways slides need
        MINITEL_1B = 2,  ///< Minitel 1B, teleinformatique mode (PRO2 handshake)
        MINITEL_2  = 3,  ///< Minitel 2, same PRO2 handshake
    };

    /** @brief True for the Minitel family.
     *  @note Enables the PRO2 handshake below, and prevents using BS as a cursor-left shortcut */
    constexpr bool is_minitel(TerminalType t) {
        return t == TerminalType::MINITEL_1B || t == TerminalType::MINITEL_2;
    }

    /** @brief Emit @p type's one-time setup handshake, once before the first render.
     *
     *  - Minitel: the PRO2 sequences above (80-col teleinformatique + lowercase keyboard + page mode).
     *  - VT100/VT102: nothing, since DECAWM-off is re-asserted on every screen establish. */
    bool terminal_setup(ByteSink& _serial, TerminalType type);

    /** @brief Enable or disable terminal auto-wrap (DECAWM, CSI ? 7 h/l). */
    bool set_auto_wrap(ByteSink& _serial, bool enable);

    /** @brief Show or hide the cursor (DECTCEM, CSI ? 25 h/l). */
    bool set_cursor_visible(ByteSink& _serial, bool show);

    /** @}
     *  @name Cursor movement
     *  @{ */

    /** @brief Move to the given absolute position. */
    bool goto_absolute(ByteSink& _serial, const CursorPos& cursor);

    /** @brief Move the cursor left @p count times. */
    bool move_left(ByteSink& _serial, uint16_t count);

    /** @brief Move the cursor right @p count times. */
    bool move_right(ByteSink& _serial, uint16_t count);

    /** @brief Move the cursor up @p count times. */
    bool move_up(ByteSink& _serial, uint16_t count);

    /** @brief Move the cursor down @p count times. */
    bool move_down(ByteSink& _serial, uint16_t count);

    /** @brief Move to the beginning of the row. */
    bool move_row_start(ByteSink& _serial);

    /** @brief Drop to the start of the row @p count lines below (CR + line
     *  feeds).
     *  @warning The caller must ensure the feeds won't scroll the screen. */
    /** @brief @p count line feeds: down a row each, same column. Scrolls on
     *  the last row, so never emitted there. */
    bool feed_lines(ByteSink& _serial, uint16_t count);
    bool down_to_row_start(ByteSink& _serial, uint16_t count);

    /** @}
     *  @name Content edition
     *  @{ */

    /** @brief Insert rows (shifts remaining rows down).
     *  @warning At the last row, scrolls the entire screen upwards. */
    bool insert_rows(ByteSink& _serial, uint16_t count);

    /** @brief Delete rows (shifts remaining rows up). */
    bool delete_rows(ByteSink& _serial, uint16_t count);

    /** @brief Delete @p count characters at the cursor (DCH), shifting the rest
     *  of the row left and blanking the freed cells at the right edge. */
    bool delete_chars(ByteSink& _serial, uint16_t count);

    /** @brief Erase from the cursor to the end of the row (EL). The cursor
     *  does not move. */
    bool erase_to_eol(ByteSink& _serial);

    /** @brief Erase entire display and move cursor to home position */
    bool clear_screen(ByteSink& _serial);

    /** @} */

}}}

#endif // CONSOLE_ANSI_H