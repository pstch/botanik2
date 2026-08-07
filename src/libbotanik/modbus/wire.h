#ifndef MODBUS_WIRE_H
#define MODBUS_WIRE_H

/**
 * @file wire.h
 * @brief The Modbus vocabulary shared by both transports and both ends:
 *        function codes, exceptions, parse errors, and the frame arithmetic
 *        that follows from them.
 *
 * FUNCTIONs in the sense plc/functions uses the word: no state, single-expression
 * `constexpr` bodies, header-only. What needs a loop or a buffer is in codec.h;
 * what needs to remember where it is in a frame is in framing.h. Same rule, same
 * reason.
 *
 * @section wire_gate Why this header carries no feature gate
 *
 * All four corners include it - the two clients, the two servers - so any gate
 * here has to name all four flags, and one that names fewer compiles a corner
 * against an empty vocabulary. That is not hypothetical: it named two, and
 * MODBUS_RTU_SERVER and MODBUS_TCP_CLIENT could not build alone. Since the whole
 * file is `constexpr` and costs an unused includer nothing, the gate buys no
 * space and only offers a way to get the list wrong.
 *
 * Nothing in this directory knows the variable model exists. That is deliberate
 * and load-bearing: it is what lets the simulator's slave bank answer requests
 * with the very same code the master builds them with, so the codec is exercised
 * from both ends rather than only against its own expectations.
 */

#include <cstdint>

namespace Botanik { namespace Modbus {

    /** @name Vocabulary
     *  @{ */

    /** @brief The function codes this firmware speaks.
     *
     *  Four reads and four writes, which is the whole of what a field bus of
     *  meters and relay boards needs. The diagnostic and file-transfer codes are
     *  deliberately absent: an unsupported code must fail to compile into a
     *  request rather than go out and confuse a slave. */
    enum Fc : uint8_t {
        FC_READ_COILS      = 1,    ///< bits, writable side
        FC_READ_DISCRETE   = 2,    ///< bits, read only
        FC_READ_HOLDING    = 3,    ///< registers, writable side
        FC_READ_INPUT      = 4,    ///< registers, read only
        FC_WRITE_COIL      = 5,    ///< one bit
        FC_WRITE_REGISTER  = 6,    ///< one register
        FC_WRITE_COILS     = 15,   ///< many bits
        FC_WRITE_REGISTERS = 16    ///< many registers
    };

    /** @brief Set in the response's function-code byte when the slave is
     *  refusing rather than answering. */
    static const uint8_t EX_BIT = 0x80;

    /** @brief Why a slave refused.
     *
     *  A slave that answers with one of these is HEALTHY: it heard us, it
     *  understood us, and it declined. That is a different fact from silence or
     *  from noise on the cable, which is why it is not an @ref Error. */
    enum Exception : uint8_t {
        EX_NONE             = 0,
        EX_ILLEGAL_FUNCTION = 1,
        EX_ILLEGAL_ADDRESS  = 2,
        EX_ILLEGAL_VALUE    = 3,
        EX_SLAVE_FAILURE    = 4,
        EX_ACK              = 5,
        EX_SLAVE_BUSY       = 6,
        EX_NAK              = 7,
        EX_MEMORY_PARITY    = 8,
        EX_GATEWAY_PATH     = 10,
        EX_GATEWAY_TARGET   = 11
    };

    /** @brief What went wrong with a frame.
     *
     *  One value per distinct diagnosis, because the driver counts them apart
     *  and they point at different things: CRC noise is a cable or a
     *  termination resistor, a wrong unit is a second master or a mis-set slave
     *  address, a timeout is an unplugged device, and an exception is a working
     *  device saying no. Collapsing these into "failed" is what makes a field
     *  bus undebuggable. */
    enum Error : int8_t {
        ERR_OK = 0,
        ERR_SHORT,       ///< fewer bytes than the frame's own shape allows
        ERR_CRC,         ///< the checksum does not match
        ERR_UNIT,        ///< a slave we did not ask answered
        ERR_FC,          ///< answered about a function we did not ask
        ERR_COUNT,       ///< the byte count disagrees with the request
        ERR_ECHO,        ///< a write echo naming another address or quantity
        ERR_EXCEPTION,   ///< the slave refused; see Response::ex
        ERR_TIMEOUT,     ///< nothing arrived before the deadline
        ERR_CAPACITY     ///< the caller's buffer is too small: a bug, not a wire event
    };

