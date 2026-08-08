/**
 * @file test_modbus_map.cpp
 * @brief The TCP slave's register map: what an address means, and what a write
 *        through it costs.
 *
 * map.h calls these tests the specification of the wire format, so the
 * conversions are pinned here rather than inferred from the code. The rest is
 * about the write path, where a single request can cover 61 parameters and the
 * store behind them is synchronous.
 */
#include <cstdint>
#include <cstring>
#include <string>

#include "doctest.h"

#include "libbotanik_site.h"
#include "libbotanik.h"

#include BOTANIK_SITE_HEADER

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
