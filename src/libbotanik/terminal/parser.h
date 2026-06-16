#ifndef TERMINAL_PARSER_H
#define TERMINAL_PARSER_H

/**
 * @file parser.h
 * @brief InputParser - decodes raw serial byte streams into InputEvents
 *        (printable text plus X3.64 CSI, Alt, and function-key sequences).
 */

#include <cstdint>

#include "libbotanik_config.h"
#include "libbotanik/utils/serial.h"
#include "libbotanik/terminal/ansi.h"

namespace Botanik { namespace Terminal {

    /** @name Types
     *  @{ */

    /** @brief A decoded input event: a printable-text batch, or a control command
     *  (with optional CSI parameters). */
    struct InputEvent {
        
        // Input event type
        /** @brief Which variant was decoded: none yet, printable content, or a command. */
        enum class EventType {
            NONE,       // No complete sequence yet
            CONTENT,    // Standard printable text (uses batch_data/len)
            COMMAND,    // Action/Sequence (uses command/params)
        };

        // Command key type
        /** @brief A decoded control key or sequence - named keys, Ctrl/Alt combos, function keys. */
        enum class Command : uint16_t {
            // Parser continuation
            NONE = 0,

            // Standard keys
            ENTER, BACKSPACE, DELETE, TAB, ESC,
            UP, DOWN, LEFT, RIGHT,
            HOME, END, PAGE_UP, PAGE_DOWN,
            CTRL_PAGE_UP, CTRL_PAGE_DOWN,

            // Physical Control Combinations
            CTRL_A, CTRL_B, CTRL_C, CTRL_D, CTRL_E, CTRL_F, CTRL_G,
            CTRL_K, CTRL_L, CTRL_N, CTRL_P, CTRL_R, CTRL_U, CTRL_W, CTRL_Y,
            CTRL_SPACE, 

            // Shift Combinations
            SHIFT_ENTER, SHIFT_TAB,

            // Meta/Alt Combinations
            ALT_B, ALT_F,

            // Alt+Number
            ALT_1, ALT_2, ALT_3, ALT_4, ALT_5,
            ALT_6, ALT_7, ALT_8, ALT_9, ALT_0,
        };
        // Event fields
        EventType   type = EventType::NONE;
        Command     command = Command::NONE;
        // For COMMAND events that have been chunked (consecutive identical
        // BACKSPACE / CTRL_D presses while output backpressure held the
        // emit window). Default 1 = run the command once.
        uint16_t    count = 1;
        // For CONTENT batches
        const char* batch_data = nullptr;
        uint16_t    batch_len = 0;
        // For CSI COMMAND
        uint16_t params[8];
        uint8_t  param_count = 0;
        char     prefix = 0;     // e.g., '?' in ESC [ ? 25 l
        // For MODE and CSI COMMANDS
        uint8_t  value = 0;
    };

    /** @} */

    /** @class InputParser
     *  @brief Decodes a raw serial byte stream into InputEvents (X3.64 CSI, Alt,
     *         function-key, and Minitel SEP sequences). */
    class InputParser {
        using BufferedSerial = Utils::BufferedSerial;

        /** @name Types
         *  @{ */

        // Parser state machine
        enum class State {
            IDLE,               // Idle state
            TEXT,               // ASCII characters
            IN_ESC,             // Decision between CSI or Alt+X
            IN_CSI,             // Standard X3.64 CSI sequences
            IN_SEP              // Minitel 0x13 sequences
        };

        /** @} */

        /** @name State
         *  @{ */

        /** @brief Serial device buffered wrapper. */
        BufferedSerial& _serial;
        /** @brief Shared wall-clock timestamp (ms) updated by the owner. */
        uint32_t*       _timestamp_ms;

        /** @brief Event currently being constructed. */
        InputEvent      _event;
        /** @brief Current state-machine state. */
        State           _state = State::IDLE;

