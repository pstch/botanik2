/**
 * @file test_modbus_tcp.cpp
 * @brief Modbus TCP: the MBAP/PDU codec, the register map, and the polling master.
 *
 * Each section below is the former test_<name>.cpp, kept in its own
 * namespace so their fixtures and helpers cannot collide.
 */

#include <cstdint>
#include <cstring>
#include "doctest.h"
#include "libbotanik/modbus/modbus.h"
#include "libbotanik/modbus/tcp/pdu.h"
#include <string>
#include "libbotanik_site.h"
#include "libbotanik.h"
#include BOTANIK_SITE_HEADER
#include "libbotanik/modbus/tcp/master.h"   // not in modbus.h's aggregate
#include "libbotanik/modbus/tcp/slave.h"

// --------------------------------------------------------------------------
// modbus_tcp_pdu
// --------------------------------------------------------------------------

namespace modbus_tcp_pdu {

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

}  // namespace modbus_tcp_pdu

// --------------------------------------------------------------------------
// modbus_map
// --------------------------------------------------------------------------

namespace modbus_map {

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER

using namespace Botanik;
using namespace Botanik::Vars;
using namespace Botanik::Modbus;

namespace {

struct Served {
    Bank bank;
    Map  map;
    Served() : bank(Site::g_site.vars), map(&bank) {
        bank.init();
        map.build();
    }
    /** @brief The first writable row, and the address a master reaches it at. */
    bool first_writable(uint16_t* addr, uint16_t* row) {
        const VarTables& t = Site::g_site.vars;
        for (uint16_t i = 0; i < t.count; ++i) {
            const Variable& d = t.var[i];
            // Map::writable() is file-local, so the rule is restated: a
            // parameter, not read-only, not text. A numeric one, since the
            // register path is what is under test here.
            if (!Map::published(d) || d.role != ROLE_PARAM) continue;
            if ((d.flags & F_RDONLY) != 0) continue;
            if (d.type == T_BOOL || d.type == T_STR) continue;
            *addr = (uint16_t)(d.bind.reg * 2);
            *row  = i;
            return true;
        }
        return false;
    }
};

/** @brief One row's value into @p p: 32 bits across TWO big-endian registers,
 *  high word first, which is what word_at() reads back. */
void put_row(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)(v & 0xFF);
}

/** @brief A wire value the bank will accept for @p row and that differs from
 *  what it holds. Probed rather than computed: a parameter's band is its own
 *  business, and "current minus one" is outside plenty of them. */
bool other_value(Bank& bank, uint16_t row, uint32_t* out) {
    Var v = bank.var(row);
    const Variable& d = *v.meta();
    INFO("row '" << std::string(d.name) << "'");
    // var_band(), not num_band(): a parameter's limits can themselves be
    // parameters, and num_band() would hand back the firmware defaults.
    const NumBand b = var_band(&bank, d);
    const float lo = as_f32(d.type, b.min);
    const float hi = as_f32(d.type, b.max);
    const float now = as_f32(d.type, v.live());

    const float probe[5] = { lo, hi, (lo + hi) * 0.5f, now + 1.0f, now - 1.0f };
    for (int i = 0; i < 5; ++i) {
        const Scalar cand = from_f32(d.type, probe[i]);
        if (bank.check(v, cand) != SET_OK) continue;
        const uint32_t w = Map::wire_of(d.type, cand);
        if (w == Map::wire_of(d.type, v.live())) continue;
        *out = w;
        return true;
    }
    return false;
}

}  // namespace

