#ifndef MODBUS_RTU_CODEC_H
#define MODBUS_RTU_CODEC_H

/**
 * @file modbus/rtu/codec.h
 * @brief Building a request and taking a response apart: the checksum, the five
 *        builders, the parser, and the 32-bit register pair.
 * @note Stateless; buffers are the caller's.
 * @see wire.h for the vocabulary and the frame sizes these agree on.
 */

#include <cstdint>

#include "libbotanik/config/base.h"
#include "libbotanik/modbus/wire.h"

// Both RTU ends: the client builds and parses frames, and the server reaches in
// here for crc16() and the frame arithmetic when it seals its answers.
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT || BOTANIK_FEATURE_MODBUS_RTU_SERVER

namespace Botanik { namespace Modbus {

    /** @name Checksum
     *  @{ */

    /** @brief CRC-16/MODBUS over @p n bytes of @p buf: reflected 0xA001, seeded
     *  0xFFFF. A null @p buf reads as the bare seed. Run over a frame INCLUDING
     *  its own two CRC bytes the result is 0, which is the residue
     *  parse_response() checks.
     *  @note Bitwise, not table-driven, deliberately: a 256-entry table saves
     *  tens of microseconds on a frame that spends 133 ms on the wire at 19200.
     *  Do not "optimise" this. */
    uint16_t crc16(const uint8_t* buf, uint16_t n);

    /** @brief Append the CRC over @p body bytes and return the total frame length.
     *  THE one place the byte order is decided - LOW BYTE FIRST, opposite to
     *  everything else on the wire - and both RTU ends go through it. */
    uint16_t seal(uint8_t* out, uint16_t body);

    /** @}
     *  @name Requests
     *
     *  Each writes a complete ADU into @p out and returns its total length, or 0
     *  having written nothing useful (buffer too small, quantity outside what the
     *  function code allows, or a null argument). Never a partial frame: half a
     *  frame on a shared bus corrupts the next transaction.
     *  @{ */

    /** @brief FC01/02/03/04: read @p qty items starting at @p addr. */
    uint16_t build_read(uint8_t* out, uint16_t cap, uint8_t unit, uint8_t fc,
                        uint16_t addr, uint16_t qty);

    /** @brief FC05: drive one coil. On the wire that is 0xFF00 for on and 0x0000
     *  for off, not 1 and 0. */
    uint16_t build_write_coil(uint8_t* out, uint16_t cap, uint8_t unit,
                              uint16_t addr, bool on);

    /** @brief FC06: write one holding register. */
    uint16_t build_write_register(uint8_t* out, uint16_t cap, uint8_t unit,
                                  uint16_t addr, uint16_t value);

    /** @brief FC15: write @p qty coils from @p bits, LSB of byte 0 first.
     *  @note Bits past @p qty in the last byte are zeroed on the way out: the
     *  spec requires padding, and a slave may reject a frame that pads
     *  with something else. */
    uint16_t build_write_coils(uint8_t* out, uint16_t cap, uint8_t unit,
                               uint16_t addr, uint16_t qty, const uint8_t* bits);

    /** @brief FC16: write @p qty holding registers from @p regs, which is @p qty
     *  big-endian words, the layout they go out in. Bytes rather than uint16_t,
     *  matching build_write_coils() and what collect_write() already produces. */
    uint16_t build_write_registers(uint8_t* out, uint16_t cap, uint8_t unit,
                                   uint16_t addr, uint16_t qty,
                                   const uint8_t* regs);

    /** @}
     *  @name Responses
     *  @{ */

    /** @brief Check the @p len bytes at @p adu against the request @p e and, on
     *  success, point @p out at the payload inside them. Checked in order:
     *  length, checksum (nothing else can be trusted before it passes), who
     *  answered, what about, and whether the shape matches.
     *  @return ERR_OK, or ERR_EXCEPTION when the slave refused - a RESULT, not a
     *  parse failure: the frame was well formed and the device is alive, and
     *  @p out carries the code. Everything else is a failure. */
    Error parse_response(const uint8_t* adu, uint16_t len, const Expect& e,
                         Response* out);

    /** @}
     *  @name Register pairs
     *
     *  A 32-bit value spans two registers, and which holds the high word is a
     *  vendor quirk rather than a protocol rule - hence a RegOrder argument (see
     *  wire.h). Through memcpy, so there is no aliasing violation and no
     *  unaligned load: a payload pointer is wherever the byte count left it.
     *  @{ */

    /** @brief The 4 bytes at @p payload as a 32-bit unsigned. */
    uint32_t u32_from_regs(const uint8_t* payload, RegOrder order);
    /** @brief The 4 bytes at @p payload as an IEEE-754 single. */
    float    f32_from_regs(const uint8_t* payload, RegOrder order);
    /** @brief @p v into the 4 bytes at @p out. */
    void     u32_to_regs(uint32_t v, RegOrder order, uint8_t* out);
    /** @brief @p v into the 4 bytes at @p out. */
    void     f32_to_regs(float v, RegOrder order, uint8_t* out);

    /** @} */

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT || BOTANIK_FEATURE_MODBUS_RTU_SERVER
#endif // MODBUS_RTU_CODEC_H
