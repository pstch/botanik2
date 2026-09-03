/**
 * @file test_modbus_framing.cpp
 * @brief RtuFramer, which is where whatever is on the RS-485 wire enters the
 *        firmware.
 *
 * Every byte here is peer-controlled and the bus is shared, so the cases that
 * matter are the ones no healthy device produces: a frame longer than the
 * request allows, a late answer from the previous transaction, a second master
 * talking over ours. The framer has to be able to tell those apart, because the
 * counters it keeps are what the comms alarms are raised from.
 */

#include <vector>

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/modbus/rtu/codec.h"
#include "libbotanik/modbus/rtu/framing.h"

namespace modbus_framing {

using namespace Botanik;
using namespace Botanik::Modbus;

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

/** @brief Append the CRC the way codec.cpp does: low byte first. */
static void seal(std::vector<uint8_t>& f) {
    const uint16_t crc = crc16(f.data(), (uint16_t)f.size());
    f.push_back((uint8_t)(crc & 0xFF));
    f.push_back((uint8_t)(crc >> 8));
}

/** @brief A slave's answer to a register read: unit, fc, byte count, payload. */
static std::vector<uint8_t> read_reply(uint8_t unit, uint8_t fc,
                                       const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f;
    f.push_back(unit);
    f.push_back(fc);
    f.push_back((uint8_t)payload.size());
    f.insert(f.end(), payload.begin(), payload.end());
    seal(f);
    return f;
}

/** @brief A slave's refusal: the function code with the high bit set. */
static std::vector<uint8_t> exception(uint8_t unit, uint8_t fc, uint8_t code) {
    std::vector<uint8_t> f;
    f.push_back(unit);
    f.push_back((uint8_t)(fc | 0x80u));
    f.push_back(code);
    seal(f);
    return f;
}

/** @brief A single write's echo, which is the only confirmation of the value. */
static std::vector<uint8_t> write_echo(uint8_t unit, uint8_t fc,
                                       uint16_t addr, uint16_t value) {
    std::vector<uint8_t> f;
    f.push_back(unit);
    f.push_back(fc);
    f.push_back((uint8_t)(addr >> 8));   f.push_back((uint8_t)(addr & 0xFF));
    f.push_back((uint8_t)(value >> 8));  f.push_back((uint8_t)(value & 0xFF));
    seal(f);
    return f;
}

static Expect read_of(uint8_t unit, uint8_t fc, uint16_t addr, uint16_t qty) {
    Expect e; e.unit = unit; e.fc = fc; e.addr = addr; e.qty = qty; e.value = 0;
    return e;
}

/** @brief Feed a whole frame and poll once, at a time inside the timeout. */
static int offer(RtuFramer& fr, const std::vector<uint8_t>& f, uint64_t now = 10) {
    fr.feed(f.data(), (uint16_t)f.size());
    return fr.poll(now);
}

TEST_CASE("rtu framing: a clean read answer parses and counts one frame") {
    RtuFramer fr;
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0x006B, 2), 0, 1000);
    CHECK(fr.armed());

    REQUIRE(offer(fr, read_reply(0x11, FC_READ_HOLDING, {0x02, 0x2B, 0x00, 0x64})) == 1);
    CHECK(fr.err() == ERR_OK);
    CHECK(fr.frames() == 1);
    CHECK(fr.crc_errors() == 0);
    CHECK(fr.bad_frames() == 0);
    CHECK(fr.timeouts() == 0);

    const Response& r = fr.response();
    REQUIRE(r.data != nullptr);
    REQUIRE(r.len == 4);
    CHECK(r.data[0] == 0x02);
    CHECK(r.data[3] == 0x64);
}

TEST_CASE("rtu framing: a frame arriving one byte at a time parses the same") {
    // The normal case on a slow bus: a beat carries whatever the UART had.
    RtuFramer fr;
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 1), 0, 1000);
    const std::vector<uint8_t> f = read_reply(0x11, FC_READ_HOLDING, {0x12, 0x34});

    for (size_t i = 0; i + 1 < f.size(); i++) {
        CHECK(fr.feed(&f[i], 1) == 1);
        INFO("byte " << i << " of " << f.size());
        CHECK(fr.poll(10) == 0);          // not done until the last byte
    }
    CHECK(fr.feed(&f[f.size() - 1], 1) == 1);
    CHECK(fr.poll(10) == 1);
    CHECK(fr.err() == ERR_OK);
}