    /** @brief Which half of a 32-bit register pair carries the high word.
     *
     *  Registers themselves are big-endian on the wire, always, with no vendor
     *  freedom. Only the PAIRING is a quirk, so it is one argument rather than a
     *  device-profile table. */
    enum RegOrder : uint8_t { REG_HI_FIRST = 0, REG_LO_FIRST = 1 };

    /** @brief What was asked, kept so the answer can be checked against it.
     *
     *  The pivot of this whole layer. An RTU frame carries no length field and
     *  no terminator, so only someone who remembers the request can say where
     *  the response ends. That is what makes framing length-driven instead of
     *  silence-driven, and it is what lets a write echo be CHECKED rather than
     *  merely accepted. */
    struct Expect {
        uint8_t  unit;
        uint8_t  fc;
        uint16_t addr;
        uint16_t qty;
    };

    /** @brief A response that parsed.
     *
     *  @c data points into the caller's own frame buffer: nothing here owns or
     *  copies memory, and the pointer is only good until that buffer is reused. */
    struct Response {
        const uint8_t* data;   ///< payload after the byte count; null on an exception
        uint16_t       len;    ///< payload bytes
        uint8_t        unit;
        uint8_t        fc;     ///< the function asked, exception bit already stripped
        Exception      ex;     ///< EX_NONE unless the slave refused
    };

    /** @brief The shortest thing that can be checksummed at all: unit, function
     *  code, and the two CRC bytes. */
    static const uint16_t ADU_MIN = 4;

    /** @brief The longest frame the protocol allows, and so the size of any
     *  buffer one is accumulated in.
     *
     *  Not a tunable: the spec fixes it at 256, and the largest response that
     *  fits is exactly 255 bytes (a 125-register read). It lives here beside the
     *  arithmetic that depends on it rather than in libbotanik_config.h, which is
     *  for what an INSTALLATION varies. How many registers a poll group may
     *  coalesce IS such a choice, but that is the planner's constant, not this
     *  one. */
    static const uint16_t ADU_MAX = 256;

    /** @}
     *  @name Frame arithmetic
     *
     *  The sizes a builder, a parser and a framer all have to agree on, written
     *  once. Single expressions in the shape Vars::type_width() uses, so a
     *  disagreement between the three is not expressible.
     *  @{ */

    /** @brief Whether @p fc addresses bits rather than 16-bit registers. */
    constexpr bool is_bit(uint8_t fc) {
        return fc == FC_READ_COILS || fc == FC_READ_DISCRETE ||
               fc == FC_WRITE_COIL || fc == FC_WRITE_COILS;
    }

    /** @brief Whether @p fc reads rather than writes. */
    constexpr bool is_read(uint8_t fc) {
        return fc == FC_READ_COILS || fc == FC_READ_DISCRETE ||
               fc == FC_READ_HOLDING || fc == FC_READ_INPUT;
    }

    /** @brief Whether this firmware speaks @p fc at all. Asked before anything
     *  else derives a size from it, so an unknown code yields 0 lengths rather
     *  than a plausible-looking wrong one. */
    constexpr bool is_supported(uint8_t fc) {
        return is_read(fc) || fc == FC_WRITE_COIL || fc == FC_WRITE_REGISTER ||
               fc == FC_WRITE_COILS || fc == FC_WRITE_REGISTERS;
    }

