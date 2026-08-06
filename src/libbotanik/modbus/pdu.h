#ifndef MODBUS_PDU_H
#define MODBUS_PDU_H

/**
 * @file modbus/pdu.h
 * @brief The Modbus wire format, and nothing else: MBAP framing, request
 *        decoding, response encoding, the exception vocabulary.
 *
 * No sockets, no variables, no clock. What arrives is a byte buffer and what
 * leaves is a byte buffer, so this is testable from a literal frame and shared
 * with the RTU client when it lands (its framing differs, its PDUs do not).
 *
 * Two kinds of bad input, and they get different answers on purpose:
 *
 *  - a **framing** error (a protocol id that is not 0, a declared length outside
 *    the legal range) means the peer is not speaking Modbus TCP. TCP is a stream,
 *    so there is nothing to resynchronise to: the caller drops the connection.
 *  - a **content** error (an unknown function, a count past the protocol's
 *    ceiling, a byte count that disagrees with the quantity) is one request that
 *    cannot be served. Framing is intact, so it gets an exception response and
 *    the connection lives on.
 *
 * That is why decode() reports four outcomes rather than a bool.
 */

#include <cstddef>                    // size_t - buffer capacities and frame lengths
#include <cstdint>                    // uint8_t/uint16_t - the wire is bytes and 16-bit words

#include "libbotanik_config.h"        // BOTANIK_FEATURE_MODBUS_* - the gate this header sits behind

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER || BOTANIK_FEATURE_MODBUS_RTU_CLIENT

namespace Botanik { namespace Modbus {

    /** @name Sizes
     *  @{ */

    /** @brief The MBAP header: transaction id, protocol id, length, unit id. */
    static const size_t MBAP_LEN = 7;
    /** @brief Largest PDU the protocol allows (function code + 252 data). */
    static const size_t PDU_MAX  = 253;
    /** @brief Largest complete TCP frame, header included. Every buffer in the
     *  server is sized from this. */
    static const size_t ADU_MAX  = MBAP_LEN + PDU_MAX;

    /** @} */

    /** @name Vocabulary
     *  @{ */

    /** @brief The function codes served. Values are the protocol's, not ours. */
    enum Function {
        FC_READ_COILS      = 0x01,   ///< bits, writable side
        FC_READ_DISCRETE   = 0x02,   ///< bits, read-only side
        FC_READ_HOLDING    = 0x03,   ///< registers, writable side
        FC_READ_INPUT      = 0x04,   ///< registers, read-only side
        FC_WRITE_COIL      = 0x05,   ///< one bit
        FC_WRITE_REGISTER  = 0x06,   ///< one register
        FC_WRITE_COILS     = 0x0F,   ///< a run of bits
        FC_WRITE_REGISTERS = 0x10    ///< a run of registers
    };

    /** @brief Exception codes. A response carries at most one, and the server's
     *  whole refusal vocabulary is these five. */
    enum Exception {
        EX_NONE             = 0x00,
        EX_ILLEGAL_FUNCTION = 0x01,  ///< a function code we do not serve
        EX_ILLEGAL_ADDRESS  = 0x02,  ///< nothing is mapped there, or not for writing
        EX_ILLEGAL_VALUE    = 0x03,  ///< the request is well-formed but unserveable
        EX_SERVER_FAILURE   = 0x04,  ///< we could not do it, and it is our fault
        EX_BUSY             = 0x06   ///< try the same request again shortly
    };

    /** @brief Protocol ceilings on a single request. None of them binds in
     *  practice (the beat does, see docs/design/modbus-tcp-server.md), but a
     *  request past one is malformed and must be refused rather than clamped. */
    static const uint16_t MAX_READ_BITS  = 2000;
    static const uint16_t MAX_READ_REGS  = 125;
    static const uint16_t MAX_WRITE_BITS = 1968;
    static const uint16_t MAX_WRITE_REGS = 123;

    /** @brief The two fields of the MBAP header a response has to echo. The
     *  protocol id is always 0 and the length is derived when encoding, so
     *  neither is worth carrying. */
    struct Mbap {
        uint16_t txn;    ///< transaction id, opaque to us
        uint8_t  unit;   ///< unit id; a single-device server echoes whatever came
    };

