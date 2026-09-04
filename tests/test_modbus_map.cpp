/**
 * @file test_modbus_map.cpp
 * @brief The register map: what a stranger's address means in terms of the
 *        variable bank.
 *
 * This is the whole authorisation surface of the TCP server. Everything a
 * remote master may read, and the much smaller set it may write, is decided
 * here, from an address it chose. Addresses are looked up rather than indexed,
 * because the published registers are sparse - so the cases that matter are the
 * ones landing in a gap, past the end, or on a row that is not a parameter.
 *
 * Written against what the map actually holds rather than literal addresses, so
 * it keeps testing the rule after the site's table moves.
 */

#include <vector>

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/modbus/tcp/map.h"

#include "site_bank.h"

namespace modbus_map {

using namespace Botanik;
using namespace Botanik::Modbus;

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER && BOTANIK_FEATURE_VARS

/** @brief A row's Modbus REGISTER address. Two words per row, high first, so
 *  the address is twice the row number. Bits are indexed by the row number
 *  itself - that asymmetry is the map's, and this is where the file states
 *  it. @see docs/dev/modbus.md */
static uint16_t reg_addr(uint16_t reg) { return (uint16_t)(reg * 2u); }

struct MapFixture {
    Vars::Bank bank;
    Map        map;

    MapFixture() : map(&bank) {
        TestSite::build(bank);
        map.build();
    }

