/**
 * @file modbus/rtu/slave.cpp
 * @brief RtuSlave: framing a request without a length field, and answering it.
 */
#include "libbotanik/modbus/rtu/slave.h"

#include <cstring>

#include "libbotanik/modbus/rtu/codec.h"

#if BOTANIK_FEATURE_MODBUS_RTU_SERVER

namespace Botanik { namespace Modbus {

    // The image

    void RtuSlave::set_reg(uint16_t addr, uint16_t v) {
        if (addr < Config::MODBUS_SIM_REGS) _regs[addr] = v;
    }
    uint16_t RtuSlave::reg(uint16_t addr) const {
        return (addr < Config::MODBUS_SIM_REGS) ? _regs[addr] : 0;
    }
    void RtuSlave::set_coil(uint16_t addr, bool on) {
        if (addr < Config::MODBUS_SIM_COILS) put_bit(_coils, addr, on);
    }
    bool RtuSlave::coil(uint16_t addr) const {
        return (addr < Config::MODBUS_SIM_COILS) && bit_at(_coils, addr);
    }

    // Framing

    void RtuSlave::feed(const uint8_t* bytes, uint16_t n) {
        if (bytes == nullptr) return;
        for (uint16_t i = 0; i < n; ++i) {
            if (_req_len >= ADU_MAX) {
                // Longer than any legal request: the wire is out of step, and
                // keeping the tail would only misframe the next one too.
                _req_len = 0;
                _dropped++;
            }
            _req[_req_len++] = bytes[i];
        }
    }

    uint16_t RtuSlave::_expected() const {
        if (_req_len < 2) return 0;                 // not even a function code
        const uint8_t fc = _req[1];
        switch (fc) {
            case FC_READ_COILS: case FC_READ_DISCRETE:
            case FC_READ_HOLDING: case FC_READ_INPUT:
            case FC_WRITE_COIL: case FC_WRITE_REGISTER:
                return 8;                            // unit, fc, addr, value, crc
            case FC_WRITE_COILS: case FC_WRITE_REGISTERS:
                // The byte count is inside the frame, so the length is knowable
                // only once that byte has arrived.
                if (_req_len < 7) return 0;
                // A byte count above 247 cannot be a legal request, and a want
                // above ADU_MAX can never be satisfied: feed() resets at 256, so
                // _req_len < want would hold forever and the device would go
                // deaf. Report it as a frame to discard, not one to wait for.
                return (uint16_t)(9 + _req[6]) > ADU_MAX
                           ? ADU_MAX
                           : (uint16_t)(9 + _req[6]);
            default:
                // An unsupported function still has to be ANSWERED (with
                // exception 1), and its request is the 8-byte shape.
                return 8;
        }
    }

    uint16_t RtuSlave::poll(uint8_t* out, uint16_t cap) {
        if (out == nullptr || _req_len == 0) return 0;
        const uint16_t want = _expected();
        if (want == 0 || _req_len < want) return 0;   // still arriving

        // Copy before consuming, so a CRC failure can resync by one byte rather
        // than by a whole frame.
        uint8_t frame[ADU_MAX];
        memcpy(frame, _req, want);

        // A corrupt frame means the frame boundary is unknown, so discarding
        // `want` bytes would leave every following window failing its CRC after
        // a single lost byte. Drop one byte and rescan instead.
        if (crc16(frame, want) != 0) {
            _dropped++;
            if (_req_len > 1) memmove(_req, _req + 1, (size_t)(_req_len - 1));
            _req_len--;
            return 0;
        }

        // Sound frame: now it is safe to consume exactly this much, leaving
        // anything after it for the next poll.
        const uint16_t rest = (uint16_t)(_req_len - want);
        if (rest > 0) memmove(_req, _req + want, rest);
        _req_len = rest;

        // Unit 0 addresses every device on the segment: it is EXECUTED and not
        // answered, because several devices replying at once is a collision.
        // Reads are excluded - there is nowhere to put the answer.
        const bool broadcast = (frame[0] == 0);
        if (!broadcast && frame[0] != _unit) { _dropped++; return 0; }

        const uint8_t  fc      = frame[1];
        const uint8_t* pdu     = frame + 1;
        const uint16_t pdu_len = (uint16_t)(want - 3);   // minus unit and CRC

        if (!is_supported(fc)) {
            if (broadcast) return 0;
            // Counted here: this route returns without passing the tally at the
            // end of poll(), and every answered request is worth exactly one.
            const uint16_t n = _except(fc, EX_ILLEGAL_FUNCTION, out, cap);
            if (n > 0) _served++;
            return n;
        }

        const uint16_t addr = be16(frame[2], frame[3]);
        const uint16_t qty  = be16(frame[4], frame[5]);

        if (broadcast) {
            // A broadcast read is not a thing; a broadcast write is applied
            // silently. Either way nothing goes back on the wire.
            if (is_read(fc)) { _dropped++; return 0; }
            uint8_t sink[ADU_MAX];
            (void)_do_write(fc, pdu, pdu_len, sink, (uint16_t)sizeof sink);
            _served++;
            return 0;
        }

        const uint16_t n = is_read(fc) ? _do_read(fc, addr, qty, out, cap)
                                       : _do_write(fc, pdu, pdu_len, out, cap);
        if (n > 0) _served++;
        return n;
    }

