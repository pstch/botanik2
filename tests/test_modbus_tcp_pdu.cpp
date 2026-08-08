/**
 * @file test_modbus_tcp_pdu.cpp
 * @brief MBAP framing (modbus/tcp/pdu.h), driven by a hostile stream.
 *
 * This is the remote-facing surface: anything that reaches decode() came off a
 * socket some peer on the plant network opened. So the cases here are mostly
 * about what a frame can claim about itself and be wrong - a length field that
 * does not match the bytes behind it, a byte count that disagrees with the
 * quantity, a protocol id that is not Modbus at all - plus the ordinary facts
 * of a stream: a frame arriving in pieces, and two frames arriving at once.
 */
#include <cstdint>
#include <cstring>

#include "doctest.h"

#include "libbotanik/modbus/modbus.h"
#include "libbotanik/modbus/tcp/pdu.h"

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER

using namespace Botanik::Modbus;

namespace {

/** @brief Build an MBAP frame: header, then @p body. @return total length. */
size_t frame(uint8_t* out, uint16_t txn, uint8_t unit,
             const uint8_t* body, uint16_t body_len) {
    out[0] = (uint8_t)(txn >> 8); out[1] = (uint8_t)(txn & 0xFF);
    out[2] = 0; out[3] = 0;                       // protocol id: Modbus
    const uint16_t mlen = (uint16_t)(body_len + 1);   // + the unit byte
    out[4] = (uint8_t)(mlen >> 8); out[5] = (uint8_t)(mlen & 0xFF);
    out[6] = unit;
    memcpy(out + 7, body, body_len);
    return (size_t)(7 + body_len);
}

/** @brief An FC03 read of @p qty registers at @p addr. */
size_t read_frame(uint8_t* out, uint16_t txn, uint8_t unit,
                  uint16_t addr, uint16_t qty) {
    const uint8_t body[5] = { FC_READ_HOLDING,
                              (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
                              (uint8_t)(qty >> 8),  (uint8_t)(qty & 0xFF) };
    return frame(out, txn, unit, body, 5);
}

}  // namespace

TEST_CASE("modbus tcp pdu: a well formed read decodes and reports its length") {
    uint8_t buf[64];
    const size_t n = read_frame(buf, 0x1234, 7, 100, 2);

    Request r;
    size_t used = 0;
    REQUIRE(decode(buf, n, &r, &used) == DEC_OK);
    CHECK(used == n);
    CHECK(r.mbap.txn == 0x1234);
    CHECK(r.mbap.unit == 7);
    CHECK(r.fc == FC_READ_HOLDING);
    CHECK(r.addr == 100);
    CHECK(r.count == 2);
}

TEST_CASE("modbus tcp pdu: a frame that is not Modbus is dropped, not parsed") {
    // Protocol id is zero for Modbus and nothing else. A non-zero one means the
    // stream is something else entirely, so there is no resynchronising: the
    // connection goes.
    uint8_t buf[64];
    const size_t n = read_frame(buf, 1, 1, 0, 2);
    buf[2] = 0x00; buf[3] = 0x01;                 // protocol id 1

    Request r;
    size_t used = 0;
    CHECK(decode(buf, n, &r, &used) == DEC_INVALID);
}

TEST_CASE("modbus tcp pdu: an impossible length field is refused") {
    uint8_t buf[64];
    const size_t n = read_frame(buf, 1, 1, 0, 2);

    Request r;
    size_t used = 0;

    // Zero and one are below the unit-plus-function-code minimum.
    for (uint16_t mlen = 0; mlen <= 1; ++mlen) {
        buf[4] = (uint8_t)(mlen >> 8); buf[5] = (uint8_t)(mlen & 0xFF);
        INFO("mlen " << mlen);
        CHECK(decode(buf, n, &r, &used) == DEC_INVALID);
    }

    // And one that cannot fit a Modbus PDU at all. A peer claiming 0xFFFF is
    // not a peer we can stay in step with.
    buf[4] = 0xFF; buf[5] = 0xFF;
    CHECK(decode(buf, n, &r, &used) == DEC_INVALID);
}

TEST_CASE("modbus tcp pdu: a frame still arriving is incomplete, not invalid") {
    // The difference matters: INCOMPLETE means read more, INVALID drops the
    // connection. Getting it wrong either way is a hang or a dropped SCADA.
    uint8_t buf[64];
    const size_t n = read_frame(buf, 1, 1, 0, 2);

    Request r;
    size_t used = 0;
    for (size_t k = 0; k < n; ++k) {
        INFO(k << " of " << n << " bytes");
        CHECK(decode(buf, k, &r, &used) == DEC_INCOMPLETE);
    }
    CHECK(decode(buf, n, &r, &used) == DEC_OK);
}

TEST_CASE("modbus tcp pdu: two frames in one read are taken one at a time") {
    // TCP is a stream: a peer that pipelines, or a segment that happens to carry
    // two requests, must not lose the second.
    uint8_t buf[128];
    const size_t a = read_frame(buf, 0x0001, 1, 0, 2);
    const size_t b = read_frame(buf + a, 0x0002, 1, 10, 4);

    Request r;
    size_t used = 0;
    REQUIRE(decode(buf, a + b, &r, &used) == DEC_OK);
    CHECK(used == a);
    CHECK(r.mbap.txn == 0x0001);

    // Shift and decode the rest, the way the slave's rx buffer does.
    REQUIRE(decode(buf + used, a + b - used, &r, &used) == DEC_OK);
    CHECK(used == b);
    CHECK(r.mbap.txn == 0x0002);
    CHECK(r.addr == 10);
}

TEST_CASE("modbus tcp pdu: a quantity past its function code's ceiling is refused") {
    // Refused as an EXCEPTION rather than dropped: the frame is well formed and
    // addressed to us, so the peer deserves an answer saying why.
    uint8_t buf[64];
    const size_t n = read_frame(buf, 1, 1, 0, (uint16_t)(max_quantity(FC_READ_HOLDING) + 1));

    Request r;
    size_t used = 0;
    const Decode d = decode(buf, n, &r, &used);
    INFO("decode returned " << (int)d);
    CHECK(d == DEC_EXCEPTION);
    CHECK(r.ex == EX_ILLEGAL_VALUE);
}

TEST_CASE("modbus tcp pdu: a zero quantity is refused") {
    uint8_t buf[64];
    const size_t n = read_frame(buf, 1, 1, 0, 0);

    Request r;
    size_t used = 0;
    CHECK(decode(buf, n, &r, &used) == DEC_EXCEPTION);
    CHECK(r.ex == EX_ILLEGAL_VALUE);
}

TEST_CASE("modbus tcp pdu: an FC16 byte count that disagrees with the quantity is refused") {
    // The TCP twin of the FC16 case that let the RTU slave read past its buffer.
    uint8_t body[16];
    body[0] = FC_WRITE_REGISTERS;
    body[1] = 0; body[2] = 0;        // address 0
    body[3] = 0; body[4] = 2;        // two registers...
    body[5] = 2;                     // ...but a byte count for one
    body[6] = 0xAA; body[7] = 0xBB;

    uint8_t buf[64];
    const size_t n = frame(buf, 1, 1, body, 8);

    Request r;
    size_t used = 0;
    const Decode d = decode(buf, n, &r, &used);
    INFO("decode returned " << (int)d);
    CHECK((d == DEC_EXCEPTION || d == DEC_INVALID));
}

TEST_CASE("modbus tcp pdu: an unsupported function code is refused with its own code") {
    uint8_t body[5] = { 0x08, 0, 0, 0, 0 };       // diagnostics
    uint8_t buf[64];
    const size_t n = frame(buf, 0x4321, 3, body, 5);

    Request r;
    size_t used = 0;
    REQUIRE(decode(buf, n, &r, &used) == DEC_EXCEPTION);
    CHECK(r.ex == EX_ILLEGAL_FUNCTION);
    // Addressed back to the right peer and transaction, which is why decode
    // fills the mbap on a refusal too.
    CHECK(r.mbap.txn == 0x4321);
    CHECK(r.mbap.unit == 3);
}

// Responses, from the master's side
// ===========================================================================

TEST_CASE("modbus tcp pdu: a response for another transaction is not our answer") {
    // The whole point of the transaction id. A late reply arriving while the
    // next request is in flight must not be matched to it.
    uint8_t rsp[64];
    const uint8_t body[4] = { FC_READ_HOLDING, 2, 0x12, 0x34 };
    const size_t n = frame(rsp, 0x0007, 1, body, 4);

    const Expect e = { 1, FC_READ_HOLDING, 0, 1, 0 };
    Response out;
    size_t used = 0;

    // The one we asked for parses...
    CHECK(decode_response(rsp, n, e, 0x0007, &out, &used) == ERR_OK);
    // ...and the same bytes against a different transaction do not.
    CHECK(decode_response(rsp, n, e, 0x0008, &out, &used) != ERR_OK);
}

TEST_CASE("modbus tcp pdu: a response from another unit is refused") {
    uint8_t rsp[64];
    const uint8_t body[4] = { FC_READ_HOLDING, 2, 0x12, 0x34 };
    const size_t n = frame(rsp, 1, 9, body, 4);     // unit 9

    const Expect e = { 1, FC_READ_HOLDING, 0, 1, 0 };   // we asked unit 1
    Response out;
    size_t used = 0;
    CHECK(decode_response(rsp, n, e, 1, &out, &used) == ERR_UNIT);
}

TEST_CASE("modbus tcp pdu: a truncated response is short, not wrong") {
    uint8_t rsp[64];
    const uint8_t body[4] = { FC_READ_HOLDING, 2, 0x12, 0x34 };
    const size_t n = frame(rsp, 1, 1, body, 4);

    const Expect e = { 1, FC_READ_HOLDING, 0, 1, 0 };
    Response out;
    size_t used = 0;
    for (size_t k = 0; k < n; ++k) {
        INFO(k << " of " << n << " bytes");
        CHECK(decode_response(rsp, k, e, 1, &out, &used) == ERR_SHORT);
    }
    CHECK(decode_response(rsp, n, e, 1, &out, &used) == ERR_OK);
}

TEST_CASE("modbus tcp pdu: an exception response is a result, not a parse failure") {
    uint8_t rsp[64];
    const uint8_t body[2] = { (uint8_t)(FC_READ_HOLDING | EX_BIT), EX_ILLEGAL_ADDRESS };
    const size_t n = frame(rsp, 1, 1, body, 2);

    const Expect e = { 1, FC_READ_HOLDING, 0, 1, 0 };
    Response out;
    size_t used = 0;
    REQUIRE(decode_response(rsp, n, e, 1, &out, &used) == ERR_EXCEPTION);
    CHECK(out.ex == EX_ILLEGAL_ADDRESS);
    CHECK(used == n);          // consumed, so the stream stays in step
}

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER
