#ifndef TERMINAL_PARSER_H
#define TERMINAL_PARSER_H

#include <cstdint>

#include "libbotanik_config.h"
#include "libbotanik/utils/serial.h"
#include "libbotanik/terminal/ansi.h"

namespace Botanik { namespace Terminal {

    struct InputEvent {
        // Input event type
        enum class EventType {
            NONE,       // No complete sequence yet
            CONTENT,    // Standard printable text (uses batch_data/len)
            COMMAND,    // Action/Sequence (uses command/params)
        };
        // Command key type
        enum class Command : uint16_t {
            // Parser continuation
            NONE = 0,

            // Standard keys
            ENTER, BACKSPACE, DELETE, TAB, ESC,
            UP, DOWN, LEFT, RIGHT,
            HOME, END, PAGE_UP, PAGE_DOWN,

            // Physical Control Combinations
            CTRL_A, CTRL_B, CTRL_C, CTRL_D, CTRL_E, CTRL_F, CTRL_G,
            CTRL_K, CTRL_L, CTRL_N, CTRL_P, CTRL_R, CTRL_U, CTRL_W, CTRL_Y,
            CTRL_SPACE,

            // Shift Combinations
            SHIFT_ENTER,

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

    class InputParser {
    private:
        using BufferedSerial = Utils::BufferedSerial;
        // State enumeration
        enum class State {
            IDLE,               // Idle state
            TEXT,               // ASCII characters
            IN_ESC,             // Decision between CSI or Alt+X
            IN_CSI,             // Standard X3.64 CSI sequences
            IN_SEP              // Minitel 0x13 sequences
        };
        // Serial device buffered wrapper
        BufferedSerial& _serial;
        uint32_t*       _timestamp_ms;
        // Local state
        InputEvent      _event;                                     // current event being constructed
        State           _state = State::IDLE;                       // current state
        // Text Batching
        char            _text_buf[Config::ANSI_INPUT_BATCH_SIZE];   // batched text
        uint16_t        _text_len = 0;                              // batched text length
        uint32_t        _last_byte_ms = 0;                          // timestamp of last byte read
        uint32_t        _batch_start_ms = 0;                        // when the current text batch started accumulating
        // Pending chunkable command (BACKSPACE / CTRL_D held down).
        // Coalesced into one COMMAND event with `count` so a held key
        // becomes one reconcile pass instead of N.
        InputEvent::Command _pending_cmd = InputEvent::Command::NONE;
        uint16_t            _pending_cmd_count = 0;
        uint32_t            _pending_cmd_start_ms = 0;
        // Initialization
        void _reset_event();
        // Text handling helpers
        void _flush_text();
        void _store_text(uint8_t b);
        // Flush policy — shared by the text batch and the chunked command
        bool _backpressure_holds(uint32_t start_ms) const;
        bool _should_flush_text() const;
        bool _should_flush_pending_command() const;
        // Chunkable command helpers
        static bool _is_chunkable_trigger(uint8_t b);
        static InputEvent::Command _chunkable_command_for(uint8_t b);
        void _stage_pending_command(InputEvent::Command cmd);
        void _emit_pending_command();
        // State handlers
        State _handle_idle(uint8_t b);
        State _handle_text(uint8_t b);
        State _handle_esc(uint8_t b);
        State _handle_csi(uint8_t b);
        State _handle_sep(uint8_t b);
        // Mapping Logic
        static InputEvent::Command _map_control_char(const uint8_t b);
        static InputEvent::Command _map_esc_extension(const uint8_t b);
        static InputEvent::Command _map_csi_to_command(const uint8_t b, const uint16_t param1);
        static InputEvent::Command _map_sep_to_command(const uint8_t b);
    public:
        // Constructor
        InputParser(BufferedSerial& serial, uint32_t* timestamp_ms);
        // Main entry point
        InputEvent& parse();
    };

}}

#endif // TERMINAL_PARSER_H