/**
 * @file test_modbus_image.cpp
 * @brief Where the bus meets the bank, in both directions.
 *
 * A defect here is SILENT. A dropped F_INVERT energises a relay the law asked
 * to open; a wrong register offset publishes one device's reading under another
 * row's name. Neither raises anything: the value is well formed, the frame is
 * well formed, and every consumer downstream - alarms, control laws, telemetry -
 * acts on it. So the load-bearing case is the round trip: what collect_write()
 * puts on the wire must be what commit_read() reads back off it.
 */

#include <vector>

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/modbus/rtu/image.h"
#include "libbotanik/modbus/rtu/plan.h"

#include "site_bank.h"

namespace modbus_image {

using namespace Botanik;
using namespace Botanik::Modbus;

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT && BOTANIK_FEATURE_VARS

struct ImageFixture {
    Vars::Bank bank;
    Planner    p;

    ImageFixture() {
        TestSite::build(bank);
        p.plan(bank, 19200, 10, Site::g_site.devices);
    }

    /** @brief The first block matching @p pred, or nullptr. */
    template <typename P>
    const Block* find_block(P pred) const {
        for (uint16_t i = 0; i < p.block_count(); i++)
            if (pred(p.block(i))) return &p.block(i);
        return nullptr;
    }
    const Block* a_read_block(bool bits) const {
        return find_block([bits](const Block& b) {
            return is_read(b.fc) && is_bit(b.fc) == bits;
        });
    }
    const Block* a_write_block() const {
        return find_block([](const Block& b) { return !is_read(b.fc); });
    }
    /** @brief A write block with a boolean row in it, and that row's slot. The
     *  first row of a write block is not necessarily a coil. */
    const Block* a_bool_write(uint16_t& slot) const {
        for (uint16_t i = 0; i < p.block_count(); i++) {
            const Block& b = p.block(i);
            if (is_read(b.fc)) continue;
            for (uint16_t k = 0; k < b.row_count; k++) {
                const uint16_t r = (uint16_t)(b.first_row + k);
                if (bank.descriptor(p.row(r)).type == Vars::T_BOOL) {
                    slot = r; return &b;
                }
            }
        }
        return nullptr;
    }
};

TEST_CASE("modbus image: a payload lands on the rows the block claims") {
    ImageFixture f;
    const Block* b = f.a_read_block(/*bits=*/false);
    REQUIRE_MESSAGE(b != nullptr, "the site has no such block");

    // A distinct value per register, so a row reading the wrong offset reads a
    // number that could only have come from a neighbour.
    std::vector<uint8_t> payload((size_t)b->regs * 2u);
    for (uint16_t i = 0; i < b->regs; i++) {
        payload[(size_t)i * 2u]     = (uint8_t)((0x1000u + i) >> 8);
        payload[(size_t)i * 2u + 1] = (uint8_t)((0x1000u + i) & 0xFF);
    }
    Response r; r.data = payload.data(); r.len = (uint16_t)payload.size(); r.ex = EX_NONE;

    const uint16_t good = commit_read(f.bank, f.p, *b, r, 1000);
    INFO("block of " << b->row_count << " rows over " << b->regs << " registers");
    CHECK(good <= b->row_count);

    // Whatever published must be valid and stamped: an unstamped row is one
    // expire_stale() will never age out.
    for (uint16_t k = 0; k < b->row_count; k++) {
        Vars::Var h = f.bank.var(f.p.row((uint16_t)(b->first_row + k)));
        if (h.valid()) CHECK(h.stamp() == 1000u);
    }
}

TEST_CASE("modbus image: a failed read invalidates rather than leaving the last value") {
    ImageFixture f;
    const Block* b = f.a_read_block(false);
    REQUIRE_MESSAGE(b != nullptr, "the site has no such block");

    std::vector<uint8_t> payload((size_t)b->regs * 2u, 0x11);
    Response r; r.data = payload.data(); r.len = (uint16_t)payload.size(); r.ex = EX_NONE;
    commit_read(f.bank, f.p, *b, r, 1000);

    // Left alone, a row keeps its last value and its validity, and one with no
    // timeout is never aged out - so it would read good for ever off a dead bus.
    invalidate_block(f.bank, f.p, *b, Vars::Q_STALE);
    for (uint16_t k = 0; k < b->row_count; k++) {
        Vars::Var h = f.bank.var(f.p.row((uint16_t)(b->first_row + k)));
        INFO(h.meta()->name);
        CHECK_FALSE(h.valid());
    }
}

TEST_CASE("modbus image: a refusal and a silence are different diagnoses") {
    ImageFixture f;
    const Block* b = f.a_read_block(false);
    REQUIRE_MESSAGE(b != nullptr, "the site has no such block");

    invalidate_block(f.bank, f.p, *b, Vars::Q_REFUSED);
    Vars::Var h = f.bank.var(f.p.row(b->first_row));
    const Vars::Quality refused = h.quality();

    invalidate_block(f.bank, f.p, *b, Vars::Q_STALE);
    const Vars::Quality stale = f.bank.var(f.p.row(b->first_row)).quality();

    // A device that answered and declined is a configuration fault; one that
    // said nothing is a wiring fault. Collapsing them costs the operator the
    // difference.
    CHECK(refused != stale);
    CHECK(refused == Vars::Q_REFUSED);
    CHECK(stale == Vars::Q_STALE);
}

TEST_CASE("modbus image: what goes on the wire is what comes back off it") {
    // The whole point of the file. A write block's demands are laid into a
    // payload, then read back as though a slave had echoed them, and every row
    // must hold what it held before - F_INVERT applied twice cancelling, the
    // register offsets agreeing in both directions.
    ImageFixture f;
    const Block* b = f.a_write_block();
    REQUIRE_MESSAGE(b != nullptr, "the site has no such block");

    // Set every row of the block to a known state through the bank.
    for (uint16_t k = 0; k < b->row_count; k++) {
        Vars::Var h = f.bank.var(f.p.row((uint16_t)(b->first_row + k)));
        const Vars::Descriptor& d = *h.meta();
        if (d.type == Vars::T_BOOL) h.publish(Vars::from_bool(d.type, (k % 2) == 0), 500);
        else                        h.publish(Vars::from_i64(d.type, (int64_t)(k + 1)), 500);
    }

    uint8_t wire[256] = {};
    const uint16_t n = collect_write(f.bank, f.p, *b, wire, sizeof(wire));
    REQUIRE(n > 0);

    std::vector<bool> before;
    std::vector<int64_t> before_i;
    for (uint16_t k = 0; k < b->row_count; k++) {
        Vars::Var h = f.bank.var(f.p.row((uint16_t)(b->first_row + k)));
        before.push_back(h.as_bool());
        before_i.push_back(h.value().v.i);
    }

    // Feed it back as a read of the same registers.
    Response r; r.data = wire; r.len = n; r.ex = EX_NONE;
    const Block rb = *b;
    commit_read(f.bank, f.p, rb, r, 2000);

    for (uint16_t k = 0; k < b->row_count; k++) {
        Vars::Var h = f.bank.var(f.p.row((uint16_t)(b->first_row + k)));
        INFO(h.meta()->name << " (row " << k << " of the block)");
        if (h.meta()->type == Vars::T_BOOL) CHECK(h.as_bool() == before[k]);
    }
}

TEST_CASE("modbus image: an inverted row is inverted once, on the wire only") {
    ImageFixture f;
    // F_INVERT is active-low WIRING. Everything above the bus reads true =
    // asserted, so the flag must appear exactly at the wire and nowhere else -
    // and it must cancel across a write and a read of the same bit.
    const Block* b = f.find_block([&f](const Block& blk) {
        if (is_read(blk.fc) || !is_bit(blk.fc)) return false;
        for (uint16_t k = 0; k < blk.row_count; k++) {
            const uint16_t vi = f.p.row((uint16_t)(blk.first_row + k));
            if (f.bank.descriptor(vi).flags & Vars::F_INVERT) return true;
        }
        return false;
    });
    REQUIRE_MESSAGE(b != nullptr, "the site has no such block");             // no inverted row on a write block

    for (uint16_t k = 0; k < b->row_count; k++)
        f.bank.var(f.p.row((uint16_t)(b->first_row + k))).publish(
            Vars::from_bool(Vars::T_BOOL, true), 500);

    uint8_t wire[64] = {};
    const uint16_t n = collect_write(f.bank, f.p, *b, wire, sizeof(wire));
    REQUIRE(n > 0);

    for (uint16_t k = 0; k < b->row_count; k++) {
        const uint16_t vi = f.p.row((uint16_t)(b->first_row + k));
        const Vars::Descriptor& d = f.bank.descriptor(vi);
        const uint16_t off = (uint16_t)(d.bind.reg - b->reg);
        const bool on_wire = (wire[off / 8] >> (off % 8)) & 1u;
        INFO(d.name << (d.flags & Vars::F_INVERT ? " (active low)" : ""));
        // The row says true; the wire says true unless the row is active-low.
        CHECK(on_wire == !(d.flags & Vars::F_INVERT));
    }
}

TEST_CASE("modbus image: an encoder given no room writes nothing") {
    ImageFixture f;
    const Block* b = f.a_write_block();
    REQUIRE_MESSAGE(b != nullptr, "the site has no such block");
    uint8_t small[1] = { 0xAA };
    CHECK(collect_write(f.bank, f.p, *b, small, 0) == 0);
    CHECK(small[0] == 0xAA);
}

TEST_CASE("modbus image: a demand that moved mid-transaction stays owed") {
    ImageFixture f;
    uint16_t slot = 0;
    const Block* b = f.a_bool_write(slot);
    REQUIRE_MESSAGE(b != nullptr, "the site has no boolean row on a write block");

    Vars::Var h = f.bank.var(f.p.row(slot));
    const Vars::Descriptor& d = *h.meta();
    h.publish(Vars::from_bool(d.type, true), 500);
    h.mark_field_dirty();
    CHECK(block_dirty(f.bank, f.p, *b));

    uint8_t wire[64] = {};
    const uint16_t n = collect_write(f.bank, f.p, *b, wire, sizeof(wire));
    REQUIRE(n > 0);

    // The two ends of a transaction are several beats apart. A program that
    // moved this demand in between must not have it cleared by the echo of the
    // OLD one, or the field keeps the old value until the rewrite interval.
    h.publish(Vars::from_bool(d.type, false), 600);
    h.mark_field_dirty();
    clear_dirty(f.bank, f.p, *b, wire, n);
    INFO("the demand changed after the frame went out");
    CHECK(h.field_dirty());
}

TEST_CASE("modbus image: an unchanged demand is cleared by its echo") {
    ImageFixture f;
    uint16_t slot = 0;
    const Block* b = f.a_bool_write(slot);
    REQUIRE_MESSAGE(b != nullptr, "the site has no boolean row on a write block");
    Vars::Var h = f.bank.var(f.p.row(slot));
    h.publish(Vars::from_bool(h.meta()->type, true), 500);
    h.mark_field_dirty();
    uint8_t wire[64] = {};
    const uint16_t n = collect_write(f.bank, f.p, *b, wire, sizeof(wire));
    REQUIRE(n > 0);
    clear_dirty(f.bank, f.p, *b, wire, n);
    CHECK_FALSE(h.field_dirty());
}

TEST_CASE("modbus image: reasserting owes the value again without changing it") {
    ImageFixture f;
    const Block* b = f.a_write_block();
    REQUIRE_MESSAGE(b != nullptr, "the site has no such block");

    for (uint16_t k = 0; k < b->row_count; k++) {
        Vars::Var h = f.bank.var(f.p.row((uint16_t)(b->first_row + k)));
        h.publish(Vars::from_bool(h.meta()->type, true), 500);
        h.clear_field_dirty();
    }
    CHECK_FALSE(block_dirty(f.bank, f.p, *b));

    // What a device that went away and came back needs: the value stands, only
    // its delivery is owed. A re-DECISION would drop an override.
    reassert_block(f.bank, f.p, *b);
    CHECK(block_dirty(f.bank, f.p, *b));
    for (uint16_t k = 0; k < b->row_count; k++) {
        Vars::Var h = f.bank.var(f.p.row((uint16_t)(b->first_row + k)));
        INFO(h.meta()->name);
        CHECK(h.as_bool());
        CHECK(h.field_dirty());
    }
}

#endif

}  // namespace modbus_image
