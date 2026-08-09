/**
 * @file modbus/rtu/codec.cpp
 * @brief @see codec.h
 */

#include "libbotanik/modbus/rtu/codec.h"
#include "libbotanik/modbus/response.h"   // match_response - shared with the other transport

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT || BOTANIK_FEATURE_MODBUS_RTU_SERVER

#include <cstring>   // memcpy - the only sanctioned route between float and its bits

namespace Botanik { namespace Modbus {

uint16_t seal(uint8_t* out, uint16_t body) {
    const uint16_t crc = crc16(out, body);
    out[body]     = lo8(crc);
    out[body + 1] = hi8(crc);
    return (uint16_t)(body + 2u);
}

namespace {

    /** @brief The largest byte count a read response can legitimately carry
     *  (125 registers, or 2000 coils packed). Anything above it means the
     *  request itself was malformed, and it is checked before the number is
     *  used to size a buffer. */
    const uint16_t PAYLOAD_MAX = 250;

}  // namespace

uint16_t crc16(const uint8_t* buf, uint16_t n) {
    uint16_t crc = 0xFFFFu;
    if (buf == nullptr) return crc;
    for (uint16_t i = 0; i < n; ++i) {
        crc = (uint16_t)(crc ^ (uint16_t)buf[i]);
        for (uint8_t b = 0; b < 8; ++b) {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u)
                             : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

namespace {

    /** @brief The shape three of the five requests share: unit, code, and two
     *  big-endian words, sealed. What each word MEANS differs - address and
     *  quantity, address and coil state, address and value - which is exactly
     *  why the callers keep their own names and their own validation. */
    uint16_t build_pair(uint8_t* out, uint8_t unit, uint8_t fc,
                        uint16_t a, uint16_t b) {
        out[0] = unit;
        out[1] = fc;
        wr16(out + 2, a);
        wr16(out + 4, b);
        return seal(out, 6);
    }

    /** @brief The header the two multi-writes share, up to and including the
     *  byte count. @return where the payload starts. */
    uint16_t build_multi(uint8_t* out, uint8_t unit, uint8_t fc,
                         uint16_t addr, uint16_t qty, uint16_t nbytes) {
        out[0] = unit;
        out[1] = fc;
        wr16(out + 2, addr);
        wr16(out + 4, qty);
        out[6] = (uint8_t)nbytes;
        return 7;
    }

}  // namespace

uint16_t build_read(uint8_t* out, uint16_t cap, uint8_t unit, uint8_t fc,
                    uint16_t addr, uint16_t qty) {
    if (out == nullptr || !is_read(fc)) return 0;
    if (qty == 0 || qty > max_quantity(fc)) return 0;
    if (cap < request_len(fc, qty)) return 0;
    return build_pair(out, unit, fc, addr, qty);
}

uint16_t build_write_coil(uint8_t* out, uint16_t cap, uint8_t unit,
                          uint16_t addr, bool on) {
    if (out == nullptr || cap < request_len(FC_WRITE_COIL, 1)) return 0;
    // 0xFF00 and 0x0000, not 1 and 0: the coil value is a two-byte constant.
    return build_pair(out, unit, (uint8_t)FC_WRITE_COIL, addr,
                      on ? (uint16_t)0xFF00u : (uint16_t)0x0000u);
}

uint16_t build_write_register(uint8_t* out, uint16_t cap, uint8_t unit,
                              uint16_t addr, uint16_t value) {
    if (out == nullptr || cap < request_len(FC_WRITE_REGISTER, 1)) return 0;
    return build_pair(out, unit, (uint8_t)FC_WRITE_REGISTER, addr, value);
}

uint16_t build_write_coils(uint8_t* out, uint16_t cap, uint8_t unit,
                           uint16_t addr, uint16_t qty, const uint8_t* bits) {
    if (out == nullptr || bits == nullptr) return 0;
    if (qty == 0 || qty > max_quantity(FC_WRITE_COILS)) return 0;
    if (cap < request_len(FC_WRITE_COILS, qty)) return 0;

    const uint16_t nb = bit_bytes(qty);
    const uint16_t at = build_multi(out, unit, (uint8_t)FC_WRITE_COILS, addr, qty, nb);
    for (uint16_t i = 0; i < nb; ++i) out[at + i] = bits[i];

    // Pad the tail of the last byte, rather than trusting the caller zeroed it.
    const uint8_t tail = (uint8_t)(qty % 8u);
    if (tail != 0) {
        out[7 + nb - 1] = (uint8_t)(out[7 + nb - 1] & ((1u << tail) - 1u));
    }
    return seal(out, (uint16_t)(at + nb));
}

uint16_t build_write_registers(uint8_t* out, uint16_t cap, uint8_t unit,
                               uint16_t addr, uint16_t qty,
                               const uint8_t* regs) {
    if (out == nullptr || regs == nullptr) return 0;
    if (qty == 0 || qty > max_quantity(FC_WRITE_REGISTERS)) return 0;
    if (cap < request_len(FC_WRITE_REGISTERS, qty)) return 0;

    const uint16_t nb = (uint16_t)(qty * 2u);
    const uint16_t at = build_multi(out, unit, (uint8_t)FC_WRITE_REGISTERS,
                                    addr, qty, nb);
    for (uint16_t i = 0; i < nb; ++i) out[at + i] = regs[i];
    return seal(out, (uint16_t)(at + nb));
}

Error parse_response(const uint8_t* adu, uint16_t len, const Expect& e,
                     Response* out) {
    if (out != nullptr) *out = Response();
    if (adu == nullptr || out == nullptr) return ERR_CAPACITY;
    if (len < ADU_MIN) return ERR_SHORT;

    // The one thing that is RTU's: the checksum. Residue - the CRC bytes are
    // inside the span, so a good frame gives 0 - and done before anything else
    // is read, because nothing in the frame means anything until it passes.
    if (crc16(adu, len) != 0) return ERR_CRC;

    // Everything after the unit byte, minus the two CRC bytes. What is inside a
    // frame is the protocol's business, not this transport's.
    return match_response(adu[0], adu + 1, (uint16_t)(len - 3), e, out);
}

uint32_t u32_from_regs(const uint8_t* payload, RegOrder order) {
    const uint16_t first  = be16(payload[0], payload[1]);
    const uint16_t second = be16(payload[2], payload[3]);
    const uint16_t hi = (order == REG_HI_FIRST) ? first : second;
    const uint16_t lo = (order == REG_HI_FIRST) ? second : first;
    return ((uint32_t)hi << 16) | (uint32_t)lo;
}

float f32_from_regs(const uint8_t* payload, RegOrder order) {
    const uint32_t bits = u32_from_regs(payload, order);
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

void u32_to_regs(uint32_t v, RegOrder order, uint8_t* out) {
    const uint16_t hi = (uint16_t)(v >> 16);
    const uint16_t lo = (uint16_t)(v & 0xFFFFu);
    const uint16_t first  = (order == REG_HI_FIRST) ? hi : lo;
    const uint16_t second = (order == REG_HI_FIRST) ? lo : hi;
    wr16(out, first);
    wr16(out + 2, second);
}

void f32_to_regs(float v, RegOrder order, uint8_t* out) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof bits);
    u32_to_regs(bits, order, out);
}

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT || BOTANIK_FEATURE_MODBUS_RTU_SERVER
