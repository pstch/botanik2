/**
 * @file test_modbus_framing.cpp
 * @brief RtuFramer (modbus/framing.h): finding where a response ends without
 *        being able to hear the silence that officially delimits it.
 *
 * The cases here are mostly about the ways a frame can fail to be the frame we
 * asked for, because that is where a field bus actually spends its trouble. The
 * happy path is two cases; the rest is a slave answering late, answering short,
 * answering about something else, or claiming a length it does not have.
 *
 * Every case drives the framer with a fake clock. It has no serial port, no HAL
 * and no beat, which is the whole point of the layer.
 */
#include <cstdint>
#include <cstring>

#include "doctest.h"

#include "libbotanik/modbus/modbus.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

using namespace Botanik::Modbus;

namespace {

// FC03 response, byte count 4: registers 10 and 20, unit 5.
const uint8_t RSP_FC03[] = { 0x05, 0x03, 0x04, 0x00, 0x0A, 0x00, 0x14, 0x9F, 0xFE };
// FC03 exception, illegal data address.
const uint8_t RSP_FC03_EXC[] = { 0x05, 0x83, 0x02, 0x81, 0x30 };
// FC16 echo, unit 34, address 0, quantity 2.
const uint8_t RSP_FC16[] = { 0x22, 0x10, 0x00, 0x00, 0x00, 0x02, 0x46, 0x9B };

const Expect E_FC03 = { 5, FC_READ_HOLDING, 0, 2, 0 };
const Expect E_FC16 = { 34, FC_WRITE_REGISTERS, 0, 2, 0 };

const uint32_t TIMEOUT_MS = 300;

/** @brief Seal a body with its CRC, low byte first, into @p out.
 *  @return the total frame length.
 *
 *  Uses the codec's own crc16, which is fine here: this file tests FRAMING.
 *  Whether that checksum is the right one is pinned in test_modbus_codec.cpp,
 *  against the spec's published example and the residue property. */
uint16_t seal(uint8_t* out, uint16_t body) {
    const uint16_t crc = crc16(out, body);
    out[body]     = lo8(crc);
    out[body + 1] = hi8(crc);
    return (uint16_t)(body + 2);
}

}  // namespace

// The happy path
// ===========================================================================

TEST_CASE("modbus framing: a complete response fed in one chunk completes") {
    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS);
    CHECK(f.armed());
    CHECK(f.poll(1000) == 0);

    CHECK(f.feed(RSP_FC03, sizeof RSP_FC03) == sizeof RSP_FC03);
    REQUIRE(f.poll(1001) == 1);
    CHECK(f.err() == ERR_OK);
    REQUIRE(f.response().len == 4);
    CHECK(reg_at(f.response().data, 0) == 10);
    CHECK(reg_at(f.response().data, 1) == 20);
    CHECK(f.frames() == 1);
}

TEST_CASE("modbus framing: the same response fed one byte at a time completes identically") {
    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS);

    for (uint16_t i = 0; i < sizeof RSP_FC03; ++i) {
        CAPTURE(i);
        // A byte per beat is the realistic case at 4800 baud, and the framer
        // must not care.
        CHECK(f.feed(RSP_FC03 + i, 1) == 1);
        const bool last = (i + 1 == sizeof RSP_FC03);
        CHECK(f.poll(1000 + i) == (last ? 1 : 0));
    }
    CHECK(f.err() == ERR_OK);
    CHECK(reg_at(f.response().data, 1) == 20);
}

TEST_CASE("modbus framing: the length comes from the byte count, not from silence") {
    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS);
    f.feed(RSP_FC03, sizeof RSP_FC03);

    // No gap has been observed and no time has passed - the clock has not moved
    // at all - and the frame is nonetheless complete. That is the whole design:
    // a 1.75 ms silence is unobservable on a 10 ms beat, so length plus CRC is
    // the delimiter and time is only ever a backstop.
    CHECK(f.poll(1000) == 1);
    CHECK(f.err() == ERR_OK);
}

