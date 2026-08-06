/**
 * @file test_modbus_pdu.cpp
 * @brief The Modbus codec, driven from literal frames. No bank, no sockets: the
 *        point of splitting pdu.h out is that everything here is bytes in,
 *        bytes out.
 *
 * The distinction under test throughout is framing versus content - a frame that
 * is not Modbus TCP kills the connection, a request that cannot be served
 * answers with an exception and the connection lives.
 */
#include <cstdint>                    // uint8_t/uint16_t - the wire
#include <cstring>                    // memcpy - assembling frames

#include "doctest.h"                  // the test framework (vendored single header)

#include "libbotanik_config.h"        // the site config, then the feature defaults

#include "libbotanik/modbus/pdu.h"    // Modbus::decode / encode_* - under test

// Compiles to an empty TU when the feature is off, contributing no CTest cases.
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER

using namespace Botanik;

namespace {
    /** @brief Build an MBAP header plus @p pdu into @p buf; returns the length. */
    size_t frame(uint8_t* buf, uint16_t txn, uint8_t unit,
                 const uint8_t* pdu, size_t pdu_len) {
        Modbus::wr16(buf + 0, txn);
        Modbus::wr16(buf + 2, 0);
        Modbus::wr16(buf + 4, (uint16_t)(pdu_len + 1));
        buf[6] = unit;
        memcpy(buf + 7, pdu, pdu_len);
        return Modbus::MBAP_LEN + pdu_len;
    }
    /** @brief A read request PDU: fc, address, quantity. */
    size_t read_pdu(uint8_t* p, uint8_t fc, uint16_t addr, uint16_t count) {
        p[0] = fc; Modbus::wr16(p + 1, addr); Modbus::wr16(p + 3, count);
        return 5;
    }
}

TEST_CASE("modbus pdu: a partial frame is incomplete, not an error") {
    uint8_t pdu[8], buf[64];
    const size_t n = frame(buf, 7, 1, pdu, read_pdu(pdu, Modbus::FC_READ_HOLDING, 208, 2));
    Modbus::Request r;
    size_t used = 0;

    // Every prefix short of the whole frame: the server must keep reading rather
    // than decide anything. The header alone is not enough either - it says how
    // long the body is, and the body is not there yet.
    for (size_t k = 0; k < n; k++) {
        INFO("prefix of " << k << " bytes");
        CHECK(Modbus::decode(buf, k, &r, &used) == Modbus::DEC_INCOMPLETE);
    }
    CHECK(Modbus::decode(buf, n, &r, &used) == Modbus::DEC_OK);
}

TEST_CASE("modbus pdu: a frame that is not Modbus TCP is fatal to the connection") {
    uint8_t pdu[8], buf[64];
    const size_t n = frame(buf, 1, 1, pdu, read_pdu(pdu, Modbus::FC_READ_HOLDING, 0, 1));
    Modbus::Request r;
    size_t used = 0;

    SUBCASE("a non-zero protocol id") {
        Modbus::wr16(buf + 2, 1);
        CHECK(Modbus::decode(buf, n, &r, &used) == Modbus::DEC_INVALID);
    }
    SUBCASE("a length below the shortest legal PDU") {
        Modbus::wr16(buf + 4, 1);
        CHECK(Modbus::decode(buf, n, &r, &used) == Modbus::DEC_INVALID);
    }
    SUBCASE("a length past the protocol ceiling") {
        Modbus::wr16(buf + 4, 255);
        CHECK(Modbus::decode(buf, n, &r, &used) == Modbus::DEC_INVALID);
    }
}