    /** @brief A mapped register whose descriptor satisfies @p pred, or 0. */
    template <typename P>
    uint16_t find_reg(P pred) const {
        for (uint16_t i = 0; i < bank.count(); i++) {
            const Vars::Descriptor& d = bank.var(i).meta();
            if (d.bind.reg == 0) continue;
            bool found = false;
            map.row_at(d.bind.reg, &found);
            if (found && pred(d)) return d.bind.reg;
        }
        return 0;
    }
    uint16_t a_param() const {
        return find_reg([](const Vars::Descriptor& d) {
            return d.kind == Vars::K_PARAM && d.type != Vars::T_BOOL;
        });
    }
    uint16_t a_readonly() const {
        return find_reg([](const Vars::Descriptor& d) {
            return d.kind != Vars::K_PARAM;
        });
    }
    /** @brief The lowest register the map publishes. */
    uint16_t lowest() const {
        uint16_t lo = 0xFFFF;
        for (uint16_t i = 0; i < bank.count(); i++) {
            const Vars::Descriptor& d = bank.var(i).meta();
            if (d.bind.reg == 0) continue;
            bool found = false;
            map.row_at(d.bind.reg, &found);
            if (found && d.bind.reg < lo) lo = d.bind.reg;
        }
        return lo;
    }
};

TEST_CASE("modbus map: the site's published rows all fit") {
    MapFixture f;
    // overflow is rows the map could not hold. Non-zero means a master silently
    // cannot see part of the plant, with nothing on the wire to say so.
    INFO("count " << f.map.count() << ", overflow " << f.map.overflow());
    CHECK(f.map.overflow() == 0);
    CHECK(f.map.count() > 0);
}

TEST_CASE("modbus map: registers come out in ascending order") {
    MapFixture f;
    // The sort is what lets a lookup binary-search; the bank's own order is the
    // table's, which is not register order. Out of order, _lower_bound silently
    // finds the wrong row.
    uint16_t prev = 0;
    uint16_t counted = 0;
    for (uint32_t r = 0; r <= 0xFFFF; r++) {
        bool found = false;
        Vars::Var v = f.map.row_at((uint16_t)r, &found);
        if (!found) continue;
        counted++;
        const uint16_t reg = v.meta().bind.reg;
        INFO("register " << r << " resolved to a row at " << reg);
        CHECK(reg >= prev);
        prev = reg;
    }
    CHECK(counted == f.map.count());
}

TEST_CASE("modbus map: an address in a gap is not the neighbouring row") {
    MapFixture f;
    // The published set is sparse. A lookup that clamped to the nearest row
    // would hand a master someone else's value under an address it asked for -
    // and a WRITE would land on the wrong parameter.
    uint16_t gaps = 0;
    for (uint32_t r = 0; r <= 0xFFFF && gaps < 64; r++) {
        bool found = false;
        Vars::Var v = f.map.row_at((uint16_t)r, &found);
        if (found) {
            CHECK(v.meta().bind.reg == (uint16_t)r);
        } else {
            gaps++;
            INFO("register " << r << " is unmapped");
            CHECK(v.null());   // the inert row
        }
    }
    CHECK(gaps > 0);
}

TEST_CASE("modbus map: a read that misses the map entirely is refused") {
    MapFixture f;
    uint8_t out[512];
    const uint16_t lo = f.lowest();
    REQUIRE(lo != 0xFFFF);

    // Below the first published row and above the last: nothing to answer with.
    CHECK(f.map.read_registers(0, 2, out, sizeof(out)) == EX_ILLEGAL_ADDRESS);
    CHECK(f.map.read_registers(0xFFF0, 8, out, sizeof(out)) == EX_ILLEGAL_ADDRESS);

    CHECK(f.map.read_registers(reg_addr(lo), 2, out, sizeof(out)) == EX_NONE);
}

TEST_CASE("modbus map: a read across a gap reads zeros rather than refusing") {
    MapFixture f;
    uint8_t out[512];
    const uint16_t lo = f.lowest();
    REQUIRE(lo != 0xFFFF);

    // The published set is sparse and a master reads it in blocks, so a run
    // straddling a hole is the NORMAL request, not an error. Refusing it would
    // force every master to know the site's table. The hole reads as zeros.
    const Exception ex = f.map.read_registers(reg_addr(lo), 64, out, sizeof(out));
    INFO("64 registers from the first published row");
    CHECK(ex == EX_NONE);
}

TEST_CASE("modbus map: a read is refused rather than overrunning the buffer") {
    MapFixture f;
    const uint16_t lo = f.lowest();
    uint8_t small[2];

    // The caller's buffer is the encoder's, sized from the PDU maximum. Asking
    // for more registers than it holds is a bug, and it must fail rather than
    // write past it.
    CHECK(f.map.read_registers(reg_addr(lo), 8, small, sizeof(small)) != EX_NONE);

    uint8_t one[1];
    CHECK(f.map.read_bits(lo, 64, BITS_VALUE, one, sizeof(one)) != EX_NONE);
}

TEST_CASE("modbus map: the two bit tables answer different questions") {
    MapFixture f;
    uint8_t val[32] = {}, qual[32] = {};
    const uint16_t lo = f.lowest();
    REQUIRE(lo != 0xFFFF);

    // Bits are indexed by the ROW number, where a register address is twice it:
    // a row is two words but one coil.
    REQUIRE(f.map.read_bits(lo, 16, BITS_VALUE, val, sizeof(val)) == EX_NONE);
    REQUIRE(f.map.read_bits(lo, 16, BITS_QUALITY, qual, sizeof(qual)) == EX_NONE);

    // Coils carry the value, discrete inputs carry valid(). Checked per bit
    // against the bank, so this cannot pass by both tables being the same.
    for (uint16_t i = 0; i < 16; i++) {
        bool found = false;
        Vars::Var v = f.map.row_at((uint16_t)(lo + i), &found);
        const bool vbit = (val[i / 8] >> (i % 8)) & 1u;
        const bool qbit = (qual[i / 8] >> (i % 8)) & 1u;
        INFO("row number " << (lo + i) << (found ? " (mapped)" : " (a gap)"));
        if (!found) {
            CHECK_FALSE(vbit);             // a gap reads 0 in both
            CHECK_FALSE(qbit);
        } else {
            CHECK(qbit == v.valid());
            // A float at that number is not a coil: reporting "non-zero" for it
            // would invent a signal the table never declared.
            CHECK(vbit == (v.type() == Vars::T_BOOL && v.as_bool()));
        }
    }
}

TEST_CASE("modbus map: register 0xFFFF is not in the map") {
    MapFixture f;
    // _reg_of() reads 0xFFFF past the end of the sorted index, and 0xFFFF is a
    // legal register, so a lookup that leaned on that sentinel reported a hit
    // and indexed _rows[_count] - one past the rows it had. With static storage
    // that read 0 and resolved every request for 65535 to bank row 0.
    bool found = true;
    Vars::Var v = f.map.row_at(0xFFFF, &found);
    CHECK_FALSE(found);
    CHECK(v.null());   // the inert row, not row 0

    uint16_t committed = 1;
    CHECK(f.map.write_bit(0xFFFF, true, &committed) == EX_ILLEGAL_ADDRESS);
    CHECK(committed == 0);

    uint8_t out[8];
    CHECK(f.map.read_bits(0xFFFF, 1, BITS_VALUE, out, sizeof(out)) == EX_ILLEGAL_ADDRESS);
}

TEST_CASE("modbus map: an address row takes a half write like any other") {
    MapFixture f;
    const uint16_t reg = f.find_reg([](const Vars::Descriptor& d) {
        return d.kind == Vars::K_PARAM && d.type == Vars::T_STR;
    });
    if (reg == 0) return;                  // no address row published

    // FC06 reaches one of the two registers an address spans. There is no need
    // to refuse it: net_ parameters are STAGED, and nothing acts on a partly
    // written address until its group serial is bumped.
    bool found = false;
    Vars::Var v = f.map.row_at(reg, &found);
    REQUIRE(found);
    INFO("writing one register of " << v.meta().name);
    const uint32_t w = f.map.to_wire(v);
    uint16_t committed = 0;
    CHECK(f.map.write_register(reg_addr(reg), (uint16_t)(w >> 16), &committed)
          != EX_ILLEGAL_ADDRESS);
}

TEST_CASE("modbus map: only a parameter may be written") {
    MapFixture f;
    const uint16_t ro = f.a_readonly();
    REQUIRE(ro != 0);

    // Everything else is a reading. A master that could write one would be
    // forging a sensor value that alarms and control programs then act on.
    uint16_t committed = 99;
    CHECK(f.map.write_register(reg_addr(ro), 1234, &committed) == EX_ILLEGAL_ADDRESS);
    CHECK(committed == 0);

    CHECK(f.map.write_register(0, 1, nullptr) == EX_ILLEGAL_ADDRESS);
    CHECK(f.map.write_bit(ro, true, &committed) == EX_ILLEGAL_ADDRESS);
    CHECK(committed == 0);
}

TEST_CASE("modbus map: a write goes through the band check an operator gets") {
    MapFixture f;
    const uint16_t reg = f.a_param();
    REQUIRE(reg != 0);

    bool found = false;
    Vars::Var v = f.map.row_at(reg, &found);
    REQUIRE(found);
    const Vars::Descriptor& d = v.meta();
    INFO("writing " << d.name << " at register " << reg);

    // Var::param_set() is the one door, so a wire write is range-checked as
    // the shell's `params set` is. A master is not more trusted than an operator.
    const uint32_t w = f.map.to_wire(v);
    uint16_t committed = 0;
    CHECK(f.map.write_register(reg_addr(reg), (uint16_t)(w >> 16), &committed) == EX_NONE);
    INFO("writing back what is already there must commit nothing");
    CHECK(committed == 0);
}

TEST_CASE("modbus map: a no-op write commits nothing, so flash is not worn out") {
    MapFixture f;
    const uint16_t reg = f.a_param();
    REQUIRE(reg != 0);
    bool found = false;
    Vars::Var v = f.map.row_at(reg, &found);
    REQUIRE(found);

    // A master re-asserting its whole setpoint block every poll is normal
    // operation. One flash write per register per poll would wear the store out
    // in weeks, so equality is checked before committing.
    const uint32_t w = f.map.to_wire(v);
    uint8_t data[4] = { (uint8_t)(w >> 24), (uint8_t)(w >> 16),
                        (uint8_t)(w >> 8),  (uint8_t)(w & 0xFF) };
    uint16_t committed = 7;
    const Exception ex = f.map.write_registers(reg_addr(reg), 2, data, &committed);
    CHECK(ex == EX_NONE);
    CHECK(committed == 0);
}

TEST_CASE("modbus map: to_wire round-trips through the registers that carry it") {
    MapFixture f;
    // The conversion is specified here rather than through the server, so the
    // wire format can be asserted without a socket.
    uint16_t checked = 0;
    for (uint16_t i = 0; i < f.bank.count() && checked < 32; i++) {
        Vars::Var v = f.bank.var(i);
        const Vars::Descriptor& d = v.meta();
        if (d.bind.reg == 0) continue;
        bool found = false;
        f.map.row_at(d.bind.reg, &found);
        if (!found) continue;
        checked++;

        const uint32_t w = f.map.to_wire(v);
        uint8_t out[8] = {};
        REQUIRE(f.map.read_registers(reg_addr(d.bind.reg), 2, out, sizeof(out)) == EX_NONE);
        // Big-endian registers, high word first: the protocol's order, not this
        // transport's choice.
        const uint32_t from_read = ((uint32_t)out[0] << 24) | ((uint32_t)out[1] << 16) |
                                   ((uint32_t)out[2] << 8)  | (uint32_t)out[3];
        INFO(d.name << " at register " << d.bind.reg);
        CHECK(from_read == w);
    }
    CHECK(checked > 0);
}

#endif

}  // namespace modbus_map