TEST_CASE("modbus map: a bulk write is applied at once and persisted a few per beat") {
    // The regression. Every changed row used to reach HAL::Storage inside the
    // beat that decoded the request - up to 61 synchronous flash writes, which
    // is hundreds of milliseconds with a garbage-collection pass in it, on the
    // beat the field bus shares.
    Served s;
    uint16_t addr = 0;
    uint16_t row = 0;
    REQUIRE(s.first_writable(&addr, &row));

    uint32_t after = 0;
    REQUIRE(other_value(s.bank, row, &after));

    uint8_t data[4];
    put_row(data, after);

    uint16_t committed = 0;
    REQUIRE(s.map.write_registers(addr, 2, data, &committed) == EX_NONE);
    CHECK(committed == 1);

    // Live straight away: that is what the master and the control programs
    // both care about.
    CHECK(Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live()) == after);

    // ...and the flash write is owed, not done.
    CHECK(s.map.pending_persists() == 1);
    CHECK(s.map.drain_persists(Config::MODBUS_PERSIST_PER_STEP) == 1);
    CHECK(s.map.pending_persists() == 0);
}

TEST_CASE("modbus map: the drain is bounded, however much is queued") {
    Served s;
    uint16_t addr = 0;
    uint16_t row = 0;
    REQUIRE(s.first_writable(&addr, &row));

    // Queue several writes to the same row, alternating between two accepted
    // values so each one is a real change.
    uint32_t a = 0;
    REQUIRE(other_value(s.bank, row, &a));
    const uint32_t b0 = Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live());
    uint16_t queued = 0;
    for (uint16_t k = 0; k < 6; ++k) {
        uint8_t data[4];
        put_row(data, (k % 2 == 0) ? a : b0);
        uint16_t c = 0;
        if (s.map.write_registers(addr, 2, data, &c) == EX_NONE) queued += c;
    }
    REQUIRE(queued >= 3);
    REQUIRE(s.map.pending_persists() == queued);

    const uint16_t did = s.map.drain_persists(2);
    INFO("a beat must not be able to spend more than it was budgeted");
    CHECK(did <= 2);
    CHECK(s.map.pending_persists() == (uint16_t)(queued - did));
}

TEST_CASE("modbus map: a write with nowhere to queue is refused whole") {
    // EX_SLAVE_BUSY is what a retry is for. A master told to come back is
    // better served than a plant left on half the new settings.
    Served s;
    uint16_t addr = 0;
    uint16_t row = 0;
    REQUIRE(s.first_writable(&addr, &row));

    // Fill the queue by alternating the row between two accepted values.
    uint32_t a = 0;
    REQUIRE(other_value(s.bank, row, &a));
    const uint32_t b0 = Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live());
    for (uint16_t k = 0; k < Config::MODBUS_PERSIST_QUEUE + 4; ++k) {
        const uint32_t w = (k % 2 == 0) ? a : b0;
        uint8_t data[4];
        put_row(data, w);
        if (s.map.write_registers(addr, 2, data, nullptr) == EX_SLAVE_BUSY) {
            CHECK(s.map.persist_room() == 0);
            // And the value did NOT move: the refusal is whole.
            CHECK(Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live()) != w);
            return;
        }
    }
    FAIL("the queue never filled, so EX_SLAVE_BUSY was never exercised");
}

TEST_CASE("modbus map: a no-op write costs no flash at all") {
    // A master re-asserting what is already set must not wear the part out.
    Served s;
    uint16_t addr = 0;
    uint16_t row = 0;
    REQUIRE(s.first_writable(&addr, &row));

    uint8_t data[4];
    put_row(data, Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live()));

    uint16_t committed = 1;
    CHECK(s.map.write_registers(addr, 2, data, &committed) == EX_NONE);
    CHECK(committed == 0);
    CHECK(s.map.pending_persists() == 0);
}

TEST_CASE("modbus map: an odd address or count is refused, not rounded") {
    Served s;
    uint16_t addr = 0;
    uint16_t row = 0;
    REQUIRE(s.first_writable(&addr, &row));

    uint8_t data[4] = { 0, 0, 0, 0 };
    CHECK(s.map.write_registers((uint16_t)(addr + 1), 2, data, nullptr)
          == EX_ILLEGAL_ADDRESS);
    CHECK(s.map.write_registers(addr, 1, data, nullptr) == EX_ILLEGAL_ADDRESS);
}

