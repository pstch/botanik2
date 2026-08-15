/**
 * @file test_modbus_rtu.cpp
 * @brief Modbus RTU: the wire codec, the frame/echo layer, and the slave device model.
 *
 * Each section below is the former test_<name>.cpp, kept in its own
 * namespace so their fixtures and helpers cannot collide.
 */

#include <cstdint>
#include <cstring>
#include "doctest.h"
#include "libbotanik/modbus/modbus.h"
#include "libbotanik/modbus/rtu/slave.h"

// modbus_codec

namespace modbus_codec {

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

using namespace Botanik::Modbus;

namespace {

// The spec's own worked example, and the only remembered constant here.
const uint8_t SPEC_BODY[] = { 0x02, 0x07 };
const uint16_t SPEC_CRC   = 0x1241;

// FC03 read 10 holding registers at 0, unit 5.
const uint8_t REQ_FC03[] = { 0x05, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC4, 0x49 };
// FC03 response, byte count 4: registers 10 and 20.
const uint8_t RSP_FC03[] = { 0x05, 0x03, 0x04, 0x00, 0x0A, 0x00, 0x14, 0x9F, 0xFE };
// FC03 exception, illegal data address.
const uint8_t RSP_FC03_EXC[] = { 0x05, 0x83, 0x02, 0x81, 0x30 };
// FC01 read 8 coils at 0, unit 31.
const uint8_t REQ_FC01[] = { 0x1F, 0x01, 0x00, 0x00, 0x00, 0x08, 0x3E, 0x72 };
// FC01 response, one byte: coils 0, 2, 5 and 7 set.
const uint8_t RSP_FC01[] = { 0x1F, 0x01, 0x01, 0xA5, 0x97, 0xDB };
// FC05 force coil 3 on, unit 31. A write echoes itself, so this is both frames.
const uint8_t REQ_FC05[] = { 0x1F, 0x05, 0x00, 0x03, 0xFF, 0x00, 0x7F, 0x84 };
// FC06 write 0x1234 to register 12, unit 6. Likewise its own echo.
const uint8_t REQ_FC06[] = { 0x06, 0x06, 0x00, 0x0C, 0x12, 0x34, 0x45, 0x09 };
// FC15 write 10 coils at 0, unit 33, and the echo that answers it.
const uint8_t REQ_FC15[] = { 0x21, 0x0F, 0x00, 0x00, 0x00, 0x0A, 0x02, 0xCD, 0x01,
                             0xE9, 0xA9 };
const uint8_t RSP_FC15[] = { 0x21, 0x0F, 0x00, 0x00, 0x00, 0x0A, 0xD2, 0xAC };
// FC16 write registers 1 and 2 at 0, unit 34, and its echo.
const uint8_t REQ_FC16[] = { 0x22, 0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x01,
                             0x00, 0x02, 0x87, 0x2A };
const uint8_t RSP_FC16[] = { 0x22, 0x10, 0x00, 0x00, 0x00, 0x02, 0x46, 0x9B };

/** @brief Compare a built frame against a frozen one, byte for byte. */
void check_frame(const uint8_t* got, uint16_t got_len,
                 const uint8_t* want, uint16_t want_len) {
    REQUIRE(got_len == want_len);
    for (uint16_t i = 0; i < want_len; ++i) {
        CAPTURE(i);
        CHECK((unsigned)got[i] == (unsigned)want[i]);
    }
}

}  // namespace

// The checksum

TEST_CASE("modbus crc: the spec's 02 07 example") {
    CHECK(crc16(SPEC_BODY, 2) == SPEC_CRC);
    // And on the wire, low byte first: 41 12, not 12 41.
    CHECK(lo8(SPEC_CRC) == 0x41);
    CHECK(hi8(SPEC_CRC) == 0x12);
}

TEST_CASE("modbus crc: any single-bit flip changes it") {
    const uint16_t good = crc16(REQ_FC16, sizeof REQ_FC16 - 2);
    for (uint16_t byte = 0; byte < sizeof REQ_FC16 - 2; ++byte) {
        for (uint8_t bit = 0; bit < 8; ++bit) {
            uint8_t bent[sizeof REQ_FC16];
            std::memcpy(bent, REQ_FC16, sizeof bent);
            bent[byte] = (uint8_t)(bent[byte] ^ (uint8_t)(1u << bit));
            CAPTURE(byte);
            CAPTURE(bit);
            CHECK(crc16(bent, sizeof REQ_FC16 - 2) != good);
        }
    }
}

// The frame arithmetic

TEST_CASE("modbus wire: quantity ceilings and frame lengths are per function code") {
    CHECK(max_quantity(FC_READ_COILS) == 2000);
    CHECK(max_quantity(FC_READ_HOLDING) == 125);
    CHECK(max_quantity(FC_WRITE_COILS) == 1968);
    CHECK(max_quantity(FC_WRITE_REGISTERS) == 123);
    CHECK(max_quantity(FC_WRITE_COIL) == 1);
    // An unspoken code sizes to nothing, not something plausible.
    CHECK(max_quantity(0x2B) == 0);
    CHECK(request_len(0x2B, 1) == 0);
    CHECK(response_len(0x2B, 1) == 0);

    CHECK(request_len(FC_READ_HOLDING, 10) == 8);
    CHECK(request_len(FC_WRITE_COIL, 1) == 8);
    CHECK(request_len(FC_WRITE_COILS, 10) == 11);     // 9 + 2 packed bytes
    CHECK(request_len(FC_WRITE_REGISTERS, 2) == 13);  // 9 + 4

    CHECK(response_len(FC_READ_HOLDING, 2) == 9);     // 5 + 4
    CHECK(response_len(FC_READ_COILS, 8) == 6);       // 5 + 1
    CHECK(response_len(FC_WRITE_REGISTERS, 2) == 8);  // every write echoes
    CHECK(exception_len() == 5);

    // The two extremes the buffer is sized for both land on 255, one byte under ADU_MAX.
    CHECK(response_len(FC_READ_HOLDING, 125) == 255);
    CHECK(request_len(FC_WRITE_REGISTERS, 123) == 255);

    CHECK(bit_bytes(0) == 0);
    CHECK(bit_bytes(1) == 1);
    CHECK(bit_bytes(8) == 1);
    CHECK(bit_bytes(9) == 2);

    static_assert(request_len(FC_READ_HOLDING, 10) == 8, "wire.h is constexpr");
}

TEST_CASE("modbus wire: bit and read classification does not leak to unknown codes") {
    CHECK(is_read(FC_READ_INPUT));
    CHECK_FALSE(is_read(FC_WRITE_REGISTERS));
    CHECK(is_bit(FC_READ_COILS));
    CHECK_FALSE(is_bit(FC_READ_HOLDING));
    CHECK(is_supported(FC_WRITE_COILS));
    CHECK_FALSE(is_supported(0x08));   // diagnostics: deliberately not spoken
}

// Building requests

TEST_CASE("modbus codec: an FC03 request is eight bytes, big-endian, CRC low byte first") {
    uint8_t out[16];
    const uint16_t n = build_read(out, sizeof out, 5, FC_READ_HOLDING, 0, 10);
    check_frame(out, n, REQ_FC03, sizeof REQ_FC03);
}

TEST_CASE("modbus codec: FC05 writes FF00 for on and 0000 for off") {
    uint8_t out[16];
    const uint16_t n = build_write_coil(out, sizeof out, 31, 3, true);
    check_frame(out, n, REQ_FC05, sizeof REQ_FC05);

    build_write_coil(out, sizeof out, 31, 3, false);
    CHECK(out[4] == 0x00);
    CHECK(out[5] == 0x00);
}

TEST_CASE("modbus codec: FC15 packs bits LSB-first and zeroes the tail of the last byte") {
    uint8_t out[32];
    const uint8_t bits[] = { 0xCD, 0x01 };
    const uint16_t n = build_write_coils(out, sizeof out, 33, 0, 10, bits);
    check_frame(out, n, REQ_FC15, sizeof REQ_FC15);

    // Unused bits of the last byte must read zero per spec; rubbish gets masked off.
    const uint8_t dirty[] = { 0xCD, 0xFD };
    const uint16_t m = build_write_coils(out, sizeof out, 33, 0, 10, dirty);
    check_frame(out, m, REQ_FC15, sizeof REQ_FC15);
}

TEST_CASE("modbus codec: FC16 carries a byte count of 2N") {
    uint8_t out[32];
    // Big-endian, as collect_write() hands the builder.
    const uint8_t regs[] = { 0x00, 0x01, 0x00, 0x02 };
    const uint16_t n = build_write_registers(out, sizeof out, 34, 0, 2, regs);
    check_frame(out, n, REQ_FC16, sizeof REQ_FC16);
    CHECK(out[6] == 4);
}

TEST_CASE("modbus codec: a builder refuses rather than truncating or overrunning") {
    uint8_t out[8];
    const uint8_t regs[] = { 0x00, 0x01, 0x00, 0x02 };

    // Too small a buffer: nothing written, 0 returned - half a frame on a shared bus would corrupt the next transaction.
    CHECK(build_read(out, 7, 5, FC_READ_HOLDING, 0, 10) == 0);
    CHECK(build_write_registers(out, sizeof out, 34, 0, 2, regs) == 0);

    // Past the protocol ceiling.
    CHECK(build_read(out, sizeof out, 5, FC_READ_HOLDING, 0, 126) == 0);
    CHECK(build_read(out, sizeof out, 5, FC_READ_COILS, 0, 2001) == 0);
    CHECK(build_write_registers(out, sizeof out, 34, 0, 124, regs) == 0);

    // Degenerate and unspoken.
    CHECK(build_read(out, sizeof out, 5, FC_READ_HOLDING, 0, 0) == 0);
    CHECK(build_read(out, sizeof out, 5, FC_WRITE_REGISTER, 0, 1) == 0);
    CHECK(build_read(nullptr, sizeof out, 5, FC_READ_HOLDING, 0, 1) == 0);
}

// Parsing responses

TEST_CASE("modbus codec: a read response yields its registers big-endian") {
    const Expect e = { 5, FC_READ_HOLDING, 0, 2, 0 };
    Response r;
    REQUIRE(parse_response(RSP_FC03, sizeof RSP_FC03, e, &r) == ERR_OK);
    CHECK(r.unit == 5);
    CHECK(r.fc == FC_READ_HOLDING);
    CHECK(r.ex == EX_NONE);
    REQUIRE(r.len == 4);
    CHECK(reg_at(r.data, 0) == 10);
    CHECK(reg_at(r.data, 1) == 20);
}

TEST_CASE("modbus codec: a coil response yields its bits LSB-first") {
    const Expect e = { 31, FC_READ_COILS, 0, 8, 0 };
    Response r;
    REQUIRE(parse_response(RSP_FC01, sizeof RSP_FC01, e, &r) == ERR_OK);
    REQUIRE(r.len == 1);
    // 0xA5 is 1010 0101, so counting from the LSB: 0, 2, 5 and 7.
    CHECK(bit_at(r.data, 0));
    CHECK_FALSE(bit_at(r.data, 1));
    CHECK(bit_at(r.data, 2));
    CHECK_FALSE(bit_at(r.data, 3));
    CHECK_FALSE(bit_at(r.data, 4));
    CHECK(bit_at(r.data, 5));
    CHECK_FALSE(bit_at(r.data, 6));
    CHECK(bit_at(r.data, 7));
}

TEST_CASE("modbus codec: an exception response is flagged with its code, not parsed as data") {
    const Expect e = { 5, FC_READ_HOLDING, 0, 10, 0 };
    Response r;
    // A RESULT, not a parse failure: well formed and alive, so a driver can tell refusing from dead.
    CHECK(parse_response(RSP_FC03_EXC, sizeof RSP_FC03_EXC, e, &r) == ERR_EXCEPTION);
    CHECK(r.ex == EX_ILLEGAL_ADDRESS);
    CHECK(r.fc == FC_READ_HOLDING);   // the exception bit is stripped
    CHECK(r.data == nullptr);
    CHECK(r.len == 0);
}

TEST_CASE("modbus codec: a bad CRC, a wrong unit and a wrong function code are distinct errors") {
    Response r;
    uint8_t bent[sizeof RSP_FC03];
    std::memcpy(bent, RSP_FC03, sizeof bent);
    bent[4] = (uint8_t)(bent[4] ^ 0x01u);

    const Expect good = { 5, FC_READ_HOLDING, 0, 2, 0 };
    CHECK(parse_response(bent, sizeof bent, good, &r) == ERR_CRC);

    const Expect other_unit = { 6, FC_READ_HOLDING, 0, 2, 0 };
    CHECK(parse_response(RSP_FC03, sizeof RSP_FC03, other_unit, &r) == ERR_UNIT);

    const Expect other_fc = { 5, FC_READ_INPUT, 0, 2, 0 };
    CHECK(parse_response(RSP_FC03, sizeof RSP_FC03, other_fc, &r) == ERR_FC);

    // The byte count is right for two registers but we asked for ten.
    const Expect too_many = { 5, FC_READ_HOLDING, 0, 10, 0 };
    CHECK(parse_response(RSP_FC03, sizeof RSP_FC03, too_many, &r) == ERR_COUNT);

    CHECK(parse_response(RSP_FC03, 3, good, &r) == ERR_SHORT);
    CHECK(parse_response(nullptr, sizeof RSP_FC03, good, &r) == ERR_CAPACITY);
}

TEST_CASE("modbus codec: an echo response is checked against the request address and quantity") {
    Response r;

    const Expect fc16 = { 34, FC_WRITE_REGISTERS, 0, 2, 0 };
    CHECK(parse_response(RSP_FC16, sizeof RSP_FC16, fc16, &r) == ERR_OK);
    CHECK(r.len == 4);

    const Expect wrong_addr = { 34, FC_WRITE_REGISTERS, 4, 2, 0 };
    CHECK(parse_response(RSP_FC16, sizeof RSP_FC16, wrong_addr, &r) == ERR_ECHO);

    const Expect wrong_qty = { 34, FC_WRITE_REGISTERS, 0, 3, 0 };
    CHECK(parse_response(RSP_FC16, sizeof RSP_FC16, wrong_qty, &r) == ERR_ECHO);

    const Expect fc15 = { 33, FC_WRITE_COILS, 0, 10, 0 };
    CHECK(parse_response(RSP_FC15, sizeof RSP_FC15, fc15, &r) == ERR_OK);

    // FC05/FC06 echo a VALUE (multi-writes echo a count), so the value is what's
    // compared - the only confirmation a point moved on a device with no FC15/FC16.
    const Expect fc05 = { 31, FC_WRITE_COIL, 3, 1, 0xFF00 };
    REQUIRE(parse_response(REQ_FC05, sizeof REQ_FC05, fc05, &r) == ERR_OK);
    CHECK(be16(r.data[2], r.data[3]) == 0xFF00);

    const Expect fc06 = { 6, FC_WRITE_REGISTER, 12, 1, 0x1234 };
    REQUIRE(parse_response(REQ_FC06, sizeof REQ_FC06, fc06, &r) == ERR_OK);
    CHECK(be16(r.data[2], r.data[3]) == 0x1234);
}

TEST_CASE("modbus codec: a single write that echoes another value is refused") {
    // A relay board that clamps or ignores commands still answers well-formed; a disagreeing echo must not clear the demand or mark the actuator moved.
    Response r;

    // We asked for the coil OFF; the board says it set it ON.
    const Expect off = { 31, FC_WRITE_COIL, 3, 1, 0x0000 };
    CHECK(parse_response(REQ_FC05, sizeof REQ_FC05, off, &r) == ERR_ECHO);

    // We asked for 0x1234; the board answers with something else.
    const Expect other = { 6, FC_WRITE_REGISTER, 12, 1, 0x9999 };
    CHECK(parse_response(REQ_FC06, sizeof REQ_FC06, other, &r) == ERR_ECHO);

    // The matching value still passes: a comparison, not a blanket refusal.
    const Expect ok = { 6, FC_WRITE_REGISTER, 12, 1, 0x1234 };
    CHECK(parse_response(REQ_FC06, sizeof REQ_FC06, ok, &r) == ERR_OK);
}

TEST_CASE("modbus codec: every request round-trips through the parser") {
    // Each builder's output checked back through the parser: the two must agree.
    uint8_t out[32];
    Response r;

    const uint16_t a = build_write_coil(out, sizeof out, 31, 3, true);
    const Expect e5 = { 31, FC_WRITE_COIL, 3, 1, 0xFF00 };
    CHECK(parse_response(out, a, e5, &r) == ERR_OK);

    const uint16_t b = build_write_register(out, sizeof out, 6, 12, 0x1234);
    const Expect e6 = { 6, FC_WRITE_REGISTER, 12, 1, 0x1234 };
    CHECK(parse_response(out, b, e6, &r) == ERR_OK);
}

TEST_CASE("modbus codec: an Expect we could not have sent sizes nothing") {
    // A quantity past the ceiling describes a payload no frame could carry, hence
    // the hand-built Expect; pinned is the answer (ERR_COUNT), not which check catches it.
    Response r;
    const uint8_t rsp[9] = { 0x05, 0x03, 0x04, 0x00, 0x0A, 0x00, 0x14, 0x9F, 0xFE };

    const Expect wild = { 5, FC_READ_HOLDING, 0, 200, 0 };   // 400 payload bytes
    CHECK(parse_response(rsp, sizeof rsp, wild, &r) == ERR_COUNT);
    CHECK(r.data == nullptr);
    CHECK(r.len == 0);

    // The largest legal quantity still parses: a ceiling, not a blanket refusal.
    const Expect ok = { 5, FC_READ_HOLDING, 0, 2, 0 };
    CHECK(parse_response(rsp, sizeof rsp, ok, &r) == ERR_OK);
}

// Register pairs

TEST_CASE("modbus codec: a register pair reads in either word order") {
    // 0x12345678 as two big-endian registers: 0x1234 then 0x5678.
    const uint8_t hi_first[] = { 0x12, 0x34, 0x56, 0x78 };
    CHECK(u32_from_regs(hi_first, REG_HI_FIRST) == 0x12345678u);
    CHECK(u32_from_regs(hi_first, REG_LO_FIRST) == 0x56781234u);
}

TEST_CASE("modbus codec: u32 and f32 round-trip through to_regs in both word orders") {
    uint8_t buf[4];
    const RegOrder orders[] = { REG_HI_FIRST, REG_LO_FIRST };
    for (unsigned i = 0; i < 2; ++i) {
        u32_to_regs(0xDEADBEEFu, orders[i], buf);
        CHECK(u32_from_regs(buf, orders[i]) == 0xDEADBEEFu);

        f32_to_regs(-273.15f, orders[i], buf);
        CHECK(f32_from_regs(buf, orders[i]) == doctest::Approx(-273.15f));

        // Swapping the order back gives the other pairing - the misconfiguration modeled here.
        u32_to_regs(0x11112222u, orders[i], buf);
        CHECK(u32_from_regs(buf, orders[1 - i]) == 0x22221111u);
    }
}

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT

}  // namespace modbus_codec

// modbus_framing

namespace modbus_framing {

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

}  // namespace

// The happy path

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

TEST_CASE("modbus framing: the length comes from the byte count, not from silence") {
    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS);
    f.feed(RSP_FC03, sizeof RSP_FC03);

    // No time passes at all, yet the frame completes: a 1.75 ms silence is
    // unobservable on a 10 ms beat, so length+CRC is the delimiter, time only a backstop.
    CHECK(f.poll(1000) == 1);
    CHECK(f.err() == ERR_OK);
}

TEST_CASE("modbus framing: an exception frame is five bytes and completes early") {
    RtuFramer f;
    // A normal answer to two registers is nine bytes, the exception five; sizing purely from the request would wait forever for the missing four.
    f.arm(E_FC03, 1000, TIMEOUT_MS);
    CHECK(f.feed(RSP_FC03_EXC, sizeof RSP_FC03_EXC) == sizeof RSP_FC03_EXC);

    REQUIRE(f.poll(1000) == 1);
    CHECK(f.err() == ERR_EXCEPTION);
    CHECK(f.response().ex == EX_ILLEGAL_ADDRESS);
    // Completes: the slave is alive and answering, which the driver needs before backoff decisions.
    CHECK(f.frames() == 1);
    CHECK(f.bad_frames() == 0);
}

