#include <cstdint>

#include "libbotanik/terminal/parser.h"

namespace Botanik { namespace Terminal {

    namespace {
        using Botanik::Utils::BufferedSerial;
        using namespace Botanik::Config;
    }

    // Initialization code

    void InputParser::_reset_event() {
        _event.type = InputEvent::Type::NONE;
        _event.command = InputEvent::Command::NONE;
        _event.batch_data = nullptr;
        _event.batch_len = 0;
        _event.prefix = 0;
        _event.value = 0;
        _event.param_count = 0;
        for (int i = 0; i < 8; i++) _event.params[i] = 0;
    }


    // Text handling helpers

    void InputParser::_store_text(uint8_t b) {
        _text_buf[_text_len++] = (char)b;
        _last_byte_ms = *_timestamp_ms;
    }

    void InputParser::_flush_text() {
        _event.type = InputEvent::Type::CONTENT;
        _event.batch_data = _text_buf;
        _event.batch_len = _text_len;
        _text_len = 0;
    }

    // Transition conditions

    bool InputParser::_is_safety_timeout_elapsed() const { 
        return (*_timestamp_ms - _last_byte_ms) >= ANSI_INPUT_BATCH_TIMEOUT_MS; 
    }

    bool InputParser::_should_flush_text() const {
        return  (_state == State::TEXT && _text_len > 0) &&
                (_serial.tx_empty() || _is_safety_timeout_elapsed());
    }

    // State handlers

    InputParser::State InputParser::_handle_idle(uint8_t b) {
        if (b == (uint8_t)Ansi::ESC)                    return State::IN_ESC;
        if (b == (uint8_t)Ansi::SEP)                    return State::IN_SEP;
        if (Ansi::is_printable(b)) { 
            _store_text(b);
            if (_serial.tx_empty()) {
                _flush_text();
                return State::IDLE;
            }
            return State::TEXT;
        }
        _event.command = _map_control_char(b);
        if (_event.command != InputEvent::Command::NONE) {
            _event.type = InputEvent::Type::COMMAND;
        }
        return State::IDLE;
    }

    InputParser::State InputParser::_handle_text(uint8_t b) {
        _store_text(b);

        if (_text_len == ANSI_INPUT_BATCH_SIZE || _serial.tx_empty()) {
            _flush_text();
            return State::IDLE;
        }
        return State::TEXT;
    }

    InputParser::State InputParser::_handle_sep(uint8_t b) {
        _event.command = _map_sep_to_command(b);
        _event.type = InputEvent::Type::COMMAND;
        return State::IDLE;
    }    

    InputParser::State InputParser::_handle_esc(uint8_t b) {
        if (b == '[') return State::IN_CSI;

        if (b >= '1' && b <= '8') {
            _event.type = InputEvent::Type::MODE;
            _event.value = b - '1';
        } else {
            _event.command = _map_esc_extension(b);
            _event.type = InputEvent::Type::COMMAND;
        }
        return State::IDLE;
    }

    InputParser::State InputParser::_handle_csi(uint8_t b) {
        if (b >= '0' && b <= '9') {
            uint8_t idx = _event.param_count;
            _event.params[idx] = (_event.params[idx] * 10) + (b - '0');
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
            _event.command = _map_csi_to_command();
            _event.type = InputEvent::Type::COMMAND;
            return State::IDLE;
        }

        return State::IDLE; // Error fallback
    }

    // Escape code mappings

    InputEvent::Command InputParser::_map_control_char(uint8_t b) {
        // TODO
    }

    InputEvent::Command InputParser::_map_esc_extension(uint8_t b) {
        // TODO
    }

    InputEvent::Command InputParser::_map_csi_to_command() {
        // TODO
    }

    InputEvent::Command InputParser::_map_sep_to_command(uint8_t b) {
        // TODO
    }

    // Public interface

    InputParser::InputParser(BufferedSerial& serial, uint32_t* timestamp_ms) 
        : _serial(serial), _timestamp_ms(timestamp_ms) {
        _reset_event();
    }

    InputEvent& InputParser::parse() {
        _reset_event();
        uint8_t b;
        // try to peek at the next character, if there is none 
        // we check if it's time to flush the batched text
        if (!_serial.peek(&b)) {
            if (_should_flush_text()) {
                _flush_text();
                _state = State::IDLE;
            }
            return _event;
        }
        // if the character we peeked at was non-printable, 
        // flush the currently batched text
        if (_state == State::TEXT && !Ansi::is_printable(b)) {
            _flush_text();
            _state = State::IDLE;
            return _event;
        }
        // read the peeked byte, and process it through the state machine
        _serial.read((char*)&b, 1);
        State new_state = _state;
        switch (_state) {
            case State::IDLE:  new_state = _handle_idle(b); break;
            case State::TEXT:  new_state = _handle_text(b); break;
            case State::IN_SEP: new_state = _handle_sep(b); break;
            case State::IN_ESC: new_state = _handle_esc(b); break;
            case State::IN_CSI: new_state = _handle_csi(b); break;
        }
        _state = new_state;
        // return the static to the event for praticity
        return _event;
    }

}}