TEST_CASE("modbus map: rebuilding makes the queue durable rather than dropping it") {
    // Everything queued was acknowledged to a master with EX_NONE. Discarding it
    // on a rebuild would break that promise silently - and the refs are BANK row
    // indices, which a rebuild does not renumber, so there was never anything
    // stale about them to justify it.
    Served s;
    uint16_t addr = 0;
    uint16_t row = 0;
    REQUIRE(s.first_writable(&addr, &row));

    uint32_t w = 0;
    REQUIRE(other_value(s.bank, row, &w));
    uint8_t data[4];
    put_row(data, w);
    REQUIRE(s.map.write_registers(addr, 2, data, nullptr) == EX_NONE);
    REQUIRE(s.map.pending_persists() > 0);

    s.map.build();
    // Drained, so nothing is owed...
    CHECK(s.map.pending_persists() == 0);
    // ...and the value the master was promised is still the one in the bank.
    CHECK(Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live()) == w);
    // The map itself is intact after the rebuild.
    CHECK(s.map.count() > 0);
}


// FC06: one register of a two-register row
// ===========================================================================

TEST_CASE("modbus map: a single-register write keeps the half it did not address") {
    // Writing one setpoint is the commonest thing a SCADA does, and it used to
    // come back EX_ILLEGAL_ADDRESS - not even the right objection, since the
    // address was fine and only the granularity was not.
    Served s;
    uint16_t addr = 0, row = 0;
    REQUIRE(s.first_writable(&addr, &row));

    uint32_t want = 0;
    REQUIRE(other_value(s.bank, row, &want));

    const uint16_t hi = (uint16_t)(want >> 16);
    const uint16_t lo = (uint16_t)(want & 0xFFFFu);

    // Two FC06s, one per half, in either order: after both the row must hold
    // exactly the value a single pair write would have put there.
    uint16_t committed = 0;
    REQUIRE(s.map.write_register(addr, hi, &committed) == EX_NONE);
    REQUIRE(s.map.write_register((uint16_t)(addr + 1), lo, &committed) == EX_NONE);

    CHECK(Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live()) == want);
}

TEST_CASE("modbus map: a single-register write goes through the same band check") {
    Served s;
    uint16_t addr = 0, row = 0;
    REQUIRE(s.first_writable(&addr, &row));

    // A value the band will not have. Both halves at 0x7FFF gives a large
    // positive float and a large integer alike; if the row happens to accept it
    // there is nothing to test here.
    const Scalar wild = Map::from_wire(s.bank.var(row).type(), 0x7F7FFFFFu);
    if (s.bank.check(s.bank.var(row), wild) == SET_OK) return;

    const uint32_t before =
        Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live());
    CHECK(s.map.write_register(addr, 0x7F7Fu) == EX_ILLEGAL_VALUE);
    INFO("a refused half must leave the row where it was");
    CHECK(Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live()) == before);
}

TEST_CASE("modbus map: a single-register write to an unmapped address is refused") {
    Served s;
    CHECK(s.map.write_register(0xFFFEu, 1) == EX_ILLEGAL_ADDRESS);
}

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER

}  // namespace modbus_map

// --------------------------------------------------------------------------
// modbus_tcp_master
// --------------------------------------------------------------------------

namespace modbus_tcp_master {

#if BOTANIK_FEATURE_MODBUS_TCP_CLIENT && BOTANIK_FEATURE_MODBUS_TCP_SERVER

using namespace Botanik;
using namespace Botanik::Vars;
using namespace Botanik::Modbus;

namespace {

/** @brief A slave and a master, wired to each other through the sim's loopback. */
struct Pair {
    Bank      bank;
    TcpSlave  slave;
    TcpMaster master;
    uint64_t  now = 1000;

    Pair() : bank(Site::g_site.vars), slave(&bank), master() {
        bank.init();
        slave.setup(now);
        master.setup(now);
    }

    /** @brief One beat of both ends, in the order the firmware runs them. */
    void beat() {
        now += 10;
        slave.step(now);
        master.step(now);
        slave.service_tx(now);
    }

