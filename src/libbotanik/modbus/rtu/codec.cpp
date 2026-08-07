/**
 * @file modbus/rtu/codec.cpp
 * @brief @see codec.h
 */

#include "libbotanik/modbus/rtu/codec.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT || BOTANIK_FEATURE_MODBUS_RTU_SERVER

#include <cstring>   // memcpy - the only sanctioned route between float and its bits

namespace Botanik { namespace Modbus {

namespace {

    /** @brief Append the CRC over @p body bytes, low byte first, and return the
     *  total frame length. The one place the byte order is decided. */
    uint16_t seal(uint8_t* out, uint16_t body) {
        const uint16_t crc = crc16(out, body);
        out[body]     = lo8(crc);
        out[body + 1] = hi8(crc);
        return (uint16_t)(body + 2u);
    }

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

uint16_t build_read(uint8_t* out, uint16_t cap, uint8_t unit, uint8_t fc,
                    uint16_t addr, uint16_t qty) {
    if (out == nullptr || !is_read(fc)) return 0;
    if (qty == 0 || qty > max_quantity(fc)) return 0;
    if (cap < request_len(fc, qty)) return 0;

    out[0] = unit;
    out[1] = fc;
    out[2] = hi8(addr);
    out[3] = lo8(addr);
    out[4] = hi8(qty);
    out[5] = lo8(qty);
    return seal(out, 6);
}

uint16_t build_write_coil(uint8_t* out, uint16_t cap, uint8_t unit,
                          uint16_t addr, bool on) {
    if (out == nullptr || cap < request_len(FC_WRITE_COIL, 1)) return 0;

    out[0] = unit;
    out[1] = (uint8_t)FC_WRITE_COIL;
    out[2] = hi8(addr);
    out[3] = lo8(addr);
    // 0xFF00 and 0x0000, not 1 and 0: the coil value is a two-byte constant.
    out[4] = on ? 0xFFu : 0x00u;
    out[5] = 0x00u;
    return seal(out, 6);
}

uint16_t build_write_register(uint8_t* out, uint16_t cap, uint8_t unit,
                              uint16_t addr, uint16_t value) {
    if (out == nullptr || cap < request_len(FC_WRITE_REGISTER, 1)) return 0;

    out[0] = unit;
    out[1] = (uint8_t)FC_WRITE_REGISTER;
    out[2] = hi8(addr);
    out[3] = lo8(addr);
    out[4] = hi8(value);
    out[5] = lo8(value);
    return seal(out, 6);
}

uint16_t build_write_coils(uint8_t* out, uint16_t cap, uint8_t unit,
                           uint16_t addr, uint16_t qty, const uint8_t* bits) {
    if (out == nullptr || bits == nullptr) return 0;
    if (qty == 0 || qty > max_quantity(FC_WRITE_COILS)) return 0;
    if (cap < request_len(FC_WRITE_COILS, qty)) return 0;

    const uint16_t nb = bit_bytes(qty);
    out[0] = unit;
    out[1] = (uint8_t)FC_WRITE_COILS;
    out[2] = hi8(addr);
    out[3] = lo8(addr);
    out[4] = hi8(qty);
    out[5] = lo8(qty);
    out[6] = (uint8_t)nb;
    for (uint16_t i = 0; i < nb; ++i) out[7 + i] = bits[i];

    // Pad the tail of the last byte, rather than trusting the caller zeroed it.
    const uint8_t tail = (uint8_t)(qty % 8u);
    if (tail != 0) {
        out[7 + nb - 1] = (uint8_t)(out[7 + nb - 1] & ((1u << tail) - 1u));
    }
    return seal(out, (uint16_t)(7u + nb));
}

uint16_t build_write_registers(uint8_t* out, uint16_t cap, uint8_t unit,
                               uint16_t addr, uint16_t qty,
                               const uint16_t* regs) {
    if (out == nullptr || regs == nullptr) return 0;
    if (qty == 0 || qty > max_quantity(FC_WRITE_REGISTERS)) return 0;
    if (cap < request_len(FC_WRITE_REGISTERS, qty)) return 0;

    const uint16_t nb = (uint16_t)(qty * 2u);
    out[0] = unit;
    out[1] = (uint8_t)FC_WRITE_REGISTERS;
    out[2] = hi8(addr);
    out[3] = lo8(addr);
    out[4] = hi8(qty);
    out[5] = lo8(qty);
    out[6] = (uint8_t)nb;
    for (uint16_t i = 0; i < qty; ++i) {
        out[7 + i * 2]     = hi8(regs[i]);
        out[7 + i * 2 + 1] = lo8(regs[i]);
    }
    return seal(out, (uint16_t)(7u + nb));
}

Error parse_response(const uint8_t* adu, uint16_t len, const Expect& e,
                     Response* out) {
    if (out != nullptr) {
        out->data = nullptr;
        out->len  = 0;
        out->unit = 0;
        out->fc   = 0;
        out->ex   = EX_NONE;
    }
    if (adu == nullptr || out == nullptr) return ERR_CAPACITY;
    if (len < ADU_MIN) return ERR_SHORT;

    // Residue: the CRC bytes are inside the span, so a good frame gives 0. Done
    // before anything else is read, because nothing in the frame means anything
    // until it passes.
    if (crc16(adu, len) != 0) return ERR_CRC;

    out->unit = adu[0];
    if (adu[0] != e.unit) return ERR_UNIT;

    const uint8_t raw_fc = adu[1];
    out->fc = (uint8_t)(raw_fc & (uint8_t)~EX_BIT);
    if (out->fc != e.fc) return ERR_FC;

    if ((raw_fc & EX_BIT) != 0) {
        if (len != exception_len()) return ERR_SHORT;
        out->ex = (Exception)adu[2];
        return ERR_EXCEPTION;
    }

    if (is_read(e.fc)) {
        const uint16_t want = read_payload(e.fc, e.qty);
        // A request we could not have sent: refuse before the byte count is
        // compared against it, so a caller's bad Expect cannot size anything.
        if (want == 0 || want > PAYLOAD_MAX) return ERR_COUNT;
        if (len < 5) return ERR_SHORT;
        if (adu[2] != (uint8_t)want) return ERR_COUNT;
        if (len != (uint16_t)(5u + want)) return ERR_SHORT;
        out->data = adu + 3;
        out->len  = want;
        return ERR_OK;
    }

    // Every write echoes its own header back.
    if (!is_supported(e.fc)) return ERR_FC;
    if (len != 8) return ERR_SHORT;
    if (be16(adu[2], adu[3]) != e.addr) return ERR_ECHO;
    if (e.fc == FC_WRITE_COILS || e.fc == FC_WRITE_REGISTERS) {
        if (be16(adu[4], adu[5]) != e.qty) return ERR_ECHO;
    }
    // The echoed body, so a caller may check FC05's coil state or FC06's value
    // for itself. Those two echo a VALUE where the multi-writes echo a count,
    // which is why this layer does not compare it.
    out->data = adu + 2;
    out->len  = 4;
    return ERR_OK;
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
    out[0] = hi8(first);
    out[1] = lo8(first);
    out[2] = hi8(second);
    out[3] = lo8(second);
}

void f32_to_regs(float v, RegOrder order, uint8_t* out) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof bits);
    u32_to_regs(bits, order, out);
}

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT || BOTANIK_FEATURE_MODBUS_RTU_SERVER
