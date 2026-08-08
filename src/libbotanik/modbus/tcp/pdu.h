#ifndef MODBUS_TCP_PDU_H
#define MODBUS_TCP_PDU_H

/**
 * @file modbus/tcp/pdu.h
 * @brief What Modbus TCP adds to a PDU: the MBAP header, request decoding and
 *        response encoding.
 *
 * The vocabulary itself - function codes, exceptions, the quantity ceilings, the
 * byte order - is @ref modbus/wire.h, shared with the RTU client. The two transports
 * differ in their FRAMING and in nothing else, and this file is that difference.
 *
 * No sockets, no variables, no clock. What arrives is a byte buffer and what
 * leaves is a byte buffer, so this is testable from a literal frame.
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

#include "libbotanik/config/base.h"   // BOTANIK_FEATURE_MODBUS_* - the gate this header sits behind

#include "libbotanik/modbus/wire.h"   // Fc / Exception / the frame arithmetic both transports share

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER || BOTANIK_FEATURE_MODBUS_TCP_CLIENT

namespace Botanik { namespace Modbus {

    /** @name Sizes
     *  @{ */

    /** @brief The MBAP header: transaction id, protocol id, length, unit id. */
    static const size_t MBAP_LEN = 7;
    /** @brief Largest PDU the protocol allows (function code + 252 data). */
    static const size_t PDU_MAX  = 253;
    /** @brief Largest complete TCP frame, header included. Every buffer in the
     *  server is sized from this. Distinct from wire.h's ADU_MAX, which is the
     *  RTU frame's 256: same protocol, different envelope. */
    static const size_t TCP_ADU_MAX = MBAP_LEN + PDU_MAX;

    /** @} */

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

    /** @}
     *  @name The master's half
     *
     *  A slave frames requests IN and answers OUT; a master does the mirror
     *  image. Both live here because both are MBAP, and MBAP is the only thing
     *  that differs from the RTU client - which is why what comes back is
     *  wire.h's Response and wire.h's Error, the same types that half parses
     *  into.
     *  @{ */

    /** @brief Frame a read request (FC01-04): @p qty items from @p addr. */
    size_t encode_read_request(uint8_t* buf, size_t cap, const Mbap& m,
                               uint8_t fc, uint16_t addr, uint16_t qty);

    /** @brief Frame an FC16 write of @p qty registers from @p data (which is
     *  already big-endian register bytes, as the map produces them). */
    size_t encode_write_request(uint8_t* buf, size_t cap, const Mbap& m,
                                uint16_t addr, uint16_t qty,
                                const uint8_t* data);

    /**
     * @brief Check a response against the request that earned it.
     *
     * Checked in the order that yields the most useful diagnosis, the same order
     * the RTU parser uses (codec.h): the frame's own shape, then who answered,
     * then what they answered about, then whether it matches what was asked. The
     * only step missing is the checksum, because TCP already carried one.
     *
     * @param txn       the transaction id that was sent; a reply carrying
     *                  another one is an answer to a question we have stopped
     *                  waiting for, not an answer to this one
     * @param consumed  set on any terminal result, so the caller can shift a
     *                  stale frame out of its buffer and keep reading
     * @return ERR_OK, ERR_EXCEPTION when the slave refused (a RESULT: the frame
     *         was well formed and the device is alive), ERR_SHORT when the frame
     *         is not all there yet, or the specific mismatch.
     */
    Error decode_response(const uint8_t* buf, size_t len, const Expect& e,
                          uint16_t txn, Response* out, size_t* consumed);

    /** @} */

    /** @name MBAP helpers
     *
     *  The header's own two-byte fields. Everything else about byte order is
     *  wire.h's, and is the same on both transports.
     *  @{ */

    // rd16 / wr16 are wire.h's - the byte order is the protocol's, not this
    // transport's, and it was forked here before that was said out loud.

    /** @} */

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER || BOTANIK_FEATURE_MODBUS_TCP_CLIENT

#endif // MODBUS_TCP_PDU_H