TEST_CASE("modbus pdu: a read decodes, and says how much of the stream it took") {
    uint8_t pdu[8], buf[128];
    size_t n = frame(buf, 0x1234, 17, pdu,
                     read_pdu(pdu, Modbus::FC_READ_HOLDING, 208, 4));
    // A second frame right behind it: TCP is a stream, and a master may pipeline.
    const size_t n2 = frame(buf + n, 0x1235, 17, pdu,
                            read_pdu(pdu, Modbus::FC_READ_INPUT, 1602, 1));

    Modbus::Request r;
    size_t used = 0;
    REQUIRE(Modbus::decode(buf, n + n2, &r, &used) == Modbus::DEC_OK);
    CHECK(r.mbap.txn  == 0x1234);
    CHECK(r.mbap.unit == 17);
    CHECK(r.fc        == Modbus::FC_READ_HOLDING);
    CHECK(r.addr      == 208);
    CHECK(r.count     == 4);
    CHECK(used        == n);

    REQUIRE(Modbus::decode(buf + used, n2, &r, &used) == Modbus::DEC_OK);
    CHECK(r.mbap.txn == 0x1235);
    CHECK(r.fc       == Modbus::FC_READ_INPUT);
    CHECK(r.addr     == 1602);
}

TEST_CASE("modbus pdu: a refusal still carries who to answer") {
    // An exception response echoes the transaction id and unit, so a decode that
    // refuses has to have read them: a master with several requests in flight
    // matches replies on that id and nothing else.
    uint8_t pdu[8], buf[64];
    const size_t n = frame(buf, 0xBEEF, 9, pdu, read_pdu(pdu, 0x2B, 0, 1));
    Modbus::Request r;
    size_t used = 0;
    REQUIRE(Modbus::decode(buf, n, &r, &used) == Modbus::DEC_EXCEPTION);
    CHECK(r.ex        == Modbus::EX_ILLEGAL_FUNCTION);
    CHECK(r.mbap.txn  == 0xBEEF);
    CHECK(r.mbap.unit == 9);
    CHECK(r.fc        == 0x2B);
    CHECK(used        == n);        // consumed, so the next frame still decodes
}

TEST_CASE("modbus pdu: quantities outside the protocol's ceilings are refused") {
    uint8_t pdu[8], buf[64];
    Modbus::Request r;
    size_t used = 0;

    struct Case { uint8_t fc; uint16_t count; bool ok; };
    const Case cases[] = {
        { Modbus::FC_READ_HOLDING,  0,    false },   // a read of nothing
        { Modbus::FC_READ_HOLDING,  125,  true  },   // the register ceiling
        { Modbus::FC_READ_HOLDING,  126,  false },
        { Modbus::FC_READ_INPUT,    126,  false },
        { Modbus::FC_READ_COILS,    2000, true  },   // bits get a higher one
        { Modbus::FC_READ_COILS,    2001, false },
        { Modbus::FC_READ_DISCRETE, 2001, false },
    };
    for (const Case& c : cases) {
        const size_t n = frame(buf, 1, 1, pdu, read_pdu(pdu, c.fc, 0, c.count));
        INFO("fc " << (unsigned)c.fc << " count " << c.count);
        const Modbus::Decode d = Modbus::decode(buf, n, &r, &used);
        if (c.ok) {
            CHECK(d == Modbus::DEC_OK);
        } else {
            REQUIRE(d == Modbus::DEC_EXCEPTION);
            CHECK(r.ex == Modbus::EX_ILLEGAL_VALUE);
        }
    }
}

TEST_CASE("modbus pdu: a coil is written 0xFF00 or 0x0000, and nothing else") {
    uint8_t pdu[8], buf[64];
    Modbus::Request r;
    size_t used = 0;

    pdu[0] = Modbus::FC_WRITE_COIL; Modbus::wr16(pdu + 1, 801);

    Modbus::wr16(pdu + 3, 0xFF00);
    REQUIRE(Modbus::decode(buf, frame(buf, 1, 1, pdu, 5), &r, &used) == Modbus::DEC_OK);
    CHECK(r.addr  == 801);
    CHECK(r.value == 0xFF00);

    Modbus::wr16(pdu + 3, 0x0000);
    CHECK(Modbus::decode(buf, frame(buf, 1, 1, pdu, 5), &r, &used) == Modbus::DEC_OK);

    // 0x0001 looks like a 1 and is not one. A master sending it has a bug, and
    // reading it as ON would hide the bug behind an actuator moving.
    Modbus::wr16(pdu + 3, 0x0001);
    REQUIRE(Modbus::decode(buf, frame(buf, 1, 1, pdu, 5), &r, &used) == Modbus::DEC_EXCEPTION);
    CHECK(r.ex == Modbus::EX_ILLEGAL_VALUE);
}

