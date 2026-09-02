/**
 * @file test_modbus.cpp
 * @brief The transport-independent half - the wire vocabulary, the CRC, and
 *        the answer check both transports share - then the RTU client's own
 *        ranking, which needs a bank.
 */

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/modbus/rtu/codec.h"
#include "libbotanik/modbus/response.h"
#include "libbotanik/modbus/rtu/client.h"

#include "site_bank.h"

namespace modbus_wire {

using namespace Botanik;
using namespace Botanik::Modbus;

TEST_CASE("modbus: the wire vocabulary agrees with the frame arithmetic") {
    // request_len / response_len / read_payload are constexpr and everything
    // sizes buffers off them, so a disagreement is silent until a frame is
    // short on the wire.
    CHECK(is_read(FC_READ_COILS));
    CHECK(is_read(FC_READ_HOLDING));
    CHECK_FALSE(is_read(FC_WRITE_COIL));
    CHECK(is_bit(FC_READ_COILS));
    CHECK_FALSE(is_bit(FC_READ_HOLDING));

    // A bit read packs 8 per byte and rounds up; a register read is 2 each.
    CHECK(bit_bytes(1) == 1);
    CHECK(bit_bytes(8) == 1);
    CHECK(bit_bytes(9) == 2);
    CHECK(read_payload(FC_READ_COILS, 9) == 2);
    CHECK(read_payload(FC_READ_HOLDING, 9) == 18);

    // The published ceilings: 2000 bits, 125 registers.
    CHECK(max_quantity(FC_READ_COILS) == 2000);
    CHECK(max_quantity(FC_READ_HOLDING) == 125);

    // A read's response is header + payload; a single write echoes its request.
    CHECK(response_len(FC_READ_HOLDING, 2) == 5 + 4);
    CHECK(response_len(FC_WRITE_COIL, 1) == 8);
    CHECK(request_len(FC_READ_HOLDING, 2) == 8);
}

TEST_CASE("modbus: CRC16 matches the published check value, and self-cancels") {
    // The standard's own vector: "123456789" over the Modbus polynomial is
    // 0x4B37. If this moves, every device on the bus stops answering.
    const uint8_t vec[9] = { '1','2','3','4','5','6','7','8','9' };
    CHECK(crc16(vec, 9) == 0x4B37);

    // A frame with its own CRC appended checksums to zero - which is how
    // parse_response() tests a frame in one pass rather than two.
    uint8_t adu[8] = {};
    const uint16_t n = build_read(adu, sizeof(adu), 17, FC_READ_HOLDING, 0x006B, 3);
    REQUIRE(n == 8);
    CHECK(crc16(adu, n) == 0);

    // ...and one flipped bit does not.
    adu[3] ^= 0x01;
    CHECK(crc16(adu, n) != 0);
}

TEST_CASE("modbus: build_read writes the frame the spec shows, or nothing") {
    uint8_t adu[16] = {};
    // The worked example from the spec: unit 17, read 3 holding from 0x006B.
    const uint16_t n = build_read(adu, sizeof(adu), 0x11, FC_READ_HOLDING, 0x006B, 3);
    REQUIRE(n == 8);
    CHECK(adu[0] == 0x11);
    CHECK(adu[1] == 0x03);
    CHECK(adu[2] == 0x00);   // address, big-endian
    CHECK(adu[3] == 0x6B);
    CHECK(adu[4] == 0x00);   // quantity, big-endian
    CHECK(adu[5] == 0x03);

    // Never a partial frame: half of one corrupts the next transaction on a
    // shared bus, so a refusal writes nothing and says 0.
    uint8_t small[4] = { 0xAA, 0xAA, 0xAA, 0xAA };
    CHECK(build_read(small, sizeof(small), 0x11, FC_READ_HOLDING, 0, 3) == 0);
    CHECK(small[0] == 0xAA);
    // Past what the function code allows, in a buffer with room.
    CHECK(build_read(adu, sizeof(adu), 0x11, FC_READ_HOLDING, 0, 126) == 0);
    CHECK(build_read(adu, sizeof(adu), 0x11, FC_READ_COILS, 0, 2001) == 0);
}

TEST_CASE("modbus: a coil write puts 0xFF00 on the wire, not 1") {
    uint8_t adu[16] = {};
    REQUIRE(build_write_coil(adu, sizeof(adu), 0x11, 0x00AC, true) == 8);
    CHECK(adu[4] == 0xFF);
    CHECK(adu[5] == 0x00);
    REQUIRE(build_write_coil(adu, sizeof(adu), 0x11, 0x00AC, false) == 8);
    CHECK(adu[4] == 0x00);
    CHECK(adu[5] == 0x00);
}

TEST_CASE("modbus: a write's payload sits where the settle path looks for it") {
    // RtuClient::_settle() hands clear_dirty() a pointer into the request still
    // in flight, so where the payload starts is part of that contract: 4 for the
    // single writes, which have no byte count, and 7 for the multiples.
    uint8_t adu[32] = {};

    REQUIRE(build_write_register(adu, sizeof(adu), 0x11, 0x0001, 0x0003) == 8);
    CHECK(adu[4] == 0x00);
    CHECK(adu[5] == 0x03);

    const uint8_t regs[4] = { 0x00, 0x0A, 0x01, 0x02 };
    const uint16_t n = build_write_registers(adu, sizeof(adu), 0x11, 0x0001, 2, regs);
    REQUIRE(n == 13);                       // 7 head + 4 payload + 2 CRC
    CHECK(n - 7u - 2u == sizeof(regs));
    CHECK(memcmp(adu + 7, regs, sizeof(regs)) == 0);

    const uint8_t bits[1] = { 0x05 };
    const uint16_t c = build_write_coils(adu, sizeof(adu), 0x11, 0x0001, 3, bits);
    REQUIRE(c == 10);                       // 7 head + 1 payload + 2 CRC
    CHECK(adu[7] == 0x05);
}

TEST_CASE("modbus: match_response separates a refusal from a mismatch") {
    // Written in BODY bytes, which is what makes one function serve both
    // transports: everything after the unit byte, trailer already excluded.
    const Expect e = { 0x11, FC_READ_HOLDING, 0x006B, 3, 0 };
    Response r = {};

    // A good read: fc, byte count, then 3 registers.
    const uint8_t good[8] = { 0x03, 0x06, 0x02, 0x2B, 0x00, 0x00, 0x00, 0x64 };
    REQUIRE(match_response(0x11, good, 8, e, &r) == ERR_OK);
    CHECK(r.len == 6);
    CHECK(r.data == good + 2);
    CHECK(r.ex == EX_NONE);

    // An EXCEPTION is a result, not a parse failure: the device is alive and
    // said no, and the caller must be able to tell that from silence.
    const uint8_t refused[2] = { 0x83, 0x02 };
    CHECK(match_response(0x11, refused, 2, e, &r) == ERR_EXCEPTION);
    CHECK(r.ex == EX_ILLEGAL_ADDRESS);
    CHECK(r.fc == FC_READ_HOLDING);     // the exception bit is stripped
    CHECK(r.unit == 0x11);

    // Wrong unit, wrong function, and a byte count that disagrees each get
    // their own diagnosis rather than one generic failure.
    CHECK(match_response(0x12, good, 8, e, &r) == ERR_UNIT);
    const uint8_t other_fc[8] = { 0x04, 0x06, 0, 0, 0, 0, 0, 0 };
    CHECK(match_response(0x11, other_fc, 8, e, &r) == ERR_FC);
    const uint8_t short_count[6] = { 0x03, 0x04, 0, 0, 0, 0 };
    CHECK(match_response(0x11, short_count, 6, e, &r) == ERR_COUNT);
}

TEST_CASE("modbus: a single write is confirmed by its echo alone") {
    // FC05/FC06 echo the value, and that echo is the ONLY confirmation it
    // landed - the multi-writes echo a count instead.
    const Expect e = { 0x11, FC_WRITE_COIL, 0x00AC, 1, 0xFF00 };
    Response r = {};

    const uint8_t echo[5] = { 0x05, 0x00, 0xAC, 0xFF, 0x00 };
    CHECK(match_response(0x11, echo, 5, e, &r) == ERR_OK);

    // A different address, or a different value, is not our transaction.
    const uint8_t wrong_addr[5] = { 0x05, 0x00, 0xAD, 0xFF, 0x00 };
    CHECK(match_response(0x11, wrong_addr, 5, e, &r) == ERR_ECHO);
    const uint8_t wrong_val[5] = { 0x05, 0x00, 0xAC, 0x00, 0x00 };
    CHECK(match_response(0x11, wrong_val, 5, e, &r) == ERR_ECHO);
}

TEST_CASE("modbus: a multi-write's byte count must agree with its quantity") {
    // The check both slaves make before trusting a master's arithmetic. The
    // 32-bit computation is the point: 2*qty wraps to 0 at qty 128 in a
    // uint8_t, and a declared count of 0 would then match - letting a short
    // frame commit 128 registers read from past the end of the buffer.
    CHECK(check_write_count(FC_WRITE_REGISTERS, 6, 3) == EX_NONE);
    CHECK(check_write_count(FC_WRITE_REGISTERS, 0, 128) == EX_ILLEGAL_VALUE);
    CHECK(check_write_count(FC_WRITE_REGISTERS, 5, 3) == EX_ILLEGAL_VALUE);

    // Bits pack 8 per byte and round up, so the expected count differs by code.
    CHECK(check_write_count(FC_WRITE_COILS, 2, 9) == EX_NONE);
    CHECK(check_write_count(FC_WRITE_COILS, 1, 9) == EX_ILLEGAL_VALUE);

    // A quantity of zero writes nothing and is refused rather than ignored,
    // and one past the code's ceiling is refused before any arithmetic.
    CHECK(check_write_count(FC_WRITE_REGISTERS, 0, 0) == EX_ILLEGAL_VALUE);
    CHECK(check_write_count(FC_WRITE_REGISTERS, 252, 126) == EX_ILLEGAL_VALUE);
    CHECK(check_write_count(FC_WRITE_COILS, 251, 2001) == EX_ILLEGAL_VALUE);
}

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
TEST_CASE("modbus: a tuning row forced to zero cannot divide the ranking") {
    Botanik::Tasks::Registry reg;
    Vars::Bank bank;
    TestSite::build(bank);

    RtuClient rtu(Botanik::SetupContext(reg), &bank, Site::g_site.devices);
    rtu.register_tasks();
    reg.setup(0);
    REQUIRE(rtu.plan().block_count() > 0);

    // What the shell does. An override is type-checked and NOT banded, so it
    // reaches _tune() past a band that starts at 1000.
    const Vars::VarRef r = bank.find("rtu_rewrite_ms");
    REQUIRE(r.index != Vars::VAR_NONE);
    REQUIRE(bank.override_set(bank.var(r), "0") == Vars::SET_OK);

    // _pick() divides by the deadline this feeds. It used to be SIGFPE here.
    for (uint64_t t = 0; t <= 30000; t += 100) {
        const uint16_t pick = rtu.next_block(t);
        CHECK((pick == RtuClient::NO_BLOCK || pick < rtu.plan().block_count()));
    }
}
#endif

}  // namespace modbus_wire