    /** @brief Beat until @p done, or give up after @p budget ms. */
    template <typename F>
    bool until(F done, uint32_t budget) {
        const uint64_t started = now;
        while (!done() && (now - started) < budget) beat();
        return done();
    }

    /** @brief The address of the lowest published row: the map is sparse and
     *  starts well above zero, so a read of register 0 is off the end of it. */
    uint16_t first_addr() const {
        const VarTables& t = Site::g_site.vars;
        uint16_t best = 0xFFFF;
        for (uint16_t i = 0; i < t.count; ++i) {
            const Variable& d = t.var[i];
            if (!Map::published(d) || d.type == T_BOOL) continue;
            if (d.bind.reg < best) best = d.bind.reg;
        }
        return (uint16_t)(best * 2);
    }

    bool connect() {
        master.connect_to(HAL::Network::mock_loopback_ip(), Config::MODBUS_TCP_PORT);
        TcpMaster* m = &master;
        return until([m] { return m->connected(); }, 5000);
    }
};

}  // namespace

TEST_CASE("modbus tcp master: it reaches the slave and reads a register") {
    Pair p;
    REQUIRE(p.slave.listening());
    REQUIRE(p.connect());

    REQUIRE(p.master.read(Config::MODBUS_TCP_UNIT_ID, FC_READ_HOLDING,
                          p.first_addr(), 2));

    TcpMaster* m = &p.master;
    REQUIRE(p.until([m] { return m->done(); }, 5000));
    INFO("error " << (int)p.master.error());
    // A real answer, not merely a verdict: this is the end-to-end path, so a
    // timeout here would mean the two never spoke and every other case in this
    // file was passing on the strength of its deadlines alone.
    REQUIRE(p.master.error() == ERR_OK);
    CHECK(p.master.response().len == 4);
}

TEST_CASE("modbus tcp master: a function code that is not a read is refused") {
    // read() took over from a read_bits() that framed whatever fc it was given,
    // so a write code could go out through the read path carrying a read's
    // Expect - and then be checked against the wrong shape of answer.
    Pair p;
    REQUIRE(p.connect());

    CHECK_FALSE(p.master.read(1, FC_WRITE_REGISTERS, 0, 2));
    CHECK_FALSE(p.master.read(1, FC_WRITE_COIL, 0, 1));
    CHECK(p.master.read(1, FC_READ_HOLDING, 0, 2));
}

TEST_CASE("modbus tcp master: a transaction always reaches a verdict") {
    // Every state has a deadline now, MA_SENDING included - it was the one with
    // none, so a peer that accepted and never read wedged the master for good.
    Pair p;
    REQUIRE(p.connect());
    REQUIRE(p.master.read(Config::MODBUS_TCP_UNIT_ID, FC_READ_HOLDING,
                          p.first_addr(), 2));

    // Well past every deadline the master has.
    const uint32_t budget = (uint32_t)(Config::MODBUS_TCP_RESPONSE_TIMEOUT_MS * 4u
                                     + Config::MODBUS_TCP_CONNECT_TIMEOUT_MS * 4u);
    TcpMaster* m = &p.master;
    CHECK(p.until([m] { return m->done(); }, budget));
}

TEST_CASE("modbus tcp master: it can be driven again after a completed transaction") {
    // A transaction that ends must leave the machine usable, whatever it ended
    // as - otherwise one bad answer takes the link down until a reconnect.
    Pair p;
    REQUIRE(p.connect());

    for (int round = 0; round < 3; ++round) {
        if (!p.master.connected()) {
            REQUIRE(p.connect());
        }
        REQUIRE(p.master.read(Config::MODBUS_TCP_UNIT_ID, FC_READ_HOLDING,
                              (uint16_t)(p.first_addr() + round * 2), 2));
        TcpMaster* m = &p.master;
        REQUIRE(p.until([m] { return m->done(); }, 5000));
        (void)p.master.take_result();
    }
}

#endif // BOTANIK_FEATURE_MODBUS_TCP_CLIENT && BOTANIK_FEATURE_MODBUS_TCP_SERVER

}  // namespace modbus_tcp_master