TEST_CASE("modbus pdu: a multi-write's byte count must agree with its quantity") {
    uint8_t pdu[32], buf[64];
    Modbus::Request r;
    size_t used = 0;

    // Two registers: quantity 2, byte count 4, then the data.
    pdu[0] = Modbus::FC_WRITE_REGISTERS;
    Modbus::wr16(pdu + 1, 208);
    Modbus::wr16(pdu + 3, 2);
    pdu[5] = 4;
    Modbus::wr16(pdu + 6, 0x41A0);
    Modbus::wr16(pdu + 8, 0x0000);

    REQUIRE(Modbus::decode(buf, frame(buf, 1, 1, pdu, 10), &r, &used) == Modbus::DEC_OK);
    CHECK(r.addr     == 208);
    CHECK(r.count    == 2);
    CHECK(r.data_len == 4);
    REQUIRE(r.data != nullptr);
    CHECK(Modbus::rd16(r.data) == 0x41A0);

    SUBCASE("a byte count that disagrees") {
        pdu[5] = 2;                        // says two bytes, quantity says four
        REQUIRE(Modbus::decode(buf, frame(buf, 1, 1, pdu, 10), &r, &used) ==
                Modbus::DEC_EXCEPTION);
        CHECK(r.ex == Modbus::EX_ILLEGAL_VALUE);
    }
    SUBCASE("a byte count that agrees but a frame that is short") {
        // Byte count and quantity agree; the frame carries fewer bytes than both
        // claim. Refused, not read past the end.
        REQUIRE(Modbus::decode(buf, frame(buf, 1, 1, pdu, 8), &r, &used) ==
                Modbus::DEC_EXCEPTION);
        CHECK(r.ex == Modbus::EX_ILLEGAL_VALUE);
    }
    SUBCASE("coils pack, so their byte count is not the quantity doubled") {
        pdu[0] = Modbus::FC_WRITE_COILS;
        Modbus::wr16(pdu + 3, 10);         // 10 bits -> 2 bytes
        pdu[5] = 2;
        CHECK(Modbus::decode(buf, frame(buf, 1, 1, pdu, 8), &r, &used) == Modbus::DEC_OK);
        pdu[5] = 20;                       // the register arithmetic, wrongly
        REQUIRE(Modbus::decode(buf, frame(buf, 1, 1, pdu, 26), &r, &used) ==
                Modbus::DEC_EXCEPTION);
        CHECK(r.ex == Modbus::EX_ILLEGAL_VALUE);
    }
}

TEST_CASE("modbus pdu: an exception response is nine bytes with the top bit set") {
    uint8_t buf[16] = {0};
    Modbus::Mbap m = { 0x0A0B, 5 };
    REQUIRE(Modbus::encode_exception(buf, sizeof(buf), m,
                                     Modbus::FC_WRITE_REGISTER,
                                     Modbus::EX_ILLEGAL_ADDRESS) == 9);
    CHECK(Modbus::rd16(buf + 0) == 0x0A0B);   // transaction echoed
    CHECK(Modbus::rd16(buf + 2) == 0);        // protocol id
    CHECK(Modbus::rd16(buf + 4) == 3);        // unit + fc + code
    CHECK(buf[6] == 5);
    CHECK(buf[7] == 0x86);                    // FC 6 with the exception bit
    CHECK(buf[8] == Modbus::EX_ILLEGAL_ADDRESS);

    // Never a partial frame: a buffer one byte short writes nothing at all.
    uint8_t small[8] = {0};
    CHECK(Modbus::encode_exception(small, sizeof(small), m,
                                   Modbus::FC_WRITE_REGISTER,
                                   Modbus::EX_ILLEGAL_ADDRESS) == 0);
    CHECK(small[0] == 0);
}

