/**
 * @file parser.cpp
 * @brief InputParser implementation - the byte-stream decode state machine that
 *        turns raw input into InputEvents.
 */
#include <cstdint>
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE

#include "libbotanik/console/parser.h"


namespace Botanik { namespace Terminal {

    namespace {
        using Command = InputEvent::Command;
        using EventType = InputEvent::EventType;
    }

    // Initialization

    void InputParser::_reset_event() {
        _event = InputEvent();
    }

    void InputParser::_take() {
        uint8_t drop;
        _serial.read((char*)&drop, 1);
        _last_byte_ms = *_timestamp_ms;
    }

    // Chunkable command helpers

    bool InputParser::_is_chunkable_trigger(uint8_t b) {
        return b == 0x04   // CTRL_D
            || b == 0x08   // ^H backspace
            || b == 0x7F;  // DEL backspace
    }

    InputEvent::Command InputParser::_chunkable_command_for(uint8_t b) {
        if (b == 0x04) return Command::CTRL_D;
        return Command::BACKSPACE;
    }

    void InputParser::_stage_pending_command(InputEvent::Command cmd) {
        _pending_cmd          = cmd;
        _pending_cmd_count    = 1;
        _pending_cmd_start_ms = *_timestamp_ms;
    }

    void InputParser::_emit_pending_command() {
        _event.type        = EventType::COMMAND;
        _event.command     = _pending_cmd;
        _event.count       = _pending_cmd_count;
        _pending_cmd       = Command::NONE;
        _pending_cmd_count = 0;
    }


    // Text handling helpers

    void InputParser::_store_text(uint8_t b) {
        if (_text_len == 0) _batch_start_ms = *_timestamp_ms;
        _text_buf[_text_len++] = (char)b;
    }

    void InputParser::_flush_text() {
        _event.type = EventType::CONTENT;
        _event.batch_data = _text_buf;
        _event.batch_len = _text_len;
        _text_len = 0;
    }

    InputParser::State InputParser::_append_text(uint8_t b) {
        _store_text(b);
        if (_text_len == Config::ANSI_INPUT_BATCH_SIZE) {
            _flush_text();
            return State::IDLE;
        }
        return State::TEXT;
    }

    // Flush policy

    // TODO: investigate whether we could use the same latency for input batches and coalesced commands

    bool InputParser::_backpressure_holds(uint64_t start_ms) const {
        // Hold a pending batch while the TX ring drains, so per-byte flushes
        // collapse into one reconcile. The timeout (from batch start, not last
        // byte) breaks the deadlock if TX never drains.
        if (_serial.tx_empty()) return false;
        return (*_timestamp_ms - start_ms) < Config::ANSI_INPUT_BATCH_TIMEOUT_MS;
    }

    bool InputParser::_should_flush_text() const {
        if (_state != State::TEXT || _text_len == 0) return false;
        return !_backpressure_holds(_batch_start_ms);
    }

    bool InputParser::_should_flush_pending_command() const {
        if (_pending_cmd == Command::NONE) return false;
        // A held command (coalesced backspace) wants lower latency than a paste
        // batch, so it uses a shorter flush timeout than
        // _backpressure_holds()'s text-batch one.
        if (_serial.tx_empty()) return true;
        return (*_timestamp_ms - _pending_cmd_start_ms) >= Config::ANSI_CMD_FLUSH_TIMEOUT_MS;
    }

    // State handlers

    InputParser::State InputParser::_set_command(Command c) {
        _event.command = c;
        if (c != Command::NONE) _event.type = EventType::COMMAND;
        return State::IDLE;
    }
    InputParser::State InputParser::_handle_idle(uint8_t b) {
        if (b == (uint8_t)Ansi::ESC[0])  return State::IN_ESC;
        if (b == (uint8_t)Ansi::SEP[0])  return State::IN_SEP;
        if (Ansi::is_printable(b))       return _append_text(b);
        return _set_command(_map_control_char(b));
    }

    InputParser::State InputParser::_handle_sep(uint8_t b) {
        return _set_command(_map_sep_to_command(b));
    }

    InputParser::State InputParser::_handle_esc(uint8_t b) {
        if (b == '[') {
            // A fresh sequence: the accumulator belongs to THIS one.
            for (uint8_t i = 0; i < MAX_CSI_PARAMS; i++) _csi_params[i] = 0;
            _csi_count = 0;
            return State::IN_CSI;
        }
        return _set_command(_map_esc_extension(b));
    }