TEST_CASE("modbus framing: an exception frame is five bytes and completes early") {
    RtuFramer f;
    // Asked for two registers, so a normal answer would be nine bytes. The
    // refusal is five, and a framer that sized from the request would sit
    // waiting for four bytes that are never coming.
    f.arm(E_FC03, 1000, TIMEOUT_MS);
    CHECK(f.feed(RSP_FC03_EXC, sizeof RSP_FC03_EXC) == sizeof RSP_FC03_EXC);

    REQUIRE(f.poll(1000) == 1);
    CHECK(f.err() == ERR_EXCEPTION);
    CHECK(f.response().ex == EX_ILLEGAL_ADDRESS);
    // It COMPLETED: the slave is alive and answering, which is what the driver
    // needs to know before it decides anything about backoff.
    CHECK(f.frames() == 1);
    CHECK(f.bad_frames() == 0);
}

TEST_CASE("modbus framing: an echo frame is eight bytes") {
    RtuFramer f;
    f.arm(E_FC16, 1000, TIMEOUT_MS);
    CHECK(f.feed(RSP_FC16, sizeof RSP_FC16) == sizeof RSP_FC16);
    REQUIRE(f.poll(1000) == 1);
    CHECK(f.err() == ERR_OK);
}

// The ways it goes wrong
// ===========================================================================

TEST_CASE("modbus framing: a byte count disagreeing with the request is a frame error") {
    RtuFramer f;
    // The frame is well formed, and its CRC is good - but we asked for ten
    // registers and it is answering with two.
    const Expect asked_ten = { 5, FC_READ_HOLDING, 0, 10, 0 };
    f.arm(asked_ten, 1000, TIMEOUT_MS);

    const uint16_t used = f.feed(RSP_FC03, sizeof RSP_FC03);
    CHECK(f.poll(1000) == -1);
    CHECK(f.err() == ERR_COUNT);
    // Rejected the moment the count was read, not after collecting a frame's
    // worth: only three bytes were taken.
    CHECK(used == 3);
    CHECK(f.bad_frames() == 1);
    CHECK(f.crc_errors() == 0);
}

TEST_CASE("modbus framing: a corrupted byte reads as a CRC error, not a short frame") {
    RtuFramer f;
    uint8_t bent[sizeof RSP_FC03];
    std::memcpy(bent, RSP_FC03, sizeof bent);
    bent[4] = (uint8_t)(bent[4] ^ 0x01u);   // inside the payload, so the length still fits

    f.arm(E_FC03, 1000, TIMEOUT_MS);
    f.feed(bent, sizeof bent);
    CHECK(f.poll(1000) == -1);
    CHECK(f.err() == ERR_CRC);
    // Counted apart from the rest, because it means something physical: a
    // cable, a termination resistor, or a baud mismatch.
    CHECK(f.crc_errors() == 1);
    CHECK(f.bad_frames() == 0);
}

TEST_CASE("modbus framing: a truncated response never completes and expires on its deadline") {
    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS);
    f.feed(RSP_FC03, 5);            // half a frame, then the slave falls over

    CHECK(f.poll(1100) == 0);       // still waiting, well inside the deadline
    CHECK(f.poll(1299) == 0);
    CHECK(f.poll(1300) == -1);      // deadline is absolute, and inclusive
    CHECK(f.err() == ERR_TIMEOUT);
    CHECK(f.timeouts() == 1);
    CHECK(f.len() == 5);            // how far it got, for the diagnostic
}

TEST_CASE("modbus framing: a slave that says nothing at all times out") {
    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS);
    CHECK(f.poll(1299) == 0);
    CHECK(f.poll(1300) == -1);
    CHECK(f.err() == ERR_TIMEOUT);
    CHECK(f.len() == 0);
}

TEST_CASE("modbus framing: bytes after a complete frame are counted, not consumed") {
    RtuFramer f;
    uint8_t stream[sizeof RSP_FC03 + 4];
    std::memcpy(stream, RSP_FC03, sizeof RSP_FC03);
    std::memset(stream + sizeof RSP_FC03, 0xAA, 4);

    f.arm(E_FC03, 1000, TIMEOUT_MS);
    // A caller that drained a whole ring in one read must keep its surplus:
    // swallowing it would hide a second master on the bus.
    CHECK(f.feed(stream, sizeof stream) == sizeof RSP_FC03);
    CHECK(f.poll(1000) == 1);
    CHECK(f.stray_bytes() == 4);
}

