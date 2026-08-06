#ifndef MODBUS_CODEC_H
#define MODBUS_CODEC_H

/**
 * @file codec.h
 * @brief Building a request and taking a response apart: the checksum, the five
 *        builders, the parser, and the 32-bit register pair.
 *
 * A BLOCK by the plc/ rule (these loop over buffers, so they need a .cpp), but
 * stateless all the same: no object, no member, nothing remembered between
 * calls, and nothing here allocates or owns memory. Buffers are the caller's.
 *
 * @see wire.h for the vocabulary and the frame sizes these agree on.
 */

#include <cstdint>

#include "wire.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

namespace Botanik { namespace Modbus {

    /** @name Checksum
     *  @{ */

    /** @brief CRC-16/MODBUS over @p n bytes of @p buf: reflected 0xA001, seeded
     *  0xFFFF. A null @p buf reads as the bare seed.
     *
     *  Bitwise rather than table-driven, deliberately. A 256-entry table would
     *  save around 2000 iterations on a maximum-length frame, which is tens of
     *  microseconds on a 480 MHz M7 - against the 133 ms that same frame spends
     *  on the wire at 19200. The flash costs more than the time is worth. Do not
     *  "optimise" this.
     *
     *  The CRC goes on the wire LOW BYTE FIRST, which is the one detail every
     *  implementation gets wrong at least once. The property that catches it:
     *  run this over a frame INCLUDING its own two CRC bytes and the result is
     *  0. That residue is what parse_response() checks, so the byte order is
     *  verified on every single frame rather than once in a test. */
    uint16_t crc16(const uint8_t* buf, uint16_t n);

    /** @}
     *  @name Requests
     *
     *  Each writes a complete ADU into @p out and returns its total length, or
     *  returns 0 having written nothing useful. 0 means the buffer was too
     *  small, the quantity was outside what the function code allows, or an
     *  argument was null.
     *
     *  Never a partial frame. A truncated request is a caller bug, and half a
     *  frame on a shared bus does not fail quietly: it corrupts whatever
     *  transaction comes next and reads as a cable fault.
     *  @{ */

    /** @brief FC01/02/03/04: read @p qty items starting at @p addr. */
    uint16_t build_read(uint8_t* out, uint16_t cap, uint8_t unit, uint8_t fc,
                        uint16_t addr, uint16_t qty);

    /** @brief FC05: drive one coil. On the wire that is 0xFF00 for on and
     *  0x0000 for off, not 1 and 0. */
    uint16_t build_write_coil(uint8_t* out, uint16_t cap, uint8_t unit,
                              uint16_t addr, bool on);

    /** @brief FC06: write one holding register. */
    uint16_t build_write_register(uint8_t* out, uint16_t cap, uint8_t unit,
                                  uint16_t addr, uint16_t value);

    /** @brief FC15: write @p qty coils from @p bits, LSB of byte 0 first.
     *
     *  Bits past @p qty in the last byte are zeroed on the way out rather than
     *  trusted from the caller: the spec requires padding, and a slave is
     *  entitled to reject a frame that pads with something else. */
    uint16_t build_write_coils(uint8_t* out, uint16_t cap, uint8_t unit,
                               uint16_t addr, uint16_t qty, const uint8_t* bits);

    /** @brief FC16: write @p qty holding registers from @p regs. */
    uint16_t build_write_registers(uint8_t* out, uint16_t cap, uint8_t unit,
                                   uint16_t addr, uint16_t qty,
                                   const uint16_t* regs);

    /** @}
     *  @name Responses
     *  @{ */

    /** @brief Check the @p len bytes at @p adu against the request @p e and, on
     *  success, point @p out at the payload inside them.
     *
     *  Checked in the order that yields the most useful diagnosis: length, then
     *  the checksum (nothing else in the frame can be trusted before it passes),
     *  then who answered, then what they answered about, then whether the shape
     *  matches what was asked.
     *
     *  @return ERR_OK, or ERR_EXCEPTION when the slave refused - which is a
     *  RESULT and not a parse failure. The frame was well formed and the device
     *  is alive; @p out carries the code. Everything else is a real failure. */
    Error parse_response(const uint8_t* adu, uint16_t len, const Expect& e,
                         Response* out);

    /** @}
     *  @name Register pairs
     *
     *  A 32-bit value spans two registers, and which of them holds the high word
     *  is a vendor quirk rather than a protocol rule - hence a RegOrder argument
     *  rather than a device table (see wire.h).
     *
     *  Through memcpy, so there is no aliasing violation and no unaligned load:
     *  a payload pointer is wherever the byte count left it, which is odd as
     *  often as not.
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

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#endif // MODBUS_CODEC_H