TEST_CASE("rtu framing: a corrupt frame is a CRC error, not a parse") {
    RtuFramer fr;
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 1), 0, 1000);
    std::vector<uint8_t> f = read_reply(0x11, FC_READ_HOLDING, {0x12, 0x34});
    f[3] ^= 0x01;                          // one bit, in the payload

    CHECK(offer(fr, f) == -1);
    CHECK(fr.err() == ERR_CRC);
    CHECK(fr.crc_errors() == 1);
    INFO("a CRC error is a cable fault, and must not be counted as a bad frame");
    CHECK(fr.bad_frames() == 0);
}

TEST_CASE("rtu framing: the wrong slave answering is not the answer") {
    RtuFramer fr;
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 1), 0, 1000);
    // Well-formed, correct CRC, wrong unit: two devices on one address, or a
    // second master's traffic. Distinct from a CRC error on purpose.
    CHECK(offer(fr, read_reply(0x12, FC_READ_HOLDING, {0x12, 0x34})) == -1);
    CHECK(fr.err() == ERR_UNIT);
    CHECK(fr.bad_frames() == 1);
    CHECK(fr.crc_errors() == 0);
}

TEST_CASE("rtu framing: an answer about another function is refused") {
    RtuFramer fr;
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 1), 0, 1000);
    CHECK(offer(fr, read_reply(0x11, FC_READ_INPUT, {0x12, 0x34})) == -1);
    CHECK(fr.err() == ERR_FC);
    CHECK(fr.bad_frames() == 1);
}

TEST_CASE("rtu framing: a byte count contradicting the request fails the frame") {
    RtuFramer fr;
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 2), 0, 1000);   // 2 regs => 4 bytes
    // The slave says 2. Caught while RESOLVING the length, before the frame is
    // complete, which is what keeps a lying byte count from sizing the read.
    CHECK(offer(fr, read_reply(0x11, FC_READ_HOLDING, {0x12, 0x34})) == -1);
    CHECK(fr.err() == ERR_COUNT);
    CHECK(fr.bad_frames() == 1);
}

TEST_CASE("rtu framing: a refusal is a completed frame, not a failure") {
    RtuFramer fr;
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 1), 0, 1000);

    // A slave that answers ILLEGAL DATA ADDRESS is healthy and talking. Counting
    // it as a comms fault would raise the bus alarm on a misconfigured register.
    REQUIRE(offer(fr, exception(0x11, FC_READ_HOLDING, 0x02)) == 1);
    CHECK(fr.err() == ERR_EXCEPTION);
    CHECK(fr.response().ex == 0x02);
    CHECK(fr.frames() == 1);
    CHECK(fr.bad_frames() == 0);
    CHECK(fr.crc_errors() == 0);
}

TEST_CASE("rtu framing: a single write is confirmed by its echo alone") {
    RtuFramer fr;
    Expect e = read_of(0x11, FC_WRITE_COIL, 0x00AC, 1);
    e.value = 0xFF00;
    fr.arm(e, 0, 1000);
    REQUIRE(offer(fr, write_echo(0x11, FC_WRITE_COIL, 0x00AC, 0xFF00)) == 1);
    CHECK(fr.err() == ERR_OK);

    // The same echo with the value changed: the slave wrote something else, and
    // for a single write nothing but the echo can say so.
    RtuFramer fr2;
    fr2.arm(e, 0, 1000);
    CHECK(offer(fr2, write_echo(0x11, FC_WRITE_COIL, 0x00AC, 0x0000)) == -1);
    CHECK(fr2.err() == ERR_ECHO);
}

TEST_CASE("rtu framing: nothing arriving is a timeout, at the deadline") {
    RtuFramer fr;
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 1), 1000, 500);

    CHECK(fr.poll(1000) == 0);
    CHECK(fr.poll(1499) == 0);
    INFO("absolute, so a caller that skipped beats does not accumulate error");
    CHECK(fr.poll(1500) == -1);
    CHECK(fr.err() == ERR_TIMEOUT);
    CHECK(fr.timeouts() == 1);
}

TEST_CASE("rtu framing: a half-arrived frame still times out") {
    RtuFramer fr;
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 2), 0, 500);
    const std::vector<uint8_t> f = read_reply(0x11, FC_READ_HOLDING,
                                              {0x01, 0x02, 0x03, 0x04});
    fr.feed(f.data(), 3);                  // header only: the device went quiet
    CHECK(fr.poll(10) == 0);
    CHECK(fr.len() == 3);                  // for the diagnostic that says how far
    CHECK(fr.poll(500) == -1);
    CHECK(fr.err() == ERR_TIMEOUT);
}

