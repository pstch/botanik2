/**
 * @file modbus/pdu.cpp
 * @brief The codec declared in pdu.h: framing in, framing out.
 *
 * The MBAP length field is authoritative for where a frame ends - it is checked
 * before anything else is read, and every subsequent bound comes from it rather
 * than from how many bytes happen to be in the buffer. A PDU that disagrees with
 * its own function code is then a CONTENT error, answerable with an exception,
 * because framing was never in doubt.
 */
#include "libbotanik/modbus/pdu.h"    // the vocabulary and the three encoders implemented here

#include <cstring>                    // memcpy - copying a read payload into the response

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER

namespace Botanik { namespace Modbus {

    namespace {

        /** @brief Shortest legal MBAP length field: the unit id plus a function
         *  code. The longest is that plus a full PDU. */
        const uint16_t LEN_MIN = 2;
        const uint16_t LEN_MAX = (uint16_t)(1 + PDU_MAX);

        /** @brief Refuse @p r with @p ex, keeping the address fields already
         *  read: a refusal is still addressed to the peer that sent it. */
        Decode refuse(Request* r, Exception ex) { r->ex = ex; return DEC_EXCEPTION; }

        /** @brief Write the MBAP header for a response whose PDU is @p pdu_len
         *  bytes. The length field counts the unit id too, which is the one
         *  off-by-one this protocol is famous for. */
        void put_mbap(uint8_t* buf, const Mbap& m, size_t pdu_len) {
            wr16(buf + 0, m.txn);
            wr16(buf + 2, 0);                                  // protocol id
            wr16(buf + 4, (uint16_t)(pdu_len + 1));
            buf[6] = m.unit;
        }

    }  // namespace

    Decode decode(const uint8_t* buf, size_t len, Request* out, size_t* consumed) {
        if (buf == 0 || out == 0 || consumed == 0) return DEC_INVALID;
        if (len < MBAP_LEN) return DEC_INCOMPLETE;

        if (rd16(buf + 2) != 0) return DEC_INVALID;            // not Modbus TCP
        const uint16_t mlen = rd16(buf + 4);
        if (mlen < LEN_MIN || mlen > LEN_MAX) return DEC_INVALID;

        // The length field counts the unit id, and the unit id is the LAST byte
        // of the header - so the frame is (header - unit) + length, not
        // header + length. Same off-by-one put_mbap() is careful about.
        const size_t total = (MBAP_LEN - 1) + mlen;
        if (len < total) return DEC_INCOMPLETE;

        // Framing is settled from here on: the frame is exactly `total` bytes
        // whatever the PDU turns out to say, so every refusal below is
        // recoverable and the connection survives it.
        *consumed = total;
        out->mbap.txn  = rd16(buf + 0);
        out->mbap.unit = buf[6];
        out->fc        = buf[7];
        out->addr      = 0;
        out->count     = 0;
        out->value     = 0;
        out->data      = 0;
        out->data_len  = 0;
        out->ex        = EX_NONE;

        const uint8_t* pdu = buf + MBAP_LEN;
        const uint16_t pdu_len = (uint16_t)(mlen - 1);          // minus the unit id

        switch (out->fc) {
            case FC_READ_COILS:
            case FC_READ_DISCRETE:
            case FC_READ_HOLDING:
            case FC_READ_INPUT: {
                if (pdu_len != 5) return refuse(out, EX_ILLEGAL_VALUE);
                out->addr  = rd16(pdu + 1);
                out->count = rd16(pdu + 3);
                if (out->count < 1 || out->count > max_quantity(out->fc))
                    return refuse(out, EX_ILLEGAL_VALUE);
                return DEC_OK;
            }
            case FC_WRITE_COIL: {
                if (pdu_len != 5) return refuse(out, EX_ILLEGAL_VALUE);
                out->addr  = rd16(pdu + 1);
                out->value = rd16(pdu + 3);
                // The protocol spells a coil 0xFF00 / 0x0000 and nothing else;
                // anything in between is a master with a bug, not a 1.
                if (out->value != 0xFF00 && out->value != 0x0000)
                    return refuse(out, EX_ILLEGAL_VALUE);
                out->count = 1;
                return DEC_OK;
            }
            case FC_WRITE_REGISTER: {
                if (pdu_len != 5) return refuse(out, EX_ILLEGAL_VALUE);
                out->addr  = rd16(pdu + 1);
                out->value = rd16(pdu + 3);
                out->count = 1;
                return DEC_OK;
            }
            case FC_WRITE_COILS:
            case FC_WRITE_REGISTERS: {
                if (pdu_len < 6) return refuse(out, EX_ILLEGAL_VALUE);
                out->addr  = rd16(pdu + 1);
                out->count = rd16(pdu + 3);
                const uint8_t bc = pdu[5];
                const bool bits = is_bit(out->fc);
                if (out->count < 1 || out->count > max_quantity(out->fc))
                    return refuse(out, EX_ILLEGAL_VALUE);
                // The byte count is redundant with the quantity, which is exactly
                // why it is checked: the two disagreeing means the request was
                // built wrong, and guessing which one to believe is how a master
                // ends up writing a register it did not name.
                const uint16_t want = bits ? bit_bytes(out->count)
                                           : (uint16_t)(out->count * 2);
                if (bc != want) return refuse(out, EX_ILLEGAL_VALUE);
                if (pdu_len != (uint16_t)(6 + bc)) return refuse(out, EX_ILLEGAL_VALUE);
                out->data     = pdu + 6;
                out->data_len = bc;
                return DEC_OK;
            }
            default:
                return refuse(out, EX_ILLEGAL_FUNCTION);
        }
    }

    size_t encode_exception(uint8_t* buf, size_t cap, const Mbap& m,
                            uint8_t fc, Exception ex) {
        const size_t total = MBAP_LEN + 2;
        if (buf == 0 || cap < total) return 0;
        put_mbap(buf, m, 2);
        buf[7] = (uint8_t)(fc | 0x80);
        buf[8] = (uint8_t)ex;
        return total;
    }

    size_t encode_read(uint8_t* buf, size_t cap, const Mbap& m, uint8_t fc,
                       const uint8_t* payload, uint8_t n) {
        const size_t total = MBAP_LEN + 2 + n;
        if (buf == 0 || (payload == 0 && n > 0) || cap < total) return 0;
        put_mbap(buf, m, (size_t)(2 + n));
        buf[7] = fc;
        buf[8] = n;
        if (n > 0) memcpy(buf + 9, payload, n);
        return total;
    }

    size_t encode_echo(uint8_t* buf, size_t cap, const Mbap& m, uint8_t fc,
                       uint16_t a, uint16_t b) {
        const size_t total = MBAP_LEN + 5;
        if (buf == 0 || cap < total) return 0;
        put_mbap(buf, m, 5);
        buf[7] = fc;
        wr16(buf + 8, a);
        wr16(buf + 10, b);
        return total;
    }

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER
