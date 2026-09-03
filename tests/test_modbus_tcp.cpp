/**
 * @file test_modbus_tcp.cpp
 * @brief The MBAP codec, which is what a stranger on the plant network reaches
 *        first.
 *
 * decode() has four outcomes and the split between two of them is the whole
 * point: a FRAMING error means the peer is not speaking Modbus TCP and the
 * connection goes, a CONTENT error means one request cannot be served and the
 * connection stays. Getting that backwards is either a server that drops a
 * client over a typo'd register, or one that keeps reading a stream it has lost
 * sync with. TCP is a stream, so there is no resynchronising to a frame edge.
 */

#include <vector>

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/modbus/tcp/pdu.h"

namespace modbus_tcp {

using namespace Botanik;
using namespace Botanik::Modbus;

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER || BOTANIK_FEATURE_MODBUS_TCP_CLIENT

/** @brief An MBAP frame: txn, protocol id, length, unit, then the PDU.
 *  @p len_override writes a length field that disagrees with the body, which is
 *  how a malformed frame is built. */
static std::vector<uint8_t> mbap(uint16_t txn, uint8_t unit,
                                 const std::vector<uint8_t>& pdu,
                                 int proto = 0, int len_override = -1) {
    const uint16_t declared = (len_override >= 0) ? (uint16_t)len_override
                                                  : (uint16_t)(pdu.size() + 1);
    std::vector<uint8_t> f;
    f.push_back((uint8_t)(txn >> 8));       f.push_back((uint8_t)(txn & 0xFF));
    f.push_back((uint8_t)(proto >> 8));     f.push_back((uint8_t)(proto & 0xFF));
    f.push_back((uint8_t)(declared >> 8));  f.push_back((uint8_t)(declared & 0xFF));
    f.push_back(unit);
    f.insert(f.end(), pdu.begin(), pdu.end());
    return f;
}

/** @brief A read PDU: fc, address, quantity. */
static std::vector<uint8_t> read_pdu(uint8_t fc, uint16_t addr, uint16_t qty) {
    return { fc, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
                 (uint8_t)(qty >> 8),  (uint8_t)(qty & 0xFF) };
}

static Decode run(const std::vector<uint8_t>& f, Request* r, size_t* consumed) {
    return decode(f.data(), f.size(), r, consumed);
}

TEST_CASE("modbus tcp: a read request decodes and says what it consumed") {
    Request r; size_t used = 0;
    const std::vector<uint8_t> f = mbap(0x1234, 0x11, read_pdu(FC_READ_HOLDING, 0x006B, 3));

    REQUIRE(run(f, &r, &used) == DEC_OK);
    CHECK(r.mbap.txn == 0x1234);
    CHECK(r.mbap.unit == 0x11);
    CHECK(r.fc == FC_READ_HOLDING);
    CHECK(r.addr == 0x006B);
    CHECK(r.count == 3);
    INFO("a stream can hold several frames, so the caller shifts by this");
    CHECK(used == f.size());
}

TEST_CASE("modbus tcp: a frame still arriving is incomplete, not invalid") {
    // The ordinary case on a stream: a read returned half a frame. Answering
    // DEC_INVALID here would drop a healthy client on every split packet.
    const std::vector<uint8_t> f = mbap(1, 1, read_pdu(FC_READ_HOLDING, 0, 1));
    for (size_t n = 0; n < f.size(); n++) {
        INFO("first " << n << " bytes of " << f.size());
        Request r; size_t used = 12345;
        CHECK(decode(f.data(), n, &r, &used) == DEC_INCOMPLETE);
    }
    Request r; size_t used = 0;
    CHECK(decode(f.data(), f.size(), &r, &used) == DEC_OK);
}

TEST_CASE("modbus tcp: two frames in one read are decoded one at a time") {
    std::vector<uint8_t> two = mbap(1, 0x11, read_pdu(FC_READ_HOLDING, 0, 1));
    const size_t first = two.size();
    const std::vector<uint8_t> b = mbap(2, 0x11, read_pdu(FC_READ_COILS, 8, 16));
    two.insert(two.end(), b.begin(), b.end());

    Request r; size_t used = 0;
    REQUIRE(decode(two.data(), two.size(), &r, &used) == DEC_OK);
    CHECK(r.mbap.txn == 1);
    CHECK(used == first);

    REQUIRE(decode(two.data() + used, two.size() - used, &r, &used) == DEC_OK);
    CHECK(r.mbap.txn == 2);
    CHECK(r.fc == FC_READ_COILS);
}

TEST_CASE("modbus tcp: a protocol id that is not zero is not Modbus TCP") {
    // Framing, so the connection goes: something else is on this port and there
    // is no frame edge in a stream to resynchronise to.
    Request r; size_t used = 0;
    CHECK(run(mbap(1, 1, read_pdu(FC_READ_HOLDING, 0, 1), /*proto=*/1), &r, &used)
          == DEC_INVALID);
}

TEST_CASE("modbus tcp: a declared length outside the legal range is framing") {
    Request r; size_t used = 0;

    SUBCASE("zero: shorter than the unit id it must include") {
        CHECK(run(mbap(1, 1, read_pdu(FC_READ_HOLDING, 0, 1), 0, 0), &r, &used)
              == DEC_INVALID);
    }
    SUBCASE("past the largest PDU the protocol allows") {
        CHECK(run(mbap(1, 1, read_pdu(FC_READ_HOLDING, 0, 1), 0, 0xFFFF), &r, &used)
              == DEC_INVALID);
    }
}

TEST_CASE("modbus tcp: an unknown function is refused, and the peer keeps talking") {
    // Content: the framing was fine, so one exception goes back and the
    // connection lives. A scanner probing function codes must not cost the
    // connection a real client is using.
    Request r; size_t used = 0;
    REQUIRE(run(mbap(0x0007, 0x11, { 0x41, 0x00, 0x00, 0x00, 0x01 }), &r, &used)
            == DEC_EXCEPTION);
    CHECK(r.ex == EX_ILLEGAL_FUNCTION);
    INFO("the refusal has to be addressable, so the header comes back with it");
    CHECK(r.mbap.txn == 0x0007);
    CHECK(r.mbap.unit == 0x11);
    CHECK(r.fc == 0x41);
    CHECK(used > 0);
}

TEST_CASE("modbus tcp: a quantity past the protocol ceiling is refused") {
    Request r; size_t used = 0;

    // 2000 bits and 125 registers are the published maxima. Past them the
    // response would not fit a PDU, so it is refused rather than truncated.
    REQUIRE(run(mbap(1, 1, read_pdu(FC_READ_COILS, 0, 2001)), &r, &used) == DEC_EXCEPTION);
    CHECK(r.ex == EX_ILLEGAL_VALUE);
    REQUIRE(run(mbap(1, 1, read_pdu(FC_READ_HOLDING, 0, 126)), &r, &used) == DEC_EXCEPTION);
    CHECK(r.ex == EX_ILLEGAL_VALUE);

    // ...and the ceilings themselves are allowed.
    CHECK(run(mbap(1, 1, read_pdu(FC_READ_COILS, 0, 2000)), &r, &used) == DEC_OK);
    CHECK(run(mbap(1, 1, read_pdu(FC_READ_HOLDING, 0, 125)), &r, &used) == DEC_OK);
}

TEST_CASE("modbus tcp: a zero quantity is refused rather than served empty") {
    Request r; size_t used = 0;
    CHECK(run(mbap(1, 1, read_pdu(FC_READ_HOLDING, 0, 0)), &r, &used) == DEC_EXCEPTION);
}

TEST_CASE("modbus tcp: a multi-write byte count must agree with its quantity") {
    Request r; size_t used = 0;
    // FC16: fc, addr, qty, byte count, then the data. The count is the peer's
    // and the data length is derived from it, so a disagreement is exactly the
    // shape of an over-read.
    std::vector<uint8_t> pdu = { FC_WRITE_REGISTERS, 0x00, 0x01, 0x00, 0x02,
                                 /*byte count*/ 0x04, 0x00, 0x0A, 0x01, 0x02 };
    // Named, not a temporary: Request::data points INTO this buffer, so a frame
    // that dies at the end of the full expression leaves it dangling.
    const std::vector<uint8_t> good = mbap(1, 1, pdu);
    REQUIRE(run(good, &r, &used) == DEC_OK);
    CHECK(r.count == 2);
    REQUIRE(r.data != nullptr);
    CHECK(r.data[0] == 0x00);
    CHECK(r.data[1] == 0x0A);

    pdu[5] = 0x06;                       // says six bytes, carries four
    const std::vector<uint8_t> lying = mbap(1, 1, pdu);
    CHECK(run(lying, &r, &used) != DEC_OK);
}

TEST_CASE("modbus tcp: a single write carries its value, not a payload pointer") {
    Request r; size_t used = 0;
    REQUIRE(run(mbap(1, 1, { FC_WRITE_COIL, 0x00, 0xAC, 0xFF, 0x00 }), &r, &used) == DEC_OK);
    CHECK(r.addr == 0x00AC);
    CHECK(r.value == 0xFF00);

    REQUIRE(run(mbap(1, 1, { FC_WRITE_REGISTER, 0x00, 0x01, 0x00, 0x03 }), &r, &used) == DEC_OK);
    CHECK(r.addr == 0x0001);
    CHECK(r.value == 0x0003);
}

TEST_CASE("modbus tcp: an encoded response echoes the header it answers") {
    uint8_t buf[TCP_ADU_MAX] = {};
    Mbap m; m.txn = 0xBEEF; m.unit = 0x11;

    const uint8_t payload[4] = { 0x02, 0x2B, 0x00, 0x64 };
    const size_t n = encode_read(buf, sizeof(buf), m, FC_READ_HOLDING, payload, 4);
    REQUIRE(n == MBAP_LEN + 2 + 4);      // header, fc, byte count, payload
    CHECK(buf[0] == 0xBE); CHECK(buf[1] == 0xEF);
    CHECK(buf[2] == 0x00); CHECK(buf[3] == 0x00);        // protocol id
    CHECK(((buf[4] << 8) | buf[5]) == (int)(n - 6));     // length counts from unit
    CHECK(buf[6] == 0x11);
    CHECK(buf[7] == FC_READ_HOLDING);
    CHECK(buf[8] == 4);
}

TEST_CASE("modbus tcp: an exception sets the top bit of the function code") {
    uint8_t buf[TCP_ADU_MAX] = {};
    Mbap m; m.txn = 0x0001; m.unit = 0x11;
    const size_t n = encode_exception(buf, sizeof(buf), m, FC_READ_HOLDING,
                                      EX_ILLEGAL_ADDRESS);
    REQUIRE(n == 9);
    CHECK(buf[7] == (FC_READ_HOLDING | 0x80));
    CHECK(buf[8] == EX_ILLEGAL_ADDRESS);
}

TEST_CASE("modbus tcp: an encoder given no room writes nothing and says so") {
    // A partial frame on a stream desynchronises everything after it, so a
    // refusal has to be total.
    Mbap m; m.txn = 1; m.unit = 1;
    const uint8_t payload[4] = { 1, 2, 3, 4 };
    uint8_t small[8];
    memset(small, 0xAA, sizeof(small));

    CHECK(encode_read(small, sizeof(small), m, FC_READ_HOLDING, payload, 4) == 0);
    CHECK(encode_exception(small, 4, m, FC_READ_HOLDING, EX_SLAVE_FAILURE) == 0);
    CHECK(encode_echo(small, 4, m, FC_WRITE_COIL, 1, 2) == 0);
    for (size_t i = 0; i < sizeof(small); i++) CHECK(small[i] == 0xAA);
}

#endif

}  // namespace modbus_tcp