TEST_CASE("rtu framing: our own echo is discarded before the answer") {
    // RE is tied low, so the driver hears the frame it just drove. Those bytes
    // arrive first and are exactly as many as were written.
    RtuFramer fr;
    uint8_t req[8] = {};
    const uint16_t n = build_read(req, sizeof(req), 0x11, FC_READ_HOLDING, 0, 1);
    REQUIRE(n == 8);

    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 1), 0, 1000, n);
    fr.feed(req, n);                       // the echo
    CHECK(fr.poll(10) == 0);               // ...consumed, not parsed as an answer
    REQUIRE(offer(fr, read_reply(0x11, FC_READ_HOLDING, {0x12, 0x34})) == 1);
    CHECK(fr.err() == ERR_OK);
    CHECK(fr.stray_bytes() == 0);          // an echo is not a stray
}

TEST_CASE("rtu framing: bytes with nothing armed are strays, not a frame") {
    RtuFramer fr;
    const std::vector<uint8_t> f = read_reply(0x11, FC_READ_HOLDING, {0x12, 0x34});

    CHECK_FALSE(fr.armed());
    fr.feed(f.data(), (uint16_t)f.size());
    CHECK(fr.stray_bytes() == f.size());
    CHECK(fr.frames() == 0);
}

TEST_CASE("rtu framing: a late answer does not become the head of the next one") {
    // The failure this prevents: a slow slave answers after we gave up, and its
    // frame is read as the first bytes of the next transaction. Every read after
    // that is off by a frame, on a bus that looks electrically fine.
    RtuFramer fr;
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 1), 0, 100);
    const std::vector<uint8_t> late = read_reply(0x11, FC_READ_HOLDING, {0xDE, 0xAD});
    fr.feed(late.data(), 3);               // part of the old answer, still arriving
    REQUIRE(fr.poll(100) == -1);           // we time out

    fr.arm(read_of(0x12, FC_READ_HOLDING, 0, 1), 100, 1000);
    REQUIRE(offer(fr, read_reply(0x12, FC_READ_HOLDING, {0x12, 0x34}), 110) == 1);
    CHECK(fr.err() == ERR_OK);
    CHECK(fr.response().data[0] == 0x12);  // ours, not 0xDE
}

TEST_CASE("rtu framing: feed stops at the frame end and calls the rest stray") {
    RtuFramer fr;
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 1), 0, 1000);
    std::vector<uint8_t> two = read_reply(0x11, FC_READ_HOLDING, {0x12, 0x34});
    const uint16_t first = (uint16_t)two.size();
    // A second master's frame arriving right behind ours in one read.
    const std::vector<uint8_t> other = read_reply(0x11, FC_READ_HOLDING, {0x56, 0x78});
    two.insert(two.end(), other.begin(), other.end());

    const uint16_t took = fr.feed(two.data(), (uint16_t)two.size());
    INFO("took " << took << " of " << two.size() << ", frame is " << first);
    CHECK(took == first);                  // not the surplus
    CHECK(fr.poll(10) == 1);
    CHECK(fr.err() == ERR_OK);
    CHECK(fr.response().data[0] == 0x12);
}

TEST_CASE("rtu framing: disarm makes what follows countable rather than confusing") {
    RtuFramer fr;
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 1), 0, 1000);
    fr.disarm();
    CHECK_FALSE(fr.armed());

    const std::vector<uint8_t> f = read_reply(0x11, FC_READ_HOLDING, {0x12, 0x34});
    fr.feed(f.data(), (uint16_t)f.size());
    CHECK(fr.stray_bytes() == f.size());
    CHECK(fr.poll(10) != 1);
}

TEST_CASE("rtu framing: the counters point at different faults and reset together") {
    RtuFramer fr;

    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 1), 0, 1000);
    std::vector<uint8_t> bad = read_reply(0x11, FC_READ_HOLDING, {0x12, 0x34});
    bad[3] ^= 0x01;
    offer(fr, bad);
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 1), 0, 1000);
    offer(fr, read_reply(0x12, FC_READ_HOLDING, {0x12, 0x34}));
    fr.arm(read_of(0x11, FC_READ_HOLDING, 0, 1), 0, 100);
    fr.poll(100);

    CHECK(fr.crc_errors() == 1);
    CHECK(fr.bad_frames() == 1);
    CHECK(fr.timeouts() == 1);

    fr.reset_counters();
    CHECK(fr.crc_errors() == 0);
    CHECK(fr.bad_frames() == 0);
    CHECK(fr.timeouts() == 0);
    CHECK(fr.frames() == 0);
    CHECK(fr.stray_bytes() == 0);
}

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT

}  // namespace modbus_framing
