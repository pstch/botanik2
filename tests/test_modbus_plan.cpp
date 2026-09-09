/**
 * @file test_modbus_plan.cpp
 * @brief The RTU scan planner, against the reference site's real bus.
 *
 * The planner decides what actually goes on the RS-485 wire: which rows merge
 * into one request, how big a request each device will tolerate, and when each
 * block falls due. A block that is legal for the protocol but not for the device
 * gets exception 02 for ever, and the row behind it reads as a dead sensor
 * rather than as a planning bug. So the cases here check each block against the
 * MODEL of the device it addresses, not against the protocol's own ceilings.
 */

#include <set>
#include <vector>

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/modbus/rtu/plan.h"

#include "site_bank.h"

namespace tests { namespace modbus_plan {

using namespace Botanik;
using namespace Botanik::Modbus;

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

/** @brief A planner over the site's own table, at the site's own bus settings. */
struct PlanFixture {
    Vars::Bank bank;
    Planner    p;

    PlanFixture(uint32_t baud = 19200, uint32_t turnaround_ms = 10) {
        TestSite::build(bank);
        p.plan(bank, baud, turnaround_ms, Site::g_site.devices);
    }
};

TEST_CASE("modbus plan: every declared row on the bus gets scanned") {
    PlanFixture f;
    // The two ways a row silently stops being read: more field rows than
    // the plan holds, and a block that could not be made legal for its device.
    // Both are counted rather than logged once at bring-up, because a row that
    // is never requested looks exactly like a device that never answers.
    INFO("dropped " << f.p.rows_dropped() << ", unscanned " << f.p.rows_unscanned());
    CHECK(f.p.rows_dropped() == 0);
    CHECK(f.p.rows_unscanned() == 0);
    CHECK(f.p.block_count() > 0);
    CHECK(f.p.row_count() > 0);
}

TEST_CASE("modbus plan: no block asks a device for more than it will answer") {
    PlanFixture f;
    for (uint16_t i = 0; i < f.p.block_count(); i++) {
        const Block& b = f.p.block(i);
        const DeviceModel& m = model_of(Site::g_site.devices, b.device);
        INFO("block " << i << " unit " << (int)b.device << " (" << m.name
             << ") fc " << (int)b.fc << " reg " << b.reg << " x" << b.regs);

        const uint16_t ceiling = is_bit(b.fc)
            ? (m.max_bits != 0 ? m.max_bits : max_quantity(b.fc))
            : (m.max_regs != 0 ? m.max_regs : max_quantity(b.fc));
        CHECK(b.regs >= 1);
        CHECK(b.regs <= ceiling);

        // Alignment: a device storing 32-bit values in register pairs answers
        // exception 02 to a request landing in the middle of one.
        if (!is_bit(b.fc)) {
            if (m.addr_align > 1) CHECK(b.reg % m.addr_align == 0);
            if (m.qty_align  > 1) CHECK(b.regs % m.qty_align == 0);
        }
    }
}

TEST_CASE("modbus plan: a block's rows are its own and all of them are on the bus") {
    PlanFixture f;
    std::vector<int> seen(f.p.row_count(), 0);

    for (uint16_t i = 0; i < f.p.block_count(); i++) {
        const Block& b = f.p.block(i);
        INFO("block " << i << " rows [" << b.first_row << ", "
             << (b.first_row + b.row_count) << ") of " << f.p.row_count());
        REQUIRE(b.row_count >= 1);
        REQUIRE(b.first_row + b.row_count <= f.p.row_count());

        for (uint16_t k = 0; k < b.row_count; k++) {
            const uint16_t slot = (uint16_t)(b.first_row + k);
            seen[slot]++;
            const uint16_t vi = f.p.row(slot);
            REQUIRE(vi < f.bank.count());
            // Every row the planner claims must actually live on the field bus,
            // or the client would write a local terminal from a bus answer.
            CHECK(f.bank.descriptor(vi).bind.device != 0);
        }
    }
    // Each row belongs to exactly one block: two blocks sharing a row means one
    // of them overwrites what the other just read.
    for (size_t i = 0; i < seen.size(); i++) {
        INFO("row slot " << i << " claimed " << seen[i] << " times");
        CHECK(seen[i] == 1);
    }
}

TEST_CASE("modbus plan: a read block has a period and a write block does not") {
    PlanFixture f;
    for (uint16_t i = 0; i < f.p.block_count(); i++) {
        const Block& b = f.p.block(i);
        INFO("block " << i << " fc " << (int)b.fc);
        // A write is driven by what the programs demanded, not by a clock, so
        // its period is 0 and its due_ms is a floor rather than a schedule.
        if (is_read(b.fc)) CHECK(b.period_ms > 0);
        else               CHECK(b.period_ms == 0);
    }
}

TEST_CASE("modbus plan: seed spreads the first activations across the period") {
    PlanFixture f;
    f.p.seed(1000);

    // A ladder of same-rate groups all falling due on one beat is a burst the
    // bus cannot carry, and it repeats every period. The same rule the task
    // registry uses.
    std::set<uint64_t> due;
    uint16_t reads = 0;
    for (uint16_t i = 0; i < f.p.block_count(); i++) {
        const Block& b = f.p.block(i);
        if (!is_read(b.fc)) continue;
        reads++;
        due.insert(b.due_ms);
        INFO("block " << i << " period " << b.period_ms << " due " << b.due_ms);
        CHECK(b.due_ms >= 1000);
        CHECK(b.due_ms <= 1000 + b.period_ms);
    }
    if (reads > 2) {
        INFO(reads << " read blocks landed on " << due.size() << " distinct beats");
        CHECK(due.size() > 1);
    }
}

TEST_CASE("modbus plan: the device roster is the set the blocks address") {
    PlanFixture f;
    std::set<uint8_t> from_blocks;
    for (uint16_t i = 0; i < f.p.block_count(); i++)
        from_blocks.insert(f.p.block(i).device);

    std::set<uint8_t> roster;
    for (uint8_t i = 0; i < f.p.device_count(); i++) roster.insert(f.p.device_at(i));

    // Health is per device and the client asks per block, so the two have to be
    // the same set: a device with no block is one nobody speaks to.
    CHECK(roster == from_blocks);

    for (uint16_t i = 0; i < f.p.block_count(); i++) {
        const Block& b = f.p.block(i);
        INFO("block " << i << " peer " << (int)b.peer);
        CHECK(b.peer < f.p.device_count());
        CHECK(f.p.device_at(b.peer) == b.device);
    }
    // Unit 0 is the broadcast address and never a peer to poll.
    for (uint8_t i = 0; i < f.p.device_count(); i++) CHECK(f.p.device_at(i) != 0);
}

TEST_CASE("modbus plan: the bus is not asked for more time than it has") {
    PlanFixture f;
    // Over 100% means the plan cannot keep its own periods: every block runs
    // late, for ever, and no single row looks wrong.
    INFO("duty " << f.p.duty_ppm() << " ppm, wire " << f.p.wire_duty_ppm() << " ppm");
    CHECK(f.p.duty_ppm() < 1000000u);
    CHECK(f.p.wire_duty_ppm() <= f.p.duty_ppm());
}

TEST_CASE("modbus plan: a faster bus merges across wider gaps") {
    // merge_gap is the fixed per-transaction cost priced in registers: bridging
    // a gap costs the useless registers, a second transaction costs a whole
    // turnaround. The faster the bus, the cheaper the registers are against
    // that fixed cost, so more gaps are worth bridging.
    CHECK(merge_gap(115200, 10000) > merge_gap(9600, 10000));
    CHECK(merge_gap(19200, 20000) > merge_gap(19200, 10000));

    PlanFixture fast(115200, 10);
    PlanFixture slow(9600, 10);

    // The same rows either way - the bus rate changes how they are grouped,
    // never which of them are read.
    REQUIRE(slow.p.row_count() == fast.p.row_count());
    INFO("9600: " << slow.p.block_count() << " blocks, "
         << slow.p.duty_ppm() << " ppm; 115200: " << fast.p.block_count()
         << " blocks, " << fast.p.duty_ppm() << " ppm");
    CHECK(fast.p.block_count() <= slow.p.block_count());
    CHECK(slow.p.duty_ppm() > fast.p.duty_ppm());
}

TEST_CASE("modbus plan: planning twice gives the same plan") {
    // plan() discards the previous one. A residue would show as blocks or rows
    // accumulating, which the bounded arrays would then start dropping.
    PlanFixture f;
    const uint16_t blocks = f.p.block_count();
    const uint16_t rows   = f.p.row_count();
    const uint8_t  devs   = f.p.device_count();

    f.p.plan(f.bank, 19200, 10, Site::g_site.devices);
    CHECK(f.p.block_count() == blocks);
    CHECK(f.p.row_count() == rows);
    CHECK(f.p.device_count() == devs);
    CHECK(f.p.rows_dropped() == 0);
    CHECK(f.p.rows_unscanned() == 0);
}

TEST_CASE("modbus plan: with no device table every unit gets the protocol's limits") {
    Vars::Bank bank;
    TestSite::build(bank);
    Planner p;
    // Null is legal and means "assume nothing": correct, and no tighter than it
    // has to be. It must still produce a scannable plan.
    p.plan(bank, 19200, 10, nullptr);
    CHECK(p.block_count() > 0);
    CHECK(p.rows_unscanned() == 0);
    for (uint16_t i = 0; i < p.block_count(); i++) {
        const Block& b = p.block(i);
        INFO("block " << i << " fc " << (int)b.fc << " x" << b.regs);
        CHECK(b.regs <= max_quantity(b.fc));
    }
}

#endif

}}  // namespace tests::modbus_plan
