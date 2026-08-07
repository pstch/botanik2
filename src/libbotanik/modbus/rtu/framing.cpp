/**
 * @file modbus/rtu/framing.cpp
 * @brief @see framing.h
 */

#include "libbotanik/modbus/rtu/framing.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

namespace Botanik { namespace Modbus {

void RtuFramer::arm(const Expect& e, uint64_t now_ms, uint32_t timeout_ms,
                    const uint8_t* echo, uint16_t echo_len) {
    _exp         = e;
    _len         = 0;
    _want        = 0;
    _err         = ERR_OK;
    _state       = FR_RX;
    _deadline_ms = now_ms + (uint64_t)timeout_ms;
    _resp        = Response();
    _echo        = (echo_len != 0) ? echo : nullptr;
    _echo_len    = (echo != nullptr) ? echo_len : 0;
    _echo_pos    = 0;
}

void RtuFramer::disarm() {
    _state = FR_IDLE;
}

uint16_t RtuFramer::feed(const uint8_t* b, uint16_t n) {
    if (b == nullptr || n == 0) return 0;
    if (_state != FR_RX) {
        _stray_bytes += n;
        return 0;
    }

    uint16_t used = 0;
    while (used < n && _state == FR_RX) {
        const uint8_t c = b[used];

        // Still holding a candidate echo? Keep holding while it matches.
        if (_echo_pos < _echo_len) {
            if (c == _echo[_echo_pos]) {
                _echo_pos++;
                used++;
                continue;
            }
            // It diverged, so it was never our transmission: everything held so
            // far belongs to the answer, and so does this byte.
            _replay_echo();
            if (_state != FR_RX) break;
        }

        _accept(c);
        used++;
    }

    if (used < n) _stray_bytes += (uint16_t)(n - used);
    return used;
}

void RtuFramer::_accept(uint8_t b) {
    if (_len >= (uint16_t)sizeof _buf) {
        // Unreachable for a legal frame: _want is capped below the buffer
        // before it is ever trusted. Here so that if it ever does happen it
        // is a counted fault rather than a write past the array.
        _fail(ERR_CAPACITY);
        return;
    }
    _buf[_len++] = b;
    _resolve();
    if (_state == FR_RX && _want != 0 && _len == _want) _complete();
}

void RtuFramer::_replay_echo() {
    const uint16_t held = _echo_pos;
    // Stop matching first: _accept() must not recurse back into the echo path.
    _echo_pos = _echo_len;
    for (uint16_t i = 0; i < held && _state == FR_RX; ++i) _accept(_echo[i]);
}

int RtuFramer::poll(uint64_t now_ms) {
    switch (_state) {
        case FR_DONE: return 1;
        case FR_FAIL: return -1;
        case FR_IDLE: return 0;
        case FR_RX:   break;
    }

    // The one thing time is evidence of: a slave that has said nothing. Not
    // where a frame ends - see the file note.
    if (now_ms >= _deadline_ms) {
        // One last possibility before calling it silence: a whole candidate echo
        // and nothing behind it. A write response is byte-identical to the
        // request it answers, so on a board that does not echo, that candidate
        // IS the reply and discarding it would time out a healthy device.
        //
        // Speculative, so it is undone on failure: when the candidate really was
        // our own transmission the replayed frame fails its own checks, and this
        // is silence rather than a malformed answer. The counters are snapshotted
        // because _fail() and _complete() move them, and a frame we sent must not
        // show up as a bad one we received.
        if (_len == 0 && _echo_len != 0 && _echo_pos == _echo_len) {
            const uint32_t bad = _bad_frames;
            const uint32_t crc = _crc_errors;
            _replay_echo();
            if (_state == FR_DONE) return 1;
            _bad_frames = bad;
            _crc_errors = crc;
            _len        = 0;
            _want       = 0;
            _err        = ERR_OK;
            _resp       = Response();
        }
        _timeouts++;
        _err   = ERR_TIMEOUT;
        _state = FR_FAIL;
        return -1;
    }
    return 0;
}

void RtuFramer::reset_counters() {
    _frames      = 0;
    _crc_errors  = 0;
    _bad_frames  = 0;
    _timeouts    = 0;
    _stray_bytes = 0;
}

void RtuFramer::_resolve() {
    if (_want != 0 || _len < 2) return;

    // The exception bit first, before the request is consulted at all: a refusal
    // is five bytes whatever was asked, so a framer that sized from the request
    // would sit waiting for a tail that is never coming.
    if ((_buf[1] & EX_BIT) != 0) {
        _want = exception_len();
        return;
    }

    if (is_read(_exp.fc)) {
        if (_len < 3) return;
        const uint16_t payload = read_payload(_exp.fc, _exp.qty);
        // Reject a byte count that disagrees with the request HERE, before it
        // sizes anything. A slave claiming more than it was asked for is the
        // one input that could reach past the buffer, so it is refused rather
        // than clamped: clamping would leave the frame misaligned and the next
        // one unparseable too.
        if (payload == 0 || _buf[2] != (uint8_t)payload) {
            _fail(ERR_COUNT);
            return;
        }
        _want = (uint16_t)(5u + payload);
        return;
    }

    // Every write echoes its header, whatever it wrote.
    _want = 8;
}

void RtuFramer::_complete() {
    _err = parse_response(_buf, _len, _exp, &_resp);
    if (_err == ERR_OK || _err == ERR_EXCEPTION) {
        // An exception frame COMPLETED. The slave is alive and answering, which
        // is what the caller needs to know; whether it agreed is err()'s business.
        _frames++;
        _state = FR_DONE;
        return;
    }
    if (_err == ERR_CRC) _crc_errors++;
    else                 _bad_frames++;
    _state = FR_FAIL;
}

void RtuFramer::_fail(Error e) {
    _err   = e;
    _state = FR_FAIL;
    _bad_frames++;
}

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
