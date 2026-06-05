#include <cstdint>

#include "libbotanik/terminal/parser.h"

namespace Botanik { namespace Terminal {

    namespace {
        using Command = InputEvent::Command;
        using EventType = InputEvent::EventType;
    }

    // Initialization code

    void InputParser::_reset_event() {
        _event.type = EventType::NONE;
        _event.command = Command::NONE;
        _event.count = 1;
        _event.batch_data = nullptr;
        _event.batch_len = 0;
        _event.prefix = 0;
        _event.value = 0;
        _event.param_count = 0;
        for (int i = 0; i < 8; i++) _event.params[i] = 0;
    }

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
        // Start coalescing a chunkable command; identical triggers bump count.
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

    // Flush policy (shared by the text batch and the chunked command)

    bool InputParser::_backpressure_holds(uint32_t start_ms) const {
        // Hold a pending batch (text or a coalesced command) while the TX ring
        // is still draining, so per-byte flushes collapse into one cheaper
        // reconcile. The safety timeout — measured from when the batch started,
        // not the last byte — breaks the deadlock if the TX never drains.
        if (_serial.tx_empty()) return false;
        return (*_timestamp_ms - start_ms) < Config::ANSI_INPUT_BATCH_TIMEOUT_MS;
    }

    bool InputParser::_should_flush_text() const {
        if (_state != State::TEXT || _text_len == 0) return false;
        return !_backpressure_holds(_batch_start_ms);
    }

    bool InputParser::_should_flush_pending_command() const {
        if (_pending_cmd == Command::NONE) return false;
        return !_backpressure_holds(_pending_cmd_start_ms);
    }

    // State handlers

    InputParser::State InputParser::_handle_idle(uint8_t b) {
        if (b == (uint8_t)(*Ansi::ESC))  return State::IN_ESC;
        if (b == (uint8_t)(*Ansi::SEP))  return State::IN_SEP;
        if (Ansi::is_printable(b)) {
            _store_text(b);
            return State::TEXT;
        }
        _event.command = _map_control_char(b);
        if (_event.command != Command::NONE) {
            _event.type = EventType::COMMAND;
        }
        return State::IDLE;
    }

    InputParser::State InputParser::_handle_text(uint8_t b) {
        _store_text(b);
        if (_text_len == Config::ANSI_INPUT_BATCH_SIZE) {
            _flush_text();
            return State::IDLE;
        }
        return State::TEXT;
    }

    InputParser::State InputParser::_handle_sep(uint8_t b) {
        _event.command = _map_sep_to_command(b);
        if (_event.command != Command::NONE)
            _event.type = EventType::COMMAND;
        return State::IDLE;
    }

    InputParser::State InputParser::_handle_esc(uint8_t b) {
        if (b == '[') return State::IN_CSI;

        _event.command = _map_esc_extension(b);
        if (_event.command != Command::NONE) {
            _event.type = EventType::COMMAND;
        }

        return State::IDLE;
    }

    InputParser::State InputParser::_handle_csi(uint8_t b) {
        if (b >= '0' && b <= '9') {
            uint8_t idx = _event.param_count;
            if (idx < 8) {
                _event.params[idx] = (_event.params[idx] * 10) + (b - '0');
            }
            return State::IN_CSI;
        }

        if (b == ';' || b == ':') {
            if (_event.param_count < 7) _event.param_count++;
            return State::IN_CSI;
        }

        if (b == '?' || b == '>' || b == '<' || b == '=') {
            _event.prefix = b;
            return State::IN_CSI;
        }

        if (b >= 0x40 && b <= 0x7E) {
            _event.value = b;
            _event.param_count++;
            _event.command = _map_csi_to_command(b, _event.params[0]);
            _event.type = EventType::COMMAND;
            return State::IDLE;
        }

        return State::IDLE; // Error fallback
    }