// The ways it goes wrong

TEST_CASE("modbus framing: a byte count disagreeing with the request is a frame error") {
    RtuFramer f;
    // Well formed and good CRC, but we asked for ten registers and got two.
    const Expect asked_ten = { 5, FC_READ_HOLDING, 0, 10, 0 };
    f.arm(asked_ten, 1000, TIMEOUT_MS);

    const uint16_t used = f.feed(RSP_FC03, sizeof RSP_FC03);
    CHECK(f.poll(1000) == -1);
    CHECK(f.err() == ERR_COUNT);
    // Rejected the moment the count was read: only three bytes taken.
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
    // Counted apart: it means something physical - a cable, a resistor, a baud mismatch.
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
    // A caller draining a whole ring must keep its surplus; swallowing it would hide a second master.
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

    // The next transaction starts clean; treating those bytes as its head is how a bus desynchronises for good.
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
    // The one input reaching past the buffer: a slave claiming far more than requested,
    // refused at the count rather than clamped (a clamp would misalign the next frame).
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

    // Raw material for later comms alarms, so it accumulates across transactions rather than resetting with arm().
    CHECK(f.frames() == 1);
    CHECK(f.timeouts() == 1);
    f.reset_counters();
    CHECK(f.frames() == 0);
    CHECK(f.timeouts() == 0);
}