        /** @brief Accumulated printable text awaiting a flush. */
        char            _text_buf[Config::ANSI_INPUT_BATCH_SIZE];
        /** @brief Bytes held in _text_buf. */
        uint16_t        _text_len = 0;
        /** @brief Timestamp of the last byte read. */
        uint32_t        _last_byte_ms = 0;
        /** @brief When the current text batch started accumulating. */
        uint32_t        _batch_start_ms = 0;

        /** @brief Pending chunkable command (BACKSPACE / CTRL_D held down).
         *  Coalesced into one COMMAND event with `count` so a held key
         *  becomes one reconcile pass instead of N. */
        InputEvent::Command _pending_cmd = InputEvent::Command::NONE;
        /** @brief Number of coalesced presses staged in _pending_cmd. */
        uint16_t            _pending_cmd_count = 0;
        /** @brief When the pending command first started coalescing. */
        uint32_t            _pending_cmd_start_ms = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Construct a parser bound to a serial wrapper and timestamp. */
        InputParser(BufferedSerial& serial, uint32_t* timestamp_ms);

        /** @} */

        /** @name Parsing
         *  @{ */

        /** @brief Read available bytes and return the next decoded event. */
        InputEvent& parse();

        /** @} */

    private:
        /** @name Initialization
         *  @{ */

        /** @brief Clear _event back to a NONE event. */
        void _reset_event();

        /** @} */

        /** @name Text handling helpers
         *  @{ */

        /** @brief Emit the accumulated text batch as a CONTENT event. */
        void _flush_text();

        /** @brief Append one byte to the text batch. */
        void _store_text(uint8_t b);

        /** @brief Store one byte, flushing first if the batch is full. */
        State _append_text(uint8_t b);

        /** @} */

        /** @name Flush policy
         *  @{ */

        /** @brief True while output backpressure should hold the emit window
         *         open for a batch/command started at @p start_ms. */
        bool _backpressure_holds(uint32_t start_ms) const;

        /** @brief True when the pending text batch should be flushed now. */
        bool _should_flush_text() const;

        /** @brief True when the pending chunked command should be emitted now. */
        bool _should_flush_pending_command() const;

        /** @} */

        /** @name Chunkable command helpers
         *  @{ */

        /** @brief True if byte @p b triggers a coalescable command. */
        static bool _is_chunkable_trigger(uint8_t b);

        /** @brief Map a chunkable trigger byte to its command. */
        static InputEvent::Command _chunkable_command_for(uint8_t b);

        /** @brief Stage (or extend) the pending coalesced command. */
        void _stage_pending_command(InputEvent::Command cmd);

        /** @brief Emit the staged command as a single COMMAND event. */
        void _emit_pending_command();

        /** @} */

        /** @name State handlers
         *  @{ */

        /** @brief Handle one byte in the IDLE state. */
        State _handle_idle(uint8_t b);

        /** @brief Handle one byte in the TEXT state. */
        State _handle_text(uint8_t b);

        /** @brief Handle one byte in the IN_ESC state. */
        State _handle_esc(uint8_t b);

        /** @brief Handle one byte in the IN_CSI state. */
        State _handle_csi(uint8_t b);

        /** @brief Handle one byte in the IN_SEP state. */
        State _handle_sep(uint8_t b);

        /** @} */

        /** @name Mapping logic
         *  @{ */

        /** @brief Map a C0 control byte to a command. */
        static InputEvent::Command _map_control_char(const uint8_t b);

        /** @brief Map an ESC-prefixed (Alt+X) byte to a command. */
        static InputEvent::Command _map_esc_extension(const uint8_t b);

        /** @brief Map a CSI final byte (with first param + modifier param) to a command. */
        static InputEvent::Command _map_csi_to_command(const uint8_t b, const uint16_t param1,
                                                       const uint16_t modifier);

        /** @brief Map a Minitel 0x13 separator byte to a command. */
        static InputEvent::Command _map_sep_to_command(const uint8_t b);

        /** @} */
    };

}}

#endif // TERMINAL_PARSER_H
