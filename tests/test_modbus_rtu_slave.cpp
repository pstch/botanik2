/**
 * @file test_modbus_rtu_slave.cpp
 * @brief RtuSlave (modbus/rtu/slave.h): the device model, driven by a hostile
 *        wire.
 *
 * The client's tests ask whether a good frame is understood. These ask the
 * opposite: what a malformed, truncated, over-long or misaligned frame does to
 * a device that has to keep answering afterwards. That is where a field bus
 * spends its trouble, and it is the half that had no coverage at all - the
 * FC16 byte-count case below wrote 128 registers out of a stack buffer.
 *
 * No HAL, no port, no beat: bytes in through feed(), an answer out of poll().
 */
#include <cstdint>
#include <cstring>

#include "doctest.h"

#include "libbotanik/modbus/modbus.h"
#include "libbotanik/modbus/rtu/slave.h"

#if BOTANIK_FEATURE_MODBUS_RTU_SERVER

using namespace Botanik::Modbus;

namespace {

/** @brief Append the CRC, low byte first, and return the total length. */
uint16_t seal(uint8_t* f, uint16_t n) {
    const uint16_t c = crc16(f, n);
    f[n]     = lo8(c);
    f[n + 1] = hi8(c);
    return (uint16_t)(n + 2);
}

/** @brief A device at unit 1 with a recognisable image: register i holds
 *  0x1000 + i, coil i is on when i is even. */
void seed(Botanik::Modbus::RtuSlave& d) {
    for (uint16_t i = 0; i < 64; ++i) d.set_reg(i, (uint16_t)(0x1000 + i));
    for (uint16_t i = 0; i < 32; ++i) d.set_coil(i, (i % 2) == 0);
}

}  // namespace

TEST_CASE("modbus rtu slave: a well formed read is answered") {
    RtuSlave d(1);
    seed(d);

    uint8_t req[8];
    req[0] = 1; req[1] = FC_READ_HOLDING;
    req[2] = 0; req[3] = 0;      // address 0
    req[4] = 0; req[5] = 2;      // two registers
    const uint16_t n = seal(req, 6);

    d.feed(req, n);
    uint8_t out[256];
    const uint16_t r = d.poll(out, sizeof out);

    REQUIRE(r == 9);                  // unit, fc, count, 4 payload, crc
    CHECK(out[0] == 1);
    CHECK(out[1] == FC_READ_HOLDING);
    CHECK(out[2] == 4);
    CHECK(be16(out[3], out[4]) == 0x1000);
    CHECK(be16(out[5], out[6]) == 0x1001);
    CHECK(d.dropped() == 0);
}

TEST_CASE("modbus rtu slave: FC16 quantity 128 does not read past the buffer") {
    // The regression. 128 registers means an expected byte count of 256, which
    // truncates to 0 in a uint8_t and so used to MATCH a declared count of 0:
    // a nine-byte frame that committed 128 registers out of whatever followed
    // the request buffer on the stack.
    RtuSlave d(1);
    seed(d);

    uint8_t req[9];
    req[0] = 1; req[1] = FC_WRITE_REGISTERS;
    req[2] = 0; req[3] = 0;      // address 0
    req[4] = 0; req[5] = 0x80;   // quantity 128
    req[6] = 0;                  // byte count 0 - the lie
    const uint16_t n = seal(req, 7);

    d.feed(req, n);
    uint8_t out[256];
    const uint16_t r = d.poll(out, sizeof out);

    // Refused, not served, and the image is untouched.
    REQUIRE(r == 5);
    CHECK(out[1] == (FC_WRITE_REGISTERS | EX_BIT));
    CHECK(out[2] == EX_ILLEGAL_VALUE);
    for (uint16_t i = 0; i < 8; ++i) CHECK(d.reg(i) == (uint16_t)(0x1000 + i));
}

