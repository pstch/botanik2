// REVIEW_35: TODO
/**
 * @file parser.cpp
 * @brief InputParser implementation - the byte-stream decode state machine that
 *        turns raw input into InputEvents.
 */
#include <cstdint>

#include "parser.h"

namespace Botanik { namespace Terminal {

    namespace {
        using Command = InputEvent::Command;
        using EventType = InputEvent::EventType;
    }

    // Initialization
    // ------------------------------------------------------------------------

    void InputParser::_reset_event() {
        // Value-init restores every field to its in-class default (count=1, the
        // rest zero/NONE) and zero-fills params[], which has no default.
        _event = InputEvent();
    }

    // Chunkable command helpers
    // ------------------------------------------------------------------------

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
    // ------------------------------------------------------------------------

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

    // Store one text byte, flushing if it fills the batch. Returns IDLE on a
    // flush, TEXT otherwise. Checking after the store keeps the batch from
    // overflowing _text_buf, including the degenerate ANSI_INPUT_BATCH_SIZE ==
    // 1 case.
    InputParser::State InputParser::_append_text(uint8_t b) {
        _store_text(b);
        if (_text_len == Config::ANSI_INPUT_BATCH_SIZE) {
            _flush_text();
            return State::IDLE;
        }
        return State::TEXT;
    }

    // Flush policy
    // ------------------------------------------------------------------------

    bool InputParser::_backpressure_holds(uint32_t start_ms) const {
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
    // ------------------------------------------------------------------------

    InputParser::State InputParser::_set_command(Command c) {
        _event.command = c;
        if (c != Command::NONE) _event.type = EventType::COMMAND;
        return State::IDLE;
    }
    InputParser::State InputParser::_handle_idle(uint8_t b) {
        if (b == (uint8_t)(*Ansi::ESC))  return State::IN_ESC;
        if (b == (uint8_t)(*Ansi::SEP))  return State::IN_SEP;
        if (Ansi::is_printable(b))       return _append_text(b);
        return _set_command(_map_control_char(b));
    }

    InputParser::State InputParser::_handle_sep(uint8_t b) {
        return _set_command(_map_sep_to_command(b));
    }

    InputParser::State InputParser::_handle_esc(uint8_t b) {
        if (b == '[') return State::IN_CSI;
        return _set_command(_map_esc_extension(b));
    }

    InputParser::State InputParser::_handle_csi(uint8_t b) {
        if (b >= '0' && b <= '9') {
            uint8_t idx = _event.param_count;
            if (idx < 8) {
                // Saturate rather than wrap: a long digit run on a noisy line
                // must not roll over into a small value that spuriously matches
                // a command.
                uint32_t v = (uint32_t)_event.params[idx] * 10 + (uint32_t)(b - '0');
                _event.params[idx] = (v > 0xFFFF) ? (uint16_t)0xFFFF : (uint16_t)v;
            }
            return State::IN_CSI;
        }

        if (b == ';' || b == ':') {
            if (_event.param_count < 7) _event.param_count++;
            return State::IN_CSI;
        }

        // Private-mode prefix (? > < =): consumed to stay in CSI, not dispatched.
        if (b == '?' || b == '>' || b == '<' || b == '=') return State::IN_CSI;

        if (b >= 0x40 && b <= 0x7E) {
            _event.command = _map_csi_to_command(b, _event.params[0], _event.params[1]);
            _event.type = EventType::COMMAND;
            return State::IDLE;
        }

        // Any other byte (e.g. a 0x20-0x2F intermediate) aborts to IDLE:
        // intermediate bytes are unsupported - none of the sequences we map use
        // them.
        return State::IDLE;
    }

    // Mapping logic
    // ------------------------------------------------------------------------

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
            // unreachable: ESC is intercepted in _handle_idle
            case 0x1B: return Command::ESC;
            case 0x7F: return Command::BACKSPACE;
            default:   return Command::NONE;
        }
    }

    Command InputParser::_map_esc_extension(const uint8_t b) {
        // ALT_1..ALT_9 are contiguous (static_assert in parser.h); map digits
        // arithmetically, keep 0 and the letters explicit.
        if (b >= '1' && b <= '9')
            return (Command)((int)Command::ALT_1 + (b - '1'));
        switch (b) {
            case 0x0D: case 0x0A: return Command::SHIFT_ENTER;  // Alt+Enter
            case '0':   return Command::ALT_0;
            case 'b':   return Command::ALT_B;         // Alt+B (Back word)
            case 'f':   return Command::ALT_F;         // Alt+F (Forward word)
            default:    return Command::NONE;
        }
    }