    /** @brief The protocol ceiling on items in one transaction; 0 for a code we
     *  do not speak.
     *
     *  Nowhere near binding at this site: the planner runs out of milliseconds
     *  long before it runs out of registers (see docs/design/modbus-budget.md).
     *  Enforced anyway, because a slave's answer to an over-long request is
     *  undefined and the failure would look like a bad cable. */
    constexpr uint16_t max_quantity(uint8_t fc) {
        return (fc == FC_READ_COILS || fc == FC_READ_DISCRETE)   ? 2000
             : (fc == FC_READ_HOLDING || fc == FC_READ_INPUT)    ? 125
             : (fc == FC_WRITE_COILS)                            ? 1968
             : (fc == FC_WRITE_REGISTERS)                        ? 123
             : (fc == FC_WRITE_COIL || fc == FC_WRITE_REGISTER)  ? 1
                                                                 : 0;
    }

    /** @brief Bytes needed to pack @p bits, LSB of the first byte first. */
    constexpr uint16_t bit_bytes(uint16_t bits) {
        return (uint16_t)((bits + 7u) / 8u);
    }

    /** @brief Payload bytes a read of @p qty items answers with, which is what
     *  the slave's byte-count byte must equal. 0 for anything but a read. */
    constexpr uint16_t read_payload(uint8_t fc, uint16_t qty) {
        return !is_read(fc) ? 0
             : is_bit(fc)   ? bit_bytes(qty)
                            : (uint16_t)(qty * 2u);
    }

    /** @brief Total request bytes: unit, function code, body, CRC. */
    constexpr uint16_t request_len(uint8_t fc, uint16_t qty) {
        return (is_read(fc) || fc == FC_WRITE_COIL || fc == FC_WRITE_REGISTER) ? 8
             : (fc == FC_WRITE_COILS)     ? (uint16_t)(9u + bit_bytes(qty))
             : (fc == FC_WRITE_REGISTERS) ? (uint16_t)(9u + qty * 2u)
                                          : 0;
    }

    /** @brief Total bytes of a normal response. Every write echoes its own
     *  header, so all four are the same eight bytes. */
    constexpr uint16_t response_len(uint8_t fc, uint16_t qty) {
        return is_read(fc)      ? (uint16_t)(5u + read_payload(fc, qty))
             : is_supported(fc) ? 8
                                : 0;
    }

    /** @brief Total bytes of an exception response: unit, code with its top bit
     *  set, the exception, CRC. Shorter than any normal response, which is why
     *  the framer must branch on the exception bit before sizing anything. */
    constexpr uint16_t exception_len() { return 5; }

    /** @}
     *  @name Byte order
     *
     *  Registers are big-endian on the wire, unconditionally. Only 32-bit
     *  PAIRING varies by vendor, and that lives in codec.h with the RegOrder it
     *  needs.
     *  @{ */

    constexpr uint8_t hi8(uint16_t v) { return (uint8_t)(v >> 8); }
    constexpr uint8_t lo8(uint16_t v) { return (uint8_t)(v & 0xFFu); }

    constexpr uint16_t be16(uint8_t hi, uint8_t lo) {
        return (uint16_t)(((uint16_t)hi << 8) | (uint16_t)lo);
    }

    /** @brief Register @p i of a read response's payload. */
    constexpr uint16_t reg_at(const uint8_t* payload, uint16_t i) {
        return be16(payload[i * 2u], payload[i * 2u + 1u]);
    }

    /** @brief Bit @p i of a coil response's payload, LSB of byte 0 first. */
    constexpr bool bit_at(const uint8_t* payload, uint16_t i) {
        return ((payload[i / 8u] >> (i % 8u)) & 1u) != 0;
    }

    /** @brief The other direction: set bit @p i of a payload being BUILT. A
     *  slave packs its answer the way a client unpacks one, and the two have to
     *  agree about which end of a byte bit 0 sits at - so they say it once, in
     *  the same place. Not constexpr: it writes. */
    inline void put_bit(uint8_t* payload, uint16_t i, bool v) {
        const uint8_t mask = (uint8_t)(1u << (i % 8u));
        if (v) payload[i / 8u] = (uint8_t)(payload[i / 8u] | mask);
        else   payload[i / 8u] = (uint8_t)(payload[i / 8u] & ~mask);
    }

    /** @} */

}}  // namespace Botanik::Modbus

#endif // MODBUS_WIRE_H