    /** @brief One decoded request. @c data points INTO the caller's buffer, so it
     *  is valid only until that buffer is refilled. */
    struct Request {
        Mbap           mbap;
        uint8_t        fc;
        uint16_t       addr;      ///< starting address
        uint16_t       count;     ///< quantity of bits / registers (reads and multi-writes)
        uint16_t       value;     ///< FC_WRITE_COIL / FC_WRITE_REGISTER payload
        const uint8_t* data;      ///< FC_WRITE_COILS / FC_WRITE_REGISTERS payload
        uint8_t        data_len;  ///< its byte count
        Exception      ex;        ///< why it was refused, when decode() says so
    };

    /** @brief What came out of the buffer. */
    enum Decode {
        DEC_INCOMPLETE = 0,  ///< a partial frame: read more bytes and try again
        DEC_INVALID,         ///< not Modbus TCP at all: drop the connection
        DEC_OK,              ///< a request to serve
        DEC_EXCEPTION        ///< well-formed, refused: answer with Request::ex
    };

    /** @} */

    /** @name Codec
     *  @{ */

    /**
     * @brief Decode the frame at the front of @p buf.
     *
     * @param len       bytes available (a stream, so possibly several frames)
     * @param out       filled on DEC_OK and DEC_EXCEPTION (mbap and fc on both,
     *                  so a refusal can still be addressed to the right peer)
     * @param consumed  bytes this frame occupied, so the caller can shift the
     *                  rest forward. Set on DEC_OK / DEC_EXCEPTION only.
     */
    Decode decode(const uint8_t* buf, size_t len, Request* out, size_t* consumed);

    /** @brief Frame an exception response: 9 bytes, the function code with its
     *  top bit set. Returns the length written, or 0 if @p cap is too small. */
    size_t encode_exception(uint8_t* buf, size_t cap, const Mbap& m,
                            uint8_t fc, Exception ex);

    /** @brief Frame a read response: byte count, then @p n bytes of @p payload.
     *  The payload is whatever the map produced - packed bits or big-endian
     *  registers - because framing it is this layer's job and filling it is not. */
    size_t encode_read(uint8_t* buf, size_t cap, const Mbap& m, uint8_t fc,
                       const uint8_t* payload, uint8_t n);

    /** @brief Frame a write response: the four echoed bytes every write function
     *  answers with (address plus value, or address plus quantity). */
    size_t encode_echo(uint8_t* buf, size_t cap, const Mbap& m, uint8_t fc,
                       uint16_t a, uint16_t b);

    /** @} */

    /** @name Wire helpers
     *
     *  Modbus is big-endian, everywhere, including inside a 32-bit value split
     *  over two registers. Single expressions, so they stay in the header.
     *  @{ */

    inline uint16_t rd16(const uint8_t* p) {
        return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
    }
    inline void wr16(uint8_t* p, uint16_t v) {
        p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v & 0xFF);
    }
    /** @brief Bytes a run of @p count bits occupies, padded to the byte. */
    inline uint16_t bit_bytes(uint16_t count) { return (uint16_t)((count + 7) / 8); }
    /** @brief Set bit @p i of a packed run. LSB first within each byte, which is
     *  the protocol's order and not the obvious one. */
    inline void put_bit(uint8_t* p, uint16_t i, bool v) {
        if (v) p[i / 8] = (uint8_t)(p[i / 8] |  (1u << (i % 8)));
        else   p[i / 8] = (uint8_t)(p[i / 8] & ~(1u << (i % 8)));
    }
    /** @brief Read bit @p i of a packed run. */
    inline bool get_bit(const uint8_t* p, uint16_t i) {
        return (p[i / 8] & (1u << (i % 8))) != 0;
    }

    /** @brief Whether @p fc writes. What the write gate and the read-only rows
     *  are checked against, so the answer lives in one place. */
    inline bool writes(uint8_t fc) {
        return fc == FC_WRITE_COIL || fc == FC_WRITE_REGISTER ||
               fc == FC_WRITE_COILS || fc == FC_WRITE_REGISTERS;
    }
    /** @brief Whether @p fc addresses bits rather than registers. */
    inline bool bit_access(uint8_t fc) {
        return fc == FC_READ_COILS || fc == FC_READ_DISCRETE ||
               fc == FC_WRITE_COIL || fc == FC_WRITE_COILS;
    }

    /** @} */

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER || BOTANIK_FEATURE_MODBUS_RTU_CLIENT

#endif // MODBUS_PDU_H