// Our own transmission coming back
// RE stays asserted for the port's life, so the receiver hears our own frame first
// - exactly as long as what we sent, so the framer counts it off rather than recognising it.

TEST_CASE("modbus framing: our own request is counted off before the answer") {
    uint8_t req[8] = { 0x05, 0x03, 0x00, 0x00, 0x00, 0x02 };
    const uint16_t rn = seal(req, 6);

    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS, rn);

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

TEST_CASE("modbus framing: an echo split across beats is still counted off") {
    uint8_t req[8] = { 0x05, 0x03, 0x00, 0x00, 0x00, 0x02 };
    const uint16_t rn = seal(req, 6);

    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS, rn);

    for (uint16_t i = 0; i < rn; ++i) CHECK(f.feed(req + i, 1) == 1);
    CHECK(f.len() == 0);
    for (uint16_t i = 0; i < sizeof RSP_FC03; ++i) f.feed(RSP_FC03 + i, 1);

    REQUIRE(f.poll(1000) == 1);
    CHECK(f.err() == ERR_OK);
}

TEST_CASE("modbus framing: a write echo is read as the answer, not as our own") {
    // A write response is byte-identical to its request; comparing bytes alone can't
    // disambiguate, so counting does: first eight bytes ours, next eight the slave's.
    uint8_t req[8] = { 0x22, 0x10, 0x00, 0x00, 0x00, 0x02 };
    const uint16_t rn = seal(req, 6);
    REQUIRE(rn == sizeof RSP_FC16);

    RtuFramer f;
    f.arm(E_FC16, 1000, TIMEOUT_MS, rn);
    CHECK(f.feed(req, rn) == rn);                   // ours
    CHECK(f.poll(1000) == 0);
    CHECK(f.feed(RSP_FC16, sizeof RSP_FC16) == sizeof RSP_FC16);

    // Immediately, not at the deadline: a single write must not cost the whole response timeout.
    REQUIRE(f.poll(1000) == 1);
    CHECK(f.err() == ERR_OK);
    CHECK(f.timeouts() == 0);
}

