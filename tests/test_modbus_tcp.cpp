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
#include "libbotanik/modbus/tcp/master.h"
#include "libbotanik/modbus/tcp/slave.h"
#include "libbotanik/vars/bindings.h"
#include "site_bank.h"

// modbus_tcp_pdu

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
    // Protocol id is zero for Modbus; a non-zero stream means no resynchronising - the connection goes.
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

    // And one that can't fit a PDU at all: 0xFFFF is a peer we can't stay in step with.
    buf[4] = 0xFF; buf[5] = 0xFF;
    CHECK(decode(buf, n, &r, &used) == DEC_INVALID);
}

TEST_CASE("modbus tcp pdu: a frame still arriving is incomplete, not invalid") {
    // INCOMPLETE means read more, INVALID drops the connection; wrong either way is a hang or a dropped SCADA.
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
    // TCP is a stream: a pipelining peer or a two-request segment must not lose the second.
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
    // Refused as an EXCEPTION, not dropped: well formed and addressed to us, so the peer deserves an answer.
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
    // decode() fills the mbap on a refusal too, so it still addresses the right peer and transaction.
    CHECK(r.mbap.txn == 0x4321);
    CHECK(r.mbap.unit == 3);
}

// Responses, from the master's side

TEST_CASE("modbus tcp pdu: a response for another transaction is not our answer") {
    // The whole point of the transaction id: a late reply in flight with the next request must not match it.
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

// modbus_map

namespace modbus_map {

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER

using namespace Botanik;
using namespace Botanik::Vars;
using namespace Botanik::Modbus;

namespace {

struct Served {
    Bank bank;
    Map  map;
    Served() : map(&bank) {
        TestSite::build(bank);
        map.build();
    }
    /** @brief The first writable row, and the address a master reaches it at. */
    bool first_writable(uint16_t* addr, uint16_t* row) {
        for (uint16_t i = 0; i < bank.count(); ++i) {
            const Descriptor& d = bank.descriptor(i);
            // Map::writable() is file-local; restated here: a numeric, writable parameter.
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

/** @brief @p v into @p p as two big-endian registers, high word first (what word_at() reads back). */
void put_row(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)(v & 0xFF);
}

/** @brief A wire value the bank accepts for @p row, differing from current.
 *  Probed, not computed: bands are the parameter's own business, and
 *  "current minus one" often falls outside them. */
bool other_value(Bank& bank, uint16_t row, uint32_t* out) {
    Var v = bank.var(row);
    const Descriptor& d = *v.meta();
    INFO("row '" << std::string(d.name) << "'");
    // var_band(), not num_band(): limits can be parameters; num_band() would give firmware defaults.
    const NumBand b = var_band(&bank, d);
    const float lo = as_f32(d.type, b.min);
    const float hi = as_f32(d.type, b.max);
    const float now = as_f32(d.type, v.live().v);

    const float probe[5] = { lo, hi, (lo + hi) * 0.5f, now + 1.0f, now - 1.0f };
    for (int i = 0; i < 5; ++i) {
        const Scalar cand = from_f32(d.type, probe[i]);
        if (bank.param_check(v, cand) != SET_OK) continue;
        const uint32_t w = Map::wire_of(d.type, cand);
        if (w == Map::wire_of(d.type, v.live().v)) continue;
        *out = w;
        return true;
    }
    return false;
}

}  // namespace

TEST_CASE("modbus map: a bulk write is applied at once and persisted a few per beat") {
    // A changed row must not reach HAL::Storage inside the decoding beat - up to
    // 61 synchronous flash writes (hundreds of ms with a GC pass) on the shared beat.
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

    // Live straight away, which is what the master and control programs care about.
    CHECK(Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live().v) == after);

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

    // Queue writes to the same row, alternating two accepted values so each is a real change.
    uint32_t a = 0;
    REQUIRE(other_value(s.bank, row, &a));
    const uint32_t b0 = Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live().v);
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
    // EX_SLAVE_BUSY is what a retry is for: better a master told to come back than a plant left on half the settings.
    Served s;
    uint16_t addr = 0;
    uint16_t row = 0;
    REQUIRE(s.first_writable(&addr, &row));

    // Fill the queue, alternating between two accepted values.
    uint32_t a = 0;
    REQUIRE(other_value(s.bank, row, &a));
    const uint32_t b0 = Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live().v);
    for (uint16_t k = 0; k < Config::MODBUS_PERSIST_QUEUE + 4; ++k) {
        const uint32_t w = (k % 2 == 0) ? a : b0;
        uint8_t data[4];
        put_row(data, w);
        if (s.map.write_registers(addr, 2, data, nullptr) == EX_SLAVE_BUSY) {
            CHECK(s.map.persist_room() == 0);
            // And the value did NOT move: the refusal is whole.
            CHECK(Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live().v) != w);
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
    put_row(data, Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live().v));

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
    // Everything queued was acknowledged with EX_NONE; discarding it on rebuild
    // would break that promise, and the refs (BANK row indices) don't go stale across one.
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
    // ...and the value promised to the master is still in the bank.
    CHECK(Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live().v) == w);
    // The map itself is intact after the rebuild.
    CHECK(s.map.count() > 0);
}