    InputParser::State InputParser::_handle_csi(uint8_t b) {
        // Read parameter and stay in CSI mode
        if (b >= '0' && b <= '9') {
            const uint8_t idx = _csi_count;
            if (idx < MAX_CSI_PARAMS) {
                uint32_t v = (uint32_t)_csi_params[idx] * 10 + (uint32_t)(b - '0');
                _csi_params[idx] = (v > 0xFFFF) ? (uint16_t)0xFFFF : (uint16_t)v;
            }
            return State::IN_CSI;
        }

        // Read separator and stay in CSI mode
        if (b == ';' || b == ':') {
            if (_csi_count + 1 < MAX_CSI_PARAMS) _csi_count++;
            return State::IN_CSI;
        }

        // Consume private-mode prefixes and stay in CSI mode
        if (b == '?' || b == '>' || b == '<' || b == '=') {
            return State::IN_CSI;
        }

        // Publish CSI command and leave CSI mode
        if (b >= 0x40 && b <= 0x7E) {
            _event.command = _map_csi_to_command(b, _csi_params[0], _csi_params[1]);
            _event.type = EventType::COMMAND;
            return State::IDLE;
        }

        // Any other byte aborts to IDLE:
        return State::IDLE;
    }

    // Mapping logic

    Command InputParser::_map_control_char(const uint8_t b) {
        switch (b) {
            case 0x00: return Command::CTRL_SPACE;
            case 0x01: return Command::CTRL_A;
            case 0x02: return Command::CTRL_B;
            case 0x03: return Command::CTRL_C;
            case 0x04: return Command::CTRL_D;
            case 0x05: return Command::CTRL_E;
            case 0x06: return Command::CTRL_F;
            case 0x07: return Command::CTRL_G;
            case 0x08: return Command::BACKSPACE;
            case 0x09: return Command::TAB;
            case 0x0A: return Command::ENTER;
            case 0x0B: return Command::CTRL_K;
            case 0x0C: return Command::CTRL_L;
            case 0x0D: return Command::ENTER;
            case 0x0E: return Command::CTRL_N;
            case 0x0F: return Command::CTRL_O;
            case 0x10: return Command::CTRL_P;
            case 0x12: return Command::CTRL_R;
            case 0x15: return Command::CTRL_U;
            case 0x16: return Command::CTRL_V;
            case 0x17: return Command::CTRL_W;
            case 0x19: return Command::CTRL_Y;
            case 0x7F: return Command::BACKSPACE;
            default:   return Command::NONE;
        }
    }

    Command InputParser::_map_esc_extension(const uint8_t b) {
        // ALT_1..ALT_9 are contiguous (static_assert in parser.h)
        // so we can map digits arithmetically
        if (b >= '1' && b <= '9')
            return (Command)((int)Command::ALT_1 + (b - '1'));
        // keep 0 and the letters explicit.
        switch (b) {
            case 0x0D:
            case 0x0A:  return Command::SHIFT_ENTER;    // Alt+Enter
            case '0':   return Command::ALT_0;
            case 'b':   return Command::ALT_B;          // Alt+B (Back word)
            case 'f':   return Command::ALT_F;          // Alt+F (Forward word)
            case 'w':   return Command::ALT_W;          // Alt+W (Copy)
            default:    return Command::NONE;
        }
    }

    Command InputParser::_map_csi_to_command(const uint8_t b, const uint16_t param1,
                                             const uint16_t modifier) {
        const uint16_t mods  = (modifier > 1) ? (uint16_t)(modifier - 1) : 0;
        const bool     shift = (mods & 0x1) != 0;
        const bool     ctrl  = (mods & 0x4) != 0;
        switch (b) {
            case 'A': return shift ? Command::SHIFT_UP   : Command::UP;
            case 'B': return shift ? Command::SHIFT_DOWN : Command::DOWN;
            case 'C': return Command::RIGHT;
            case 'D': return Command::LEFT;
            case 'H': return Command::HOME;
            case 'F': return Command::END;
            case '~': {
                switch (param1) {
                    case 1: return Command::HOME;
                    case 2: return Command::NONE;
                    case 3: return Command::DELETE;
                    case 4: return Command::END;
                    case 5: return ctrl ? Command::CTRL_PAGE_UP   : Command::PAGE_UP;
                    case 6: return ctrl ? Command::CTRL_PAGE_DOWN : Command::PAGE_DOWN;
                    case 7: return Command::HOME;
                    case 8: return Command::END;
                    default: return Command::NONE;
                }
            }
            case 'Z': return Command::SHIFT_TAB;  // backtab (Shift+Tab)
            case 'u': return (param1 == 13) ? Command::SHIFT_ENTER : Command::NONE;
            default: return Command::NONE;
        }
    }