TEST_CASE("modbus rtu slave: a write of quantity zero is refused") {
    RtuSlave d(1);
    seed(d);

    uint8_t req[9];
    req[0] = 1; req[1] = FC_WRITE_REGISTERS;
    req[2] = 0; req[3] = 0;
    req[4] = 0; req[5] = 0;      // quantity 0
    req[6] = 0;
    const uint16_t n = seal(req, 7);

    d.feed(req, n);
    uint8_t out[256];
    const uint16_t r = d.poll(out, sizeof out);

    REQUIRE(r == 5);
    CHECK(out[1] == (FC_WRITE_REGISTERS | EX_BIT));
    CHECK(out[2] == EX_ILLEGAL_VALUE);
}

TEST_CASE("modbus rtu slave: a byte count that cannot fit does not wedge it") {
    // want = 9 + 255 = 264, past ADU_MAX. feed() resets the buffer at 256, so a
    // slave that waits for 264 bytes waits for ever - it goes deaf rather than
    // dropping one bad frame.
    RtuSlave d(1);
    seed(d);

    uint8_t req[9];
    req[0] = 1; req[1] = FC_WRITE_REGISTERS;
    req[2] = 0; req[3] = 0;
    req[4] = 0; req[5] = 0x7B;
    req[6] = 0xFF;               // byte count 255
    const uint16_t n = seal(req, 7);

    uint8_t out[256];
    d.feed(req, n);
    (void)d.poll(out, sizeof out);   // whatever it makes of it, it must not stick

    // A good frame right behind it is still answered.
    uint8_t good[8];
    good[0] = 1; good[1] = FC_READ_HOLDING;
    good[2] = 0; good[3] = 0;
    good[4] = 0; good[5] = 1;
    const uint16_t gn = seal(good, 6);

    bool answered = false;
    for (int pass = 0; pass < 300 && !answered; ++pass) {
        d.feed(good, gn);
        if (d.poll(out, sizeof out) > 0) answered = true;
    }
    CHECK(answered);
}

TEST_CASE("modbus rtu slave: one lost byte costs one frame, not the link") {
    // A CRC failure used to discard `want` bytes, which fixes the misalignment
    // in place: with a master sending 8-byte requests, a single dropped byte
    // left every following window failing its CRC for good.
    RtuSlave d(1);
    seed(d);

    uint8_t req[8];
    req[0] = 1; req[1] = FC_READ_HOLDING;
    req[2] = 0; req[3] = 0;
    req[4] = 0; req[5] = 1;
    const uint16_t n = seal(req, 6);

    uint8_t out[256];
    // A truncated frame: the tail of a request whose head was lost on the wire.
    d.feed(req + 1, (uint16_t)(n - 1));
    (void)d.poll(out, sizeof out);

    // The very next good frame must be served.
    bool answered = false;
    for (int pass = 0; pass < 300 && !answered; ++pass) {
        d.feed(req, n);
        if (d.poll(out, sizeof out) == 7) answered = true;
    }
    REQUIRE(answered);
    CHECK(out[1] == FC_READ_HOLDING);
    CHECK(be16(out[3], out[4]) == 0x1000);
}

TEST_CASE("modbus rtu slave: another unit's frame is not answered") {
    RtuSlave d(1);
    seed(d);

    uint8_t req[8];
    req[0] = 9;                  // not us
    req[1] = FC_READ_HOLDING;
    req[2] = 0; req[3] = 0;
    req[4] = 0; req[5] = 1;
    const uint16_t n = seal(req, 6);

    d.feed(req, n);
    uint8_t out[256];
    CHECK(d.poll(out, sizeof out) == 0);
    CHECK(d.dropped() == 1);
}

TEST_CASE("modbus rtu slave: an unsupported function is refused, not ignored") {
    RtuSlave d(1);
    seed(d);

    uint8_t req[8];
    req[0] = 1; req[1] = 0x08;   // diagnostics: not spoken here
    req[2] = 0; req[3] = 0;
    req[4] = 0; req[5] = 0;
    const uint16_t n = seal(req, 6);

    d.feed(req, n);
    uint8_t out[256];
    const uint16_t r = d.poll(out, sizeof out);

    REQUIRE(r == 5);
    CHECK(out[1] == (0x08 | EX_BIT));
    CHECK(out[2] == EX_ILLEGAL_FUNCTION);
}

