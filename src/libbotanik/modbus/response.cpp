/**
 * @file response.cpp
 * @brief @see response.h
 */

#include "libbotanik/modbus/response.h"

namespace Botanik { namespace Modbus {

namespace {

    /** @brief The most payload bytes a read can legally answer with: a
     *  125-register read, the largest of the four.
     *
     *  Belt, not braces: what stops a bad size reaching @c out is the length
     *  equality below (@c out->len is assigned only where body_len is exactly
     *  2 + want). This refuses an impossible request earlier and with the right
     *  diagnosis, ERR_COUNT rather than a confusing ERR_SHORT. */
    const uint16_t PAYLOAD_MAX = 250;

}  // namespace

Error match_response(uint8_t unit, const uint8_t* body, uint16_t body_len,
                     const Expect& e, Response* out) {
    if (body == nullptr || out == nullptr) return ERR_CAPACITY;
    if (body_len < 1) return ERR_SHORT;

    out->unit = unit;
    if (unit != e.unit) return ERR_UNIT;

    const uint8_t raw_fc = body[0];
    out->fc = (uint8_t)(raw_fc & (uint8_t)~EX_BIT);
    if (out->fc != e.fc) return ERR_FC;

    if ((raw_fc & EX_BIT) != 0) {
        if (body_len != 2) return ERR_SHORT;
        out->ex = (Exception)body[1];
        return ERR_EXCEPTION;
    }

    if (is_read(e.fc)) {
        const uint16_t want = read_payload(e.fc, e.qty);
        // A request we could not have sent: refuse before the byte count is
        // compared against it, so a caller's bad Expect cannot size anything.
        if (want == 0 || want > PAYLOAD_MAX) return ERR_COUNT;
        if (body_len < 2) return ERR_SHORT;
        if (body[1] != (uint8_t)want) return ERR_COUNT;
        if (body_len != (uint16_t)(2u + want)) return ERR_SHORT;
        out->data = body + 2;
        out->len  = want;
        return ERR_OK;
    }

    // Every write echoes its own header back.
    if (!is_supported(e.fc)) return ERR_FC;
    if (body_len != 5) return ERR_SHORT;
    if (rd16(body + 1) != e.addr) return ERR_ECHO;

    // The multi-writes echo a count; the single writes echo the VALUE. Both are
    // checked here: a relay board that clamps, inverts or ignores what it was
    // told still answers with a well-formed frame.
    const uint16_t echoed = rd16(body + 3);
    if (e.fc == FC_WRITE_COILS || e.fc == FC_WRITE_REGISTERS) {
        if (echoed != e.qty) return ERR_ECHO;
    } else if (echoed != e.value) {
        return ERR_ECHO;
    }

    out->data = body + 1;
    out->len  = 4;
    return ERR_OK;
}

}}  // namespace Botanik::Modbus
