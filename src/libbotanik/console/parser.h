#ifndef CONSOLE_PARSER_H
#define CONSOLE_PARSER_H

/**
 * @file parser.h
 * @brief InputParser - decodes raw serial byte streams into InputEvents
 *        (printable text plus X3.64 CSI, Alt, and function-key sequences).
 */

#include <cstdint>

#include "libbotanik/config/terminal.h"

#include "libbotanik/console/ansi.h"
#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Terminal {

    /** @name Types
     *  @{ */

    /** @brief A decoded input event: a printable-text batch, or a control
     *  command (with optional CSI parameters). */
    struct InputEvent {

        /** @brief Which variant was decoded. */
        enum class EventType {
            NONE,       // No complete sequence yet
            CONTENT,    // Printable text (uses batch_data/len)
            COMMAND,    // Action/sequence (uses command/params)
        };

        /** @brief A decoded control key or sequence: named keys, Ctrl/Alt
         *  combos, function keys. */
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
            CTRL_K, CTRL_L, CTRL_N, CTRL_O, CTRL_P, CTRL_R, CTRL_S, CTRL_U,
            CTRL_V, CTRL_W, CTRL_Y,
            CTRL_SPACE,

            // Shift Combinations
            SHIFT_ENTER, SHIFT_TAB,
            SHIFT_UP, SHIFT_DOWN,

            // Meta/Alt Combinations
            ALT_B, ALT_F, ALT_W,

            // Alt+Number
            ALT_1, ALT_2, ALT_3, ALT_4, ALT_5,
            ALT_6, ALT_7, ALT_8, ALT_9, ALT_0,
        };

        // check that ALT_1 and ALT_9 stay contiguous with ALT_0 last
        static_assert((int)Command::ALT_9 - (int)Command::ALT_1 == 8 &&
                      (int)Command::ALT_0 == (int)Command::ALT_9 + 1,
                      "ALT_1..ALT_9 must stay contiguous with ALT_0 last");

        // main event fields
        EventType   type        = EventType::NONE;
        Command     command     = Command::NONE;

        // repeat count for chunked COMMAND events
        uint16_t    count       = 1;
        const char* batch_data  = nullptr;  // CONTENT batch
        uint16_t    batch_len   = 0;        // CONTENT batch length
    };

    /** @} */

    /** @class InputParser
     *  @brief Decodes a raw serial byte stream into InputEvents (X3.64 CSI,
     *         Alt, function-key, and Minitel SEP sequences). */
    class InputParser {
        using BufferedSerial = Utils::BufferedSerial;
        using Command = InputEvent::Command;

        /** @name Types
         *  @{ */

        enum class State {
            IDLE,               // Nothing to do
            TEXT,               // ASCII characters
            IN_ESC,             // Decision between CSI or Alt+X
            IN_CSI,             // Standard X3.64 CSI sequences
            IN_SEP              // Minitel 0x13 sequences
        };

        /** @} */

        /** @name State
         *  @{ */

        BufferedSerial& _serial;                ///< borrowed serial device
        const uint64_t* _timestamp_ms;          ///< shared loop timestamp, published by the scheduler

        InputEvent      _event;                 ///< event being constructed

        /** @brief The CSI accumulator: the PARSER's, not the event's.
         *
         *  A sequence arrives across as many calls as the ring splits it into
         *  - parse() returns the moment the ring empties - and _event is
         *  cleared at the top of every one. Cleared on entering IN_CSI. */
        static const uint8_t MAX_CSI_PARAMS = 8;
        uint16_t        _csi_params[MAX_CSI_PARAMS] = {};
        uint8_t         _csi_count = 0;
        State           _state = State::IDLE;   ///< parser state
        uint64_t        _last_byte_ms = 0;      ///< timestamp of last read byte
        uint64_t        _batch_start_ms = 0;    ///< timestamp of current batch start

        char            _text_buf[Config::ANSI_INPUT_BATCH_SIZE];   ///< text awaiting flush
        uint16_t        _text_len = 0;                              ///< length of text above

        Command         _pending_cmd = Command::NONE;               ///< pending chunkable command
        uint16_t        _pending_cmd_count = 0;                     ///< pending chunk length
        uint64_t        _pending_cmd_start_ms = 0;                  ///< pending chunk start timestamp

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Construct a parser bound to a serial wrapper and timestamp. */
        InputParser(BufferedSerial& serial, const uint64_t* timestamp_ms);

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

        /** @brief Drop the byte parse() peeked from the ring and stamp its
         *  arrival. Every consuming path in the drain goes through this. */
        void _take();

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
        bool _backpressure_holds(uint64_t start_ms) const;

        /** @brief True when the pending text batch should be flushed now. */
        bool _should_flush_text() const;

        /** @brief True when the pending chunked command should be emitted
         *  now. */
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

        /** @brief Set the event command (COMMAND type if non-NONE), back to IDLE.
         *  Shared tail of the idle/sep/esc handlers. */
        State _set_command(InputEvent::Command c);

        /** @brief Handle one byte in the IDLE state. */
        State _handle_idle(uint8_t b);

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

#endif // CONSOLE_PARSER_H