    // Answers

    uint16_t RtuSlave::_except(uint8_t fc, Exception ex, uint8_t* out, uint16_t cap) {
        if (cap < exception_len()) return 0;
        out[0] = _unit;
        out[1] = (uint8_t)(fc | EX_BIT);
        out[2] = (uint8_t)ex;
        (void)seal(out, 3);
        _refused++;
        // NOT _served++: poll() counts any non-zero answer, and this returns
        // through it. Counting here too would make an exception worth two.
        return exception_len();
    }

    uint16_t RtuSlave::_do_read(uint8_t fc, uint16_t addr, uint16_t qty,
                                uint8_t* out, uint16_t cap) {
        if (qty < 1 || qty > max_quantity(fc))
            return _except(fc, EX_ILLEGAL_VALUE, out, cap);
        const uint16_t bytes = read_payload(fc, qty);
        const uint16_t total = (uint16_t)(5 + bytes);
        // No room to answer in. Silence would leave the master waiting out its
        // response timeout for a fault that is entirely ours, so say so - a
        // device failure is exactly what this is.
        if (cap < total) return _except(fc, EX_SLAVE_FAILURE, out, cap);

        const bool bits = is_bit(fc);
        const uint32_t last = (uint32_t)addr + qty - 1;
        if (last >= (bits ? Config::MODBUS_SIM_COILS : Config::MODBUS_SIM_REGS))
            return _except(fc, EX_ILLEGAL_ADDRESS, out, cap);

        out[0] = _unit;
        out[1] = fc;
        out[2] = (uint8_t)bytes;
        if (bits) {
            for (uint16_t i = 0; i < bytes; ++i) out[3 + i] = 0;
            for (uint16_t i = 0; i < qty; ++i)
                put_bit(out + 3, i, coil((uint16_t)(addr + i)));
        } else {
            for (uint16_t i = 0; i < qty; ++i) {
                const uint16_t v = reg((uint16_t)(addr + i));
                wr16(out + 3 + (size_t)i * 2, v);
            }
        }
        (void)seal(out, (uint16_t)(3 + bytes));
        return total;
    }

    uint16_t RtuSlave::_do_write(uint8_t fc, const uint8_t* pdu, uint16_t pdu_len,
                                 uint8_t* out, uint16_t cap) {
        const uint16_t addr = be16(pdu[1], pdu[2]);
        const uint16_t val  = be16(pdu[3], pdu[4]);

        switch (fc) {
            case FC_WRITE_COIL:
                if (val != 0xFF00 && val != 0x0000)
                    return _except(fc, EX_ILLEGAL_VALUE, out, cap);
                if (addr >= Config::MODBUS_SIM_COILS)
                    return _except(fc, EX_ILLEGAL_ADDRESS, out, cap);
                set_coil(addr, val == 0xFF00);
                break;
            case FC_WRITE_REGISTER:
                if (addr >= Config::MODBUS_SIM_REGS)
                    return _except(fc, EX_ILLEGAL_ADDRESS, out, cap);
                set_reg(addr, val);
                break;
            case FC_WRITE_COILS: {
                const uint8_t bc = pdu[5];
                // The quantity ceiling first, and in 32-bit arithmetic: the
                // expected byte count is 2*qty for registers, which wraps to 0
                // at qty 128 in a uint8_t and would then match a declared count
                // of 0, letting a 9-byte frame commit 128 registers from past
                // the end of the buffer.
                if (val < 1 || val > max_quantity(fc))
                    return _except(fc, EX_ILLEGAL_VALUE, out, cap);
                if (pdu_len < (uint16_t)(6 + bc) ||
                    (uint32_t)bc != (uint32_t)bit_bytes(val))
                    return _except(fc, EX_ILLEGAL_VALUE, out, cap);
                if ((uint32_t)addr + val > Config::MODBUS_SIM_COILS)
                    return _except(fc, EX_ILLEGAL_ADDRESS, out, cap);
                for (uint16_t i = 0; i < val; ++i)
                    set_coil((uint16_t)(addr + i), bit_at(pdu + 6, i));
                break;
            }
            case FC_WRITE_REGISTERS: {
                const uint8_t bc = pdu[5];
                if (val < 1 || val > max_quantity(fc))
                    return _except(fc, EX_ILLEGAL_VALUE, out, cap);
                if (pdu_len < (uint16_t)(6 + bc) ||
                    (uint32_t)bc != (uint32_t)val * 2u)
                    return _except(fc, EX_ILLEGAL_VALUE, out, cap);
                if ((uint32_t)addr + val > Config::MODBUS_SIM_REGS)
                    return _except(fc, EX_ILLEGAL_ADDRESS, out, cap);
                for (uint16_t i = 0; i < val; ++i)
                    set_reg((uint16_t)(addr + i), be16(pdu[6 + i * 2], pdu[7 + i * 2]));
                break;
            }
            default:
                return _except(fc, EX_ILLEGAL_FUNCTION, out, cap);
        }

        // Every write echoes its own header back: unit, fc, address, and the
        // value or the quantity, whichever it was given.
        if (cap < 8) return _except(fc, EX_SLAVE_FAILURE, out, cap);
        out[0] = _unit;
        out[1] = fc;
        wr16(out + 2, addr);
        wr16(out + 4, val);
        return seal(out, 6);
    }

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_SERVER