TEST_CASE("modbus framing: silence after the echo is still a timeout") {
    uint8_t req[8] = { 0x05, 0x03, 0x00, 0x00, 0x00, 0x02 };
    const uint16_t rn = seal(req, 6);

    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS, rn);
    f.feed(req, rn);                                // only our own transmission

    CHECK(f.poll(1000) == 0);
    REQUIRE(f.poll(1000 + TIMEOUT_MS) == -1);
    CHECK(f.err() == ERR_TIMEOUT);
    CHECK(f.timeouts() == 1);
}

TEST_CASE("modbus framing: an echo followed by a refusal reads as the refusal") {
    uint8_t req[8] = { 0x05, 0x03, 0x00, 0x00, 0x00, 0x02 };
    const uint16_t rn = seal(req, 6);

    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS, rn);
    f.feed(req, rn);
    f.feed(RSP_FC03_EXC, sizeof RSP_FC03_EXC);

    REQUIRE(f.poll(1000) == 1);
    CHECK(f.err() == ERR_EXCEPTION);
    CHECK(f.response().ex == EX_ILLEGAL_ADDRESS);
}

TEST_CASE("modbus framing: armed with no echo, the answer is framed as it stands") {
    // A caller that did not transmit (a test, a listener) passes 0; the answer frames as it stands.
    RtuFramer f;
    f.arm(E_FC03, 1000, TIMEOUT_MS);
    CHECK(f.feed(RSP_FC03, sizeof RSP_FC03) == sizeof RSP_FC03);
    REQUIRE(f.poll(1000) == 1);
    CHECK(f.err() == ERR_OK);
}

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT

}  // namespace modbus_framing