TEST_CASE("modbus rtu slave: a read past the image is an address exception") {
    RtuSlave d(1);
    seed(d);

    uint8_t req[8];
    req[0] = 1; req[1] = FC_READ_HOLDING;
    req[2] = 0xFF; req[3] = 0xF0;   // well past MODBUS_SIM_REGS
    req[4] = 0x00; req[5] = 0x02;
    const uint16_t n = seal(req, 6);

    d.feed(req, n);
    uint8_t out[256];
    const uint16_t r = d.poll(out, sizeof out);

    REQUIRE(r == 5);
    CHECK(out[2] == EX_ILLEGAL_ADDRESS);
}

TEST_CASE("modbus rtu slave: a full-width register write round-trips") {
    RtuSlave d(1);
    seed(d);

    // FC16, four registers, honest byte count.
    uint8_t req[32];
    req[0] = 1; req[1] = FC_WRITE_REGISTERS;
    req[2] = 0; req[3] = 0x10;   // address 16
    req[4] = 0; req[5] = 4;
    req[6] = 8;
    for (uint16_t i = 0; i < 4; ++i) {
        req[7 + i * 2]     = hi8((uint16_t)(0xBEE0 + i));
        req[7 + i * 2 + 1] = lo8((uint16_t)(0xBEE0 + i));
    }
    const uint16_t n = seal(req, 15);

    d.feed(req, n);
    uint8_t out[256];
    const uint16_t r = d.poll(out, sizeof out);

    REQUIRE(r == 8);                            // the echo
    CHECK(be16(out[2], out[3]) == 16);
    CHECK(be16(out[4], out[5]) == 4);
    for (uint16_t i = 0; i < 4; ++i)
        CHECK(d.reg((uint16_t)(16 + i)) == (uint16_t)(0xBEE0 + i));
}


// The spec's odder corners
// ===========================================================================

TEST_CASE("modbus rtu slave: a broadcast write is executed and not answered") {
    // Unit 0 addresses every device on the segment. Answering it would put
    // several devices on the wire at once; ignoring it means a broadcast does
    // nothing at all, which is what used to happen.
    RtuSlave d(1);
    seed(d);

    uint8_t req[16];
    req[0] = 0;                  // broadcast
    req[1] = FC_WRITE_REGISTERS;
    req[2] = 0; req[3] = 0x20;   // address 32
    req[4] = 0; req[5] = 2;
    req[6] = 4;
    req[7] = 0xAB; req[8] = 0xCD;
    req[9] = 0x12; req[10] = 0x34;
    const uint16_t n = seal(req, 11);

    d.feed(req, n);
    uint8_t out[256];
    CHECK(d.poll(out, sizeof out) == 0);       // silence on the wire
    CHECK(d.reg(32) == 0xABCD);                // and the write happened
    CHECK(d.reg(33) == 0x1234);
}

TEST_CASE("modbus rtu slave: a broadcast read is ignored rather than answered") {
    RtuSlave d(1);
    seed(d);

    uint8_t req[8];
    req[0] = 0; req[1] = FC_READ_HOLDING;
    req[2] = 0; req[3] = 0;
    req[4] = 0; req[5] = 2;
    const uint16_t n = seal(req, 6);

    d.feed(req, n);
    uint8_t out[256];
    CHECK(d.poll(out, sizeof out) == 0);
}

TEST_CASE("modbus rtu slave: no room to answer is reported, not left silent") {
    // Silence makes the master wait out its whole response timeout for a fault
    // that is entirely ours, and then charge it to the device as a miss.
    RtuSlave d(1);
    seed(d);

    uint8_t req[8];
    req[0] = 1; req[1] = FC_READ_HOLDING;
    req[2] = 0; req[3] = 0;
    req[4] = 0; req[5] = 8;      // 21 bytes of answer
    const uint16_t n = seal(req, 6);

    d.feed(req, n);
    uint8_t out[8];              // ...into a buffer that cannot hold it
    const uint16_t r = d.poll(out, sizeof out);

    REQUIRE(r == 5);
    CHECK(out[1] == (FC_READ_HOLDING | EX_BIT));
    CHECK(out[2] == EX_SLAVE_FAILURE);
}

#endif // BOTANIK_FEATURE_MODBUS_RTU_SERVER