    Command InputParser::_map_csi_to_command(const uint8_t b, const uint16_t param1,
                                             const uint16_t modifier) {
        switch (b) {
            case 'A': return Command::UP;
            case 'B': return Command::DOWN;
            case 'C': return Command::RIGHT;
            case 'D': return Command::LEFT;
            case 'H': return Command::HOME;
            case 'F': return Command::END;

            case '~': {
                // xterm encodes modifiers as a 1-based bitmask second param
                // (Shift=1, Alt=2, Ctrl=4): e.g. Ctrl+PageUp is CSI 5;5~. The
                // Ctrl bit is set when (modifier - 1) & 4; modifier is 0 when
                // no second param was sent (plain PageUp/PageDown).
                const bool ctrl = modifier > 1 && (((modifier - 1) & 0x4) != 0);
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
            // kitty: CSI 13;2u
            case 'u': return (param1 == 13) ? Command::SHIFT_ENTER : Command::NONE;
            default: return Command::NONE;
        }
    }

    // Minitel SEP (function-key) codes. Some map to the same command on
    // purpose: a/A (ENVOI) and b/B (RETOUR) both act as ENTER; d/D (SOMMAIRE)
    // and g/G (GUIDE) both jump to page 1.
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

    // Lifecycle
    // ------------------------------------------------------------------------

    InputParser::InputParser(Utils::BufferedSerial& serial, const uint32_t* timestamp_ms)
        : _serial(serial), _timestamp_ms(timestamp_ms) {
        _reset_event();
        _state = State::IDLE;
        _last_byte_ms = *timestamp_ms;
    }

    // Parsing
    // ------------------------------------------------------------------------

    InputEvent& InputParser::parse() {
        _reset_event();
        uint8_t b;
        // Drain available bytes through the state machine until something worth
        // returning happens. A stored TEXT byte produces no event on its own,
        // so looping lets a burst accumulate into one batch before the caller
        // sees it.
        while (true) {
            // No byte available: time out an in-flight sequence, or flush a
            // pending command / text batch on idle.
            if (!_serial.peek(&b)) {
                if (_should_flush_pending_command()) {
                    _emit_pending_command();
                } else if (_should_flush_text()) {
                    _flush_text();
                    _state = State::IDLE;
                } else if ((_state == State::IN_ESC || _state == State::IN_CSI ||
                            _state == State::IN_SEP) &&
                           (*_timestamp_ms - _last_byte_ms) >= Config::ANSI_CMD_TIMEOUT_MS) {
                    // An incomplete escape timed out: a lone ESC emits the ESC
                    // command; a partial CSI or a stray SEP (0x13/XOFF) just
                    // drops to IDLE so it can't swallow the next keystroke.
                    if (_state == State::IN_ESC) {
                        _event.command = Command::ESC;
                        _event.type    = EventType::COMMAND;
                    }
                    _state = State::IDLE;
                }
                return _event;
            }
            // Pending chunkable command: consume the byte only if it's the same
            // trigger (coalesce). Anything else emits the pending event now and
            // leaves the byte for the next parse().
            if (_pending_cmd != Command::NONE) {
                if (_state == State::IDLE && _is_chunkable_trigger(b) &&
                    _chunkable_command_for(b) == _pending_cmd) {
                    _serial.read((char*)&b, 1);
                    _last_byte_ms = *_timestamp_ms;
                    // saturate, never wrap
                    if (_pending_cmd_count < UINT16_MAX) _pending_cmd_count++;
                    continue;
                }
                // Coalescing is IDLE-only: a chunkable byte arriving mid-escape
                // (rare; escapes come atomically, ANSI_CMD_TIMEOUT_MS bounds a
                // split one) is emitted-and-left, then runs through the FSM
                // next parse(). Keeps the parser single-byte.
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
            // Chunkable trigger in IDLE: consume + stage. The byte never
            // reaches the state machine - chunking lives here so the parser
            // stays single-byte.
            if (_state == State::IDLE && _is_chunkable_trigger(b)) {
                _serial.read((char*)&b, 1);
                _last_byte_ms = *_timestamp_ms;
                _stage_pending_command(_chunkable_command_for(b));
                continue;
            }
            // Read the peeked byte and run it through the state machine.
            _serial.read((char*)&b, 1);
            _last_byte_ms = *_timestamp_ms;
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
            // A handler produced an event (command, full-batch flush, ...) -
            // return it. Otherwise keep draining.
            if (_event.type != EventType::NONE) return _event;
        }
    }

}}