// modbus_rtu_slave

namespace modbus_rtu_slave {

#if BOTANIK_FEATURE_MODBUS_RTU_SERVER

using namespace Botanik::Modbus;

namespace {

/** @brief Unit 1 seeded with register i = 0x1000 + i, coil i on when i is even. */
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
    // 128 registers implies a byte count of 256, truncating to 0 in a uint8_t -
    // matching a declared count of 0 and committing 128 registers from whatever follows.
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

TEST_CASE("modbus rtu slave: every answered request counts once, exceptions included") {
    // served() is "requests answered"; an in-path exception and an unsupported-fc exception (from poll()) must each count exactly one.
    RtuSlave d(1);
    seed(d);
    uint8_t out[256];

    // 1. a good read
    uint8_t rd[8];
    rd[0] = 1; rd[1] = FC_READ_HOLDING; rd[2] = 0; rd[3] = 0; rd[4] = 0; rd[5] = 1;
    uint16_t n = seal(rd, 6);
    d.feed(rd, n);
    REQUIRE(d.poll(out, sizeof out) > 0);
    CHECK(d.served() == 1);

    // 2. an exception raised inside the read path (quantity 0)
    rd[0] = 1; rd[1] = FC_READ_HOLDING; rd[2] = 0; rd[3] = 0; rd[4] = 0; rd[5] = 0;
    n = seal(rd, 6);
    d.feed(rd, n);
    REQUIRE(d.poll(out, sizeof out) > 0);
    CHECK(out[1] == (FC_READ_HOLDING | EX_BIT));
    CHECK(d.served() == 2);                 // one answer, one count

    // 3. an unsupported function code, which returns from poll() directly
    rd[0] = 1; rd[1] = 0x2B; rd[2] = 0; rd[3] = 0; rd[4] = 0; rd[5] = 1;
    n = seal(rd, 6);
    d.feed(rd, n);
    REQUIRE(d.poll(out, sizeof out) > 0);
    CHECK(out[2] == EX_ILLEGAL_FUNCTION);
    CHECK(d.served() == 3);                 // the other route agrees
}

TEST_CASE("modbus rtu slave: a byte count that cannot fit does not wedge it") {
    // want = 9+255 = 264, past ADU_MAX; feed() resets the buffer at 256, so it must drop one bad frame rather than wait forever and go deaf.
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
    // A CRC failure must not discard `want` bytes: with 8-byte requests, one dropped byte would misalign every following window for good.
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

TEST_CASE("modbus rtu slave: a broadcast write is executed and not answered") {
    // Unit 0 addresses every device; answering would put several on the wire at once, but the write must still execute even though nothing answers.
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

TEST_CASE("modbus rtu slave: no room to answer is reported, not left silent") {
    // Silence would cost the master its full response timeout for a fault that's entirely ours, then charge the device with a miss.
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

}  // namespace modbus_rtu_slave