    // Escape code mappings

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
            case 0x10: return Command::CTRL_P;
            case 0x12: return Command::CTRL_R;
            case 0x15: return Command::CTRL_U;
            case 0x17: return Command::CTRL_W;
            case 0x19: return Command::CTRL_Y;
            case 0x1B: return Command::ESC;
            case 0x7F: return Command::BACKSPACE;
            default:   return Command::NONE;
        }
    }

    Command InputParser::_map_esc_extension(const uint8_t b) {
        switch (b) {
            case 0x0D:
            case 0x0A:  return Command::SHIFT_ENTER;  // Alt+Enter
            case '0':   return Command::ALT_0;
            case '1':   return Command::ALT_1;
            case '2':   return Command::ALT_2;
            case '3':   return Command::ALT_3;
            case '4':   return Command::ALT_4;
            case '5':   return Command::ALT_5;
            case '6':   return Command::ALT_6;
            case '7':   return Command::ALT_7;
            case '8':   return Command::ALT_8;
            case '9':   return Command::ALT_9;
            case 'b':   return Command::ALT_B;         // Alt+B (Back word)
            case 'f':   return Command::ALT_F;         // Alt+F (Forward word)
            default:    return Command::NONE;
        }
    }

    Command InputParser::_map_csi_to_command(const uint8_t b, const uint16_t param1) {
        switch (b) {
            case 'A': return Command::UP;
            case 'B': return Command::DOWN;
            case 'C': return Command::RIGHT;
            case 'D': return Command::LEFT;
            case 'H': return Command::HOME;
            case 'F': return Command::END;

            case '~':
                switch (param1) {
                    case 1: return Command::HOME;
                    case 2: return Command::NONE;
                    case 3: return Command::DELETE;
                    case 4: return Command::END;
                    case 5: return Command::PAGE_UP;
                    case 6: return Command::PAGE_DOWN;
                    case 7: return Command::HOME;
                    case 8: return Command::END;
                    default: return Command::NONE;
                }
            case 'Z': return Command::TAB;
            case 'u': return (param1 == 13) ? Command::SHIFT_ENTER : Command::NONE; // kitty: CSI 13;2u
            default: return Command::NONE;
        }
    }

    Command InputParser::_map_sep_to_command(const uint8_t b) {
        switch (b) {
            case 'a': case 'A': return Command::ENTER;
            case 'b': case 'B': return Command::ENTER;
            case 'c': case 'C': return Command::NONE;
            case 'd': case 'D': return Command::ALT_1;
            case 'e': case 'E': return Command::BACKSPACE;
            case 'f': case 'F': return Command::CTRL_U;
            case 'g': case 'G': return Command::ALT_1;
            case 'h': case 'H': return Command::NONE;
            default: return Command::NONE;
        }
    }

    // Public interface

    InputParser::InputParser(Utils::BufferedSerial& serial, uint32_t* timestamp_ms)
        : _serial(serial), _timestamp_ms(timestamp_ms) {
        _reset_event();
        _state = State::IDLE;
        _last_byte_ms = *timestamp_ms;
    }

    InputEvent& InputParser::parse() {
        _reset_event();
        uint8_t b;
        // Drain available bytes through the state machine until something
        // worth returning happens. Storing a TEXT byte produces no event on
        // its own — looping here lets a burst accumulate into the batch
        // before the caller sees a flush event, instead of leaving after
        // each stored byte and only flushing when the idle window elapses.
        while (true) {
            // No byte available: decide whether to time out an in-flight
            // sequence, flush a pending chunked command, or flush the
            // pending text batch on idle.
            if (!_serial.peek(&b)) {
                if (_should_flush_pending_command()) {
                    _emit_pending_command();
                } else if (_should_flush_text()) {
                    _flush_text();
                    _state = State::IDLE;
                } else if (_state == State::IN_ESC &&
                           (*_timestamp_ms - _last_byte_ms) >= Config::ANSI_ESC_TIMEOUT_MS) {
                    _event.command = Command::ESC;
                    _event.type    = EventType::COMMAND;
                    _state = State::IDLE;
                } else if (_state == State::IN_CSI &&
                           (*_timestamp_ms - _last_byte_ms) >= Config::ANSI_ESC_TIMEOUT_MS) {
                    _state = State::IDLE;
                }
                return _event;
            }
            // Pending chunkable command: only consume the byte if it's the
            // same trigger (in which case we coalesce). Anything else
            // forces an immediate emit of the pending event; the byte
            // stays in the serial buffer for the next parse() call.
            if (_pending_cmd != Command::NONE) {
                if (_state == State::IDLE && _is_chunkable_trigger(b) &&
                    _chunkable_command_for(b) == _pending_cmd) {
                    _serial.read((char*)&b, 1);
                    _last_byte_ms = *_timestamp_ms;
                    _pending_cmd_count++;
                    continue;
                }
                _emit_pending_command();
                return _event;
            }
            // Non-printable interrupts a TEXT batch: flush what's there now
            // and let the next parse() call dispatch the byte itself.
            if (_state == State::TEXT && !Ansi::is_printable(b)) {
                _flush_text();
                _state = State::IDLE;
                return _event;
            }
            // Chunkable command trigger in IDLE state: consume + stage.
            // The byte never reaches the state machine — chunking happens
            // entirely at this layer so the rest of the parser stays
            // single-byte-at-a-time.
            if (_state == State::IDLE && _is_chunkable_trigger(b)) {
                _serial.read((char*)&b, 1);
                _last_byte_ms = *_timestamp_ms;
                _stage_pending_command(_chunkable_command_for(b));
                continue;
            }
            // Read the peeked byte and run it through the state machine.
            _serial.read((char*)&b, 1);
            _last_byte_ms = *_timestamp_ms;
            State new_state = _state;
            switch (_state) {
                case State::IDLE:  new_state = _handle_idle(b); break;
                case State::TEXT:  new_state = _handle_text(b); break;
                case State::IN_SEP: new_state = _handle_sep(b); break;
                case State::IN_ESC: new_state = _handle_esc(b); break;
                case State::IN_CSI: new_state = _handle_csi(b); break;
            }
            _state = new_state;
            // A handler produced an event (command, full-batch flush, ...) —
            // return it. Otherwise keep draining.
            if (_event.type != EventType::NONE) return _event;
        }
    }

}}