// FC06: one register of a two-register row

TEST_CASE("modbus map: a single-register write keeps the half it did not address") {
    // The commonest SCADA write must not come back EX_ILLEGAL_ADDRESS when only the granularity, not the address, differs.
    Served s;
    uint16_t addr = 0, row = 0;
    REQUIRE(s.first_writable(&addr, &row));

    uint32_t want = 0;
    REQUIRE(other_value(s.bank, row, &want));

    const uint16_t hi = (uint16_t)(want >> 16);
    const uint16_t lo = (uint16_t)(want & 0xFFFFu);

    // Two FC06s, either order: after both, the row must hold what a single pair write would have.
    uint16_t committed = 0;
    REQUIRE(s.map.write_register(addr, hi, &committed) == EX_NONE);
    REQUIRE(s.map.write_register((uint16_t)(addr + 1), lo, &committed) == EX_NONE);

    CHECK(Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live().v) == want);
}

TEST_CASE("modbus map: a single-register write goes through the same band check") {
    Served s;
    uint16_t addr = 0, row = 0;
    REQUIRE(s.first_writable(&addr, &row));

    // A value the band should reject: both halves at 0x7FFF gives a large float and integer alike; if the row accepts it, nothing to test.
    const Scalar wild = Map::from_wire(s.bank.var(row).type(), 0x7F7FFFFFu);
    if (s.bank.param_check(s.bank.var(row), wild) == SET_OK) return;

    const uint32_t before =
        Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live().v);
    CHECK(s.map.write_register(addr, 0x7F7Fu) == EX_ILLEGAL_VALUE);
    INFO("a refused half must leave the row where it was");
    CHECK(Map::wire_of(s.bank.var(row).type(), s.bank.var(row).live().v) == before);
}

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER

}  // namespace modbus_map

// modbus_tcp_master

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
    Vars::ConfigBinder binder;
    uint64_t  now = 1000;

    Pair() : slave(Botanik::SetupContext(), &bank), master(Botanik::SetupContext()),
             binder(Botanik::SetupContext(), &bank) {
        TestSite::build(bank);
        slave.setup(now);
        master.setup(now);
        // The listener opens on a config, not at setup: the `mb` group is what
        // says whether this slave answers and on which port.
        Vars::bind_modbus_tcp(binder, slave);
        binder.apply_all();
    }

    /** @brief One beat of both ends, in the order the firmware runs them. */
    void beat() {
        now += 10;
        slave.step(now, Botanik::Tasks::PHASE_INPUT);     // accept, decode, apply
        master.step(now, Botanik::Tasks::PHASE_SERVICE);
        slave.step(now, Botanik::Tasks::PHASE_SERVICE);   // drain the persist queue
        slave.step(now, Botanik::Tasks::PHASE_PRESENT);   // answer off the settled image
    }

    /** @brief Beat until @p done, or give up after @p budget ms. */
    template <typename F>
    bool until(F done, uint32_t budget) {
        const uint64_t started = now;
        while (!done() && (now - started) < budget) beat();
        return done();
    }

    /** @brief Lowest published row's address; the sparse map starts well above zero. */
    uint16_t first_addr() const {
        uint16_t best = 0xFFFF;
        for (uint16_t i = 0; i < bank.count(); ++i) {
            const Descriptor& d = bank.descriptor(i);
            if (!Map::published(d) || d.type == T_BOOL) continue;
            if (d.bind.reg < best) best = d.bind.reg;
        }
        return (uint16_t)(best * 2);
    }

    bool connect() {
        master.connect_to(HAL::Network::mock_loopback_ip(), Modbus::TcpSlaveConfig().port);
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
    // A real answer, not a verdict: end-to-end, so a timeout would mean every other case here was passing on deadlines alone.
    REQUIRE(p.master.error() == ERR_OK);
    CHECK(p.master.response().len == 4);
}

TEST_CASE("modbus tcp master: a function code that is not a read is refused") {
    // read() must refuse a non-read fc, or a write code would be checked against the wrong shape of Expect.
    Pair p;
    REQUIRE(p.connect());

    CHECK_FALSE(p.master.read(1, FC_WRITE_REGISTERS, 0, 2));
    CHECK_FALSE(p.master.read(1, FC_WRITE_COIL, 0, 1));
    CHECK(p.master.read(1, FC_READ_HOLDING, 0, 2));
}

TEST_CASE("modbus tcp master: a transaction always reaches a verdict") {
    // Every state must have a deadline (MA_SENDING included), or an accept-then-silent peer wedges the master.
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

#endif // BOTANIK_FEATURE_MODBUS_TCP_CLIENT && BOTANIK_FEATURE_MODBUS_TCP_SERVER

}  // namespace modbus_tcp_master