    // The Minitel function keys, SEP (0x13) then one byte, per STUM1B. VERIFY
    // ON HARDWARE: CORRECTION and ANNULATION read as transposed.
    Command InputParser::_map_sep_to_command(const uint8_t b) {
        switch (b) {
            case 'a': case 'A': return Command::ENTER;      // ENVOI
            case 'b': case 'B': return Command::ENTER;      // RETOUR
            case 'c': case 'C': return Command::NONE;       // REPETITION
            case 'd': case 'D': return Command::ALT_1;      // GUIDE
            case 'e': case 'E': return Command::BACKSPACE;  // ANNULATION
            case 'f': case 'F': return Command::CTRL_U;     // SOMMAIRE
            case 'g': case 'G': return Command::ALT_1;      // CORRECTION
            case 'h': case 'H': return Command::NONE;       // SUITE
            default: return Command::NONE;
        }
    }

    // Lifecycle

    InputParser::InputParser(Utils::BufferedSerial& serial, const uint64_t* timestamp_ms)
        : _serial(serial), _timestamp_ms(timestamp_ms) {
        _reset_event();
        _state = State::IDLE;
        _last_byte_ms = *timestamp_ms;
    }

    // Parsing

    InputEvent& InputParser::parse() {
        _reset_event();
        uint8_t b;
        // Every iteration either consumes the peeked byte or returns, so the
        // drain is bounded by what the RX ring holds.
        while (_serial.peek(&b)) {
            if (_pending_cmd != Command::NONE) {
                // Take the byte only if it coalesces into the chunk in flight.
                if (_state == State::IDLE && _is_chunkable_trigger(b) &&
                    _chunkable_command_for(b) == _pending_cmd) {
                    _take();
                    if (_pending_cmd_count < UINT16_MAX) _pending_cmd_count++;   // saturate
                    continue;
                }
                _emit_pending_command();
                return _event;
            }
            // A non-printable byte ends the TEXT batch; it stays in the ring.
            if (_state == State::TEXT && !Ansi::is_printable(b)) {
                _flush_text();
                _state = State::IDLE;
                return _event;
            }
            if (_state == State::IDLE && _is_chunkable_trigger(b)) {
                _take();
                _stage_pending_command(_chunkable_command_for(b));
                continue;
            }
            _take();
            State new_state;
            switch (_state) {
                case State::IDLE:   new_state = _handle_idle(b); break;
                case State::TEXT:   new_state = _append_text(b); break;
                case State::IN_SEP: new_state = _handle_sep(b);  break;
                case State::IN_ESC: new_state = _handle_esc(b);  break;
                case State::IN_CSI: new_state = _handle_csi(b);  break;
                default:            new_state = _state        ;  break;
            }
            _state = new_state;
            if (_event.type != EventType::NONE) return _event;
        }

        // The ring is empty: silence is what completes a chunk, a text batch
        // and an unterminated escape.
        if (_should_flush_pending_command()) {
            _emit_pending_command();
        } else if (_should_flush_text()) {
            _flush_text();
            _state = State::IDLE;
        } else if ((_state == State::IN_ESC || _state == State::IN_CSI ||
                    _state == State::IN_SEP) &&
                   (*_timestamp_ms - _last_byte_ms) >= Config::ANSI_CMD_TIMEOUT_MS) {
            if (_state == State::IN_ESC) {
                _event.command = Command::ESC;
                _event.type    = EventType::COMMAND;
            } else if (_state == State::IN_SEP) {
                // 0x13 alone is Ctrl+S; 0x13 + a letter is a Minitel function
                // key, so the key resolves the way a bare ESC does - on the
                // timeout, once no second byte came.
                _event.command = Command::CTRL_S;
                _event.type    = EventType::COMMAND;
            }
            _state = State::IDLE;
        }
        return _event;
    }

}}

#endif // BOTANIK_FEATURE_CONSOLE