TEST_CASE("modbus framing: bytes arriving unarmed are strays") {
    RtuFramer f;
    CHECK(f.feed(RSP_FC03, sizeof RSP_FC03) == 0);
    CHECK(f.stray_bytes() == sizeof RSP_FC03);
    CHECK(f.poll(1000) == 0);       // idle is not a failure
}

TEST_CASE("modbus framing: arm() drops whatever the previous transaction left") {
    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS);
    f.feed(RSP_FC03, 4);            // a partial answer, still arriving

    // The next transaction starts clean. Treating those four bytes as this
    // frame's head is exactly how a bus desynchronises and stays that way.
    f.arm(E_FC03, 2000, TIMEOUT_MS);
    CHECK(f.len() == 0);
    CHECK(f.feed(RSP_FC03, sizeof RSP_FC03) == sizeof RSP_FC03);
    CHECK(f.poll(2000) == 1);
    CHECK(f.err() == ERR_OK);
}

TEST_CASE("modbus framing: disarm makes a late answer countable rather than confusing") {
    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS);
    CHECK(f.poll(1300) == -1);
    f.disarm();
    CHECK_FALSE(f.armed());

    // The slave finally answers, long after we gave up on it.
    CHECK(f.feed(RSP_FC03, sizeof RSP_FC03) == 0);
    CHECK(f.stray_bytes() == sizeof RSP_FC03);
}

// The buffer
// ===========================================================================

TEST_CASE("modbus framing: a 125-register response fills the ADU buffer exactly") {
    uint8_t big[ADU_MAX];
    big[0] = 5;
    big[1] = FC_READ_HOLDING;
    big[2] = 250;
    for (uint16_t i = 0; i < 250; ++i) big[3 + i] = (uint8_t)i;
    const uint16_t n = seal(big, 253);
    REQUIRE(n == 255);              // one under ADU_MAX, which is the whole margin

    RtuFramer f;
    const Expect e = { 5, FC_READ_HOLDING, 0, 125, 0 };
    f.arm(e, 1000, TIMEOUT_MS);
    CHECK(f.feed(big, n) == n);
    REQUIRE(f.poll(1000) == 1);
    CHECK(f.err() == ERR_OK);
    CHECK(f.response().len == 250);
    CHECK(reg_at(f.response().data, 124) == 0xF8F9);
}

TEST_CASE("modbus framing: a lying byte count writes nothing past the ADU buffer") {
    // The one input that could reach past the buffer: a slave claiming far more
    // than it was asked for. Refused when the count is read, rather than
    // clamped - a clamp would leave the frame misaligned and the next one
    // unparseable too.
    uint8_t evil[ADU_MAX * 2];
    evil[0] = 5;
    evil[1] = FC_READ_HOLDING;
    evil[2] = 0xFF;                 // 255 payload bytes, against the 4 we asked for
    std::memset(evil + 3, 0x5A, sizeof evil - 3);

    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS);
    const uint16_t used = f.feed(evil, (uint16_t)sizeof evil);

    CHECK(used == 3);
    CHECK(f.poll(1000) == -1);
    CHECK(f.err() == ERR_COUNT);
    CHECK(f.stray_bytes() == sizeof evil - 3);
}

TEST_CASE("modbus framing: counters survive a transaction and reset on demand") {
    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS);
    f.feed(RSP_FC03, sizeof RSP_FC03);
    REQUIRE(f.poll(1000) == 1);

    f.arm(E_FC03, 2000, TIMEOUT_MS);
    CHECK(f.poll(2300) == -1);

    // Counters are the raw material for the comms alarms a later stage raises,
    // so they accumulate across transactions rather than resetting with arm().
    CHECK(f.frames() == 1);
    CHECK(f.timeouts() == 1);
    f.reset_counters();
    CHECK(f.frames() == 0);
    CHECK(f.timeouts() == 0);
}


// Our own transmission coming back
// ===========================================================================
//
// RE is held asserted while we transmit, so the receiver hears the request
// (hal/platform.cpp). The framer is armed the moment the frame is queued, and
// has to be right whether or not the echo is real on a given board - which is
// why it holds a CANDIDATE and replays it rather than discarding byte by byte.

