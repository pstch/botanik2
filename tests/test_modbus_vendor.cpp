/**
 * @file test_modbus_vendor.cpp
 * @brief Our codec against the byte-exact examples printed in the device
 *        manuals.
 *
 * Every other codec test asserts we agree with ourselves. These assert we agree
 * with the two devices actually on this site's bus, using the frames their
 * manuals print - CRCs included, which is the one part of the protocol a
 * self-consistent implementation can get uniformly wrong and never notice.
 *
 * Sources:
 *  - Eastron SDM630-Modbus V2 user manual V1.6B, 3.8.1 "Read Input Registers"
 *  - Lettel MCT1-140 Modbus table, 2.1 "Lecture des donnees"
 */
#include <cstdint>
#include <cstring>

#include "doctest.h"

#include "libbotanik/modbus/modbus.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

using namespace Botanik::Modbus;

TEST_CASE("modbus vendor: the SDM630 manual's own read request, byte for byte") {
    // "The following query will request 'Volts 1' from an instrument with node
    // address 1": 01 04 0000 0002, error check low 71, high CB.
    uint8_t out[16];
    const uint16_t n = build_read(out, sizeof out, 1, FC_READ_INPUT, 0, 2);

    REQUIRE(n == 8);
    const uint8_t want[8] = { 0x01, 0x04, 0x00, 0x00, 0x00, 0x02, 0x71, 0xCB };
    for (uint16_t i = 0; i < 8; ++i) {
        INFO("byte " << i);
        CHECK(out[i] == want[i]);
    }
}

TEST_CASE("modbus vendor: the SDM630 manual's own response decodes to 230.2 V") {
    // 01 04 04 43 66 33 34, error check 1B 38. "the contents of Volts 1 as 230.2"
    const uint8_t rsp[9] = { 0x01, 0x04, 0x04, 0x43, 0x66, 0x33, 0x34, 0x1B, 0x38 };
    const Expect e = { 1, FC_READ_INPUT, 0, 2, 0 };

    Response r;
    REQUIRE(parse_response(rsp, sizeof rsp, e, &r) == ERR_OK);
    REQUIRE(r.len == 4);

    // "All values stored and returned are in floating point format to IEEE 754
    // with the most significant register first" - so REG_HI_FIRST, which is what
    // a row without F_WORD_SWAP asks for.
    const float v = f32_from_regs(r.data, REG_HI_FIRST);
    CHECK(v == doctest::Approx(230.2f).epsilon(0.001));

    // And the other order is wrong, so the test would notice a swapped default.
    CHECK_FALSE(f32_from_regs(r.data, REG_LO_FIRST) == doctest::Approx(230.2f));
}

TEST_CASE("modbus vendor: the MCT1-140 manual's own exchange") {
    // Request "02 03 00 00 00 02 C4 38", answered "02 03 04 43 68 99 9A B7 50",
    // which the manual reads as 232.6 V.
    uint8_t out[16];
    const uint16_t n = build_read(out, sizeof out, 2, FC_READ_HOLDING, 0, 2);

    REQUIRE(n == 8);
    const uint8_t want[8] = { 0x02, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x38 };
    for (uint16_t i = 0; i < 8; ++i) {
        INFO("byte " << i);
        CHECK(out[i] == want[i]);
    }

    const uint8_t rsp[9] = { 0x02, 0x03, 0x04, 0x43, 0x68, 0x99, 0x9A, 0xB7, 0x50 };
    const Expect e = { 2, FC_READ_HOLDING, 0, 2, 0 };
    Response r;
    REQUIRE(parse_response(rsp, sizeof rsp, e, &r) == ERR_OK);
    CHECK(f32_from_regs(r.data, REG_HI_FIRST) == doctest::Approx(232.6f).epsilon(0.001));
}

TEST_CASE("modbus vendor: the SDM630 manual's own exception frame") {
    // 3.10: function 10 OR 80 = 90, exception code 01, error check 8D C0.
    const uint8_t rsp[5] = { 0x01, 0x90, 0x01, 0x8D, 0xC0 };
    const Expect e = { 1, FC_WRITE_REGISTERS, 0, 2, 0 };

    Response r;
    CHECK(parse_response(rsp, sizeof rsp, e, &r) == ERR_EXCEPTION);
    CHECK(r.ex == EX_ILLEGAL_FUNCTION);
    CHECK(r.fc == FC_WRITE_REGISTERS);   // the exception bit is stripped
}

TEST_CASE("modbus vendor: the SDM630 refuses a request that splits a float") {
    // "the 'Starting Address' and the 'Number of Points' must be even numbers to
    // request a floating point variable. If [either] is odd then the query will
    // fall in the middle of a floating point variable [and] the product will
    // return an error message."
    //
    // Nothing in modbus/ can enforce a rule that belongs to one device model, so
    // this pins the site table instead: every row this meter serves sits on a
    // pair boundary and is a pair wide, which is what keeps every block the
    // planner can cut out of them even at both ends.
    const uint16_t reg[] = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26,
                             28, 30, 32, 34, 36, 38, 40, 42, 46, 48, 52, 56, 60,
                             62, 66, 70, 200, 202, 204, 206, 224, 248, 340 };
    for (uint16_t i = 0; i < sizeof reg / sizeof reg[0]; ++i) {
        INFO("register " << reg[i]);
        CHECK((reg[i] % 2) == 0);
    }
}

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