TEST_CASE("modbus pdu: a read response frames the payload it is handed") {
    uint8_t buf[32] = {0};
    Modbus::Mbap m = { 1, 1 };
    const uint8_t payload[4] = { 0x41, 0xA0, 0x00, 0x00 };   // 20.0f, big-endian
    REQUIRE(Modbus::encode_read(buf, sizeof(buf), m, Modbus::FC_READ_HOLDING,
                                payload, sizeof(payload)) == 13);
    CHECK(Modbus::rd16(buf + 4) == 7);        // unit + fc + byte count + 4
    CHECK(buf[7] == Modbus::FC_READ_HOLDING);
    CHECK(buf[8] == 4);                       // byte count, not register count
    CHECK(Modbus::rd16(buf + 9) == 0x41A0);

    uint8_t small[12] = {0};
    CHECK(Modbus::encode_read(small, sizeof(small), m, Modbus::FC_READ_HOLDING,
                              payload, sizeof(payload)) == 0);
}

TEST_CASE("modbus pdu: a write is answered by echoing what it asked for") {
    uint8_t buf[32] = {0};
    Modbus::Mbap m = { 0x77, 2 };
    REQUIRE(Modbus::encode_echo(buf, sizeof(buf), m, Modbus::FC_WRITE_REGISTERS,
                                208, 2) == 12);
    CHECK(Modbus::rd16(buf + 4) == 6);
    CHECK(buf[7] == Modbus::FC_WRITE_REGISTERS);
    CHECK(Modbus::rd16(buf + 8)  == 208);
    CHECK(Modbus::rd16(buf + 10) == 2);
}

TEST_CASE("modbus pdu: bits pack LSB first, which is not the obvious order") {
    // Bit 0 is the LOW bit of the first byte. Getting this backwards puts every
    // alarm in a SCADA display on the wrong lamp, and looks plausible either way.
    uint8_t p[3] = {0, 0, 0};
    CHECK(Modbus::bit_bytes(1)  == 1);
    CHECK(Modbus::bit_bytes(8)  == 1);
    CHECK(Modbus::bit_bytes(9)  == 2);
    CHECK(Modbus::bit_bytes(17) == 3);

    Modbus::put_bit(p, 0, true);
    CHECK(p[0] == 0x01);
    Modbus::put_bit(p, 7, true);
    CHECK(p[0] == 0x81);
    Modbus::put_bit(p, 8, true);
    CHECK(p[1] == 0x01);
    Modbus::put_bit(p, 0, false);
    CHECK(p[0] == 0x80);
    CHECK(Modbus::get_bit(p, 7));
    CHECK_FALSE(Modbus::get_bit(p, 0));
}

TEST_CASE("modbus pdu: which function codes write, and which address bits") {
    CHECK_FALSE(Modbus::writes(Modbus::FC_READ_HOLDING));
    CHECK_FALSE(Modbus::writes(Modbus::FC_READ_COILS));
    CHECK(Modbus::writes(Modbus::FC_WRITE_COIL));
    CHECK(Modbus::writes(Modbus::FC_WRITE_REGISTER));
    CHECK(Modbus::writes(Modbus::FC_WRITE_COILS));
    CHECK(Modbus::writes(Modbus::FC_WRITE_REGISTERS));

    CHECK(Modbus::bit_access(Modbus::FC_READ_COILS));
    CHECK(Modbus::bit_access(Modbus::FC_READ_DISCRETE));
    CHECK(Modbus::bit_access(Modbus::FC_WRITE_COIL));
    CHECK(Modbus::bit_access(Modbus::FC_WRITE_COILS));
    CHECK_FALSE(Modbus::bit_access(Modbus::FC_READ_HOLDING));
    CHECK_FALSE(Modbus::bit_access(Modbus::FC_WRITE_REGISTERS));
}

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER
