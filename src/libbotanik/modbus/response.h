#ifndef MODBUS_RESPONSE_H
#define MODBUS_RESPONSE_H

/**
 * @file response.h
 * @brief Is this answer the one we asked for? The half of that question neither
 *        transport owns.
 *
 * @section rsp_why Why this is not in codec.h or pdu.cpp
 *
 * RTU and TCP frame differently and agree completely about what is inside the
 * frame. Both were checking it, separately, in two spellings that looked
 * unrelated because one counts a whole ADU and the other counts an MBAP length
 * field. Written in BODY bytes - everything after the unit byte, with the
 * transport's own trailer already accounted for - they are the same function:
 *
 *              RTU (len)        body_len       TCP (mlen)      body_len
 *   exception  len == 5         5 - 3 = 2      mlen == 3       3 - 1 = 2
 *   read       len == 5 + want  2 + want       mlen == 3+want  2 + want
 *   write echo len == 8         8 - 3 = 5      mlen == 6       6 - 1 = 5
 *
 * and the payload and echo pointers land at body+2 and body+1 either way. So
 * the transports keep what is genuinely theirs - a CRC and a length field - and
 * hand the rest here, so a rule added to the answer check reaches both.
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
     * The transport has already decided the frame is intact; this decides
     * whether it is OURS.
     */
    Error match_response(uint8_t unit, const uint8_t* body, uint16_t body_len,
                         const Expect& e, Response* out);

}}  // namespace Botanik::Modbus

#endif // MODBUS_RESPONSE_H