TEST_CASE("modbus framing: our own request is swallowed before the answer") {
    // Request as it goes out: read 2 holding registers from unit 5.
    uint8_t req[8] = { 0x05, 0x03, 0x00, 0x00, 0x00, 0x02 };
    const uint16_t rn = seal(req, 6);

    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS, req, rn);

    // The echo arrives first, then the answer.
    CHECK(f.feed(req, rn) == rn);
    CHECK(f.poll(1000) == 0);            // nothing framed yet: that was us
    CHECK(f.len() == 0);

    CHECK(f.feed(RSP_FC03, sizeof RSP_FC03) == sizeof RSP_FC03);
    REQUIRE(f.poll(1000) == 1);
    CHECK(f.err() == ERR_OK);
    CHECK(f.response().len == 4);
    CHECK(reg_at(f.response().data, 0) == 10);
    CHECK(reg_at(f.response().data, 1) == 20);
}

TEST_CASE("modbus framing: with no echo, the answer is not eaten") {
    // The regression this design exists for. A response opens with the same unit
    // and function code the request did, so a running prefix match would swallow
    // those two bytes and frame from the byte count onwards.
    uint8_t req[8] = { 0x05, 0x03, 0x00, 0x00, 0x00, 0x02 };
    const uint16_t rn = seal(req, 6);

    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS, req, rn);

    CHECK(f.feed(RSP_FC03, sizeof RSP_FC03) == sizeof RSP_FC03);
    REQUIRE(f.poll(1000) == 1);
    CHECK(f.err() == ERR_OK);
    CHECK(f.response().len == 4);
    CHECK(reg_at(f.response().data, 0) == 10);
    CHECK(reg_at(f.response().data, 1) == 20);
}

TEST_CASE("modbus framing: an echo split across beats is still swallowed") {
    uint8_t req[8] = { 0x05, 0x03, 0x00, 0x00, 0x00, 0x02 };
    const uint16_t rn = seal(req, 6);

    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS, req, rn);

    for (uint16_t i = 0; i < rn; ++i) CHECK(f.feed(req + i, 1) == 1);
    CHECK(f.len() == 0);
    for (uint16_t i = 0; i < sizeof RSP_FC03; ++i) f.feed(RSP_FC03 + i, 1);

    REQUIRE(f.poll(1000) == 1);
    CHECK(f.err() == ERR_OK);
}

TEST_CASE("modbus framing: a write echo is recovered when the board does not echo") {
    // A write response is byte-identical to the request it answers, so a whole
    // candidate with nothing behind it is ambiguous. At the deadline the framer
    // replays it rather than calling it silence.
    uint8_t req[8] = { 0x22, 0x10, 0x00, 0x00, 0x00, 0x02 };
    const uint16_t rn = seal(req, 6);
    REQUIRE(rn == sizeof RSP_FC16);

    RtuFramer f;
    f.arm(E_FC16, 1000, TIMEOUT_MS, req, rn);
    CHECK(f.feed(RSP_FC16, sizeof RSP_FC16) == sizeof RSP_FC16);

    CHECK(f.poll(1000) == 0);                    // held as a candidate
    REQUIRE(f.poll(1000 + TIMEOUT_MS) == 1);     // deadline: it was the answer
    CHECK(f.err() == ERR_OK);
    CHECK(f.timeouts() == 0);
}

TEST_CASE("modbus framing: a genuine silence still times out with an echo armed") {
    uint8_t req[8] = { 0x05, 0x03, 0x00, 0x00, 0x00, 0x02 };
    const uint16_t rn = seal(req, 6);

    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS, req, rn);
    f.feed(req, rn);                             // only our own transmission

    CHECK(f.poll(1000) == 0);
    REQUIRE(f.poll(1000 + TIMEOUT_MS) == -1);
    CHECK(f.err() == ERR_TIMEOUT);
    CHECK(f.timeouts() == 1);
}

TEST_CASE("modbus framing: an echo followed by a refusal reads as the refusal") {
    uint8_t req[8] = { 0x05, 0x03, 0x00, 0x00, 0x00, 0x02 };
    const uint16_t rn = seal(req, 6);

    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS, req, rn);
    f.feed(req, rn);
    f.feed(RSP_FC03_EXC, sizeof RSP_FC03_EXC);

    REQUIRE(f.poll(1000) == 1);
    CHECK(f.err() == ERR_EXCEPTION);
    CHECK(f.response().ex == EX_ILLEGAL_ADDRESS);
}

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
