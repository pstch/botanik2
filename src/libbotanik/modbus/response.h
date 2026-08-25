#ifndef MODBUS_RESPONSE_H
#define MODBUS_RESPONSE_H

/**
 * @file response.h
 * @brief Is this answer the one we asked for? The half of that question neither
 *        transport owns.
 *
 * RTU and TCP frame differently and agree completely about what is inside the
 * frame. Written in BODY bytes - everything after the unit byte, with the
 * transport's own trailer already accounted for - the two checks are the same
 * function:
 *
 *              RTU (len)        body_len       TCP (mlen)      body_len
 *   exception  len == 5         5 - 3 = 2      mlen == 3       3 - 1 = 2
 *   read       len == 5 + want  2 + want       mlen == 3+want  2 + want
 *   write echo len == 8         8 - 3 = 5      mlen == 6       6 - 1 = 5
 *
 * and the payload and echo pointers land at body+2 and body+1 either way. The
 * transports keep what is theirs (a CRC, a length field) and hand the rest
 * here, so a rule added to the answer check reaches both.
 *
 * @see wire.h for the vocabulary; the transports for the framing.
 */

#include <cstdint>

#include "libbotanik/modbus/wire.h"

namespace Botanik { namespace Modbus {

    /**
     * @brief Check a response body against the request it should answer.
     *
     * @param unit     who answered, already read out of the transport's header
     * @param body     the function-code byte onward
     * @param body_len bytes from @p body to the end of the PDU, trailer excluded
     * @param e        what was asked
     * @param out      filled on every terminal result, so a refusal still says
     *                 which unit and function it refused
     *
     * @return ERR_OK, ERR_EXCEPTION when the slave refused - which is a RESULT,
     *         not a parse failure: the frame was well formed and the device is
     *         alive - or the specific mismatch.
     *
     * Checked in the order that yields the most useful diagnosis: who answered,
     * what they answered about, then whether the shape matches what was asked.
     * The transport decided the frame is intact; this decides whether it is OURS.
     */
    Error match_response(uint8_t unit, const uint8_t* body, uint16_t body_len,
                         const Expect& e, Response* out);

    /**
     * @brief Check a multi-write request's own consistency: FC15 and FC16.
     *
     * The quantity against what the function code allows, then the declared
     * byte count against that quantity. Both transports ask this, and it is
     * where a slave stops trusting a master's arithmetic.
     *
     * @param bc  the byte count the frame declares (pdu[5])
     * @param qty the quantity the frame declares
     * @return EX_NONE, or EX_ILLEGAL_VALUE naming which of the two is wrong
     *
     * @note The byte count is REDUNDANT with the quantity, and checked because
     * of it: guessing which to believe when they disagree is how a master ends
     * up writing a register it did not name. Computed in 32 bits - 2*qty wraps
     * to 0 at qty 128 in a uint8_t, which would then match a declared 0 and let
     * a short frame commit 128 registers from past the end of the buffer.
     */
    inline Exception check_write_count(uint8_t fc, uint8_t bc, uint16_t qty) {
        if (qty < 1 || qty > max_quantity(fc)) return EX_ILLEGAL_VALUE;
        const uint32_t want = is_bit(fc) ? (uint32_t)bit_bytes(qty)
                                         : (uint32_t)qty * 2u;
        if ((uint32_t)bc != want) return EX_ILLEGAL_VALUE;
        return EX_NONE;
    }

}}  // namespace Botanik::Modbus

#endif // MODBUS_RESPONSE_H
