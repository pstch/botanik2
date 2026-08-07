/**
 * @file test_modbus_image.cpp
 * @brief The process image (modbus/rtu/image.h): a response becoming readings,
 *        and demands becoming a frame.
 *
 * Driven over the selected site's own table, because the interesting behaviour
 * is the interaction between a row's declared shape and the block the planner
 * cut it into - which a hand-made fixture would only pretend to have.
 */
#include <cstdint>
#include <cstring>
#include <string>

#include "doctest.h"

#include "libbotanik_site.h"
#include "libbotanik.h"

#include BOTANIK_SITE_HEADER

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

using namespace Botanik;
using namespace Botanik::Vars;
using namespace Botanik::Modbus;

namespace {

/** @brief A bank over the site's real tables, plus a plan over it. */
struct Bus {
    Bank    bank;
    Planner plan;
    Bus() : bank(Site::g_site.vars) {
        bank.init();
        bank.enable_control(Bank::INHIBIT_HEALTH);
        bank.enable_control(Bank::INHIBIT_OPERATOR);
        plan.plan(bank, 38400, 10);
    }
    /** @brief The first write block carrying registers, or carrying bits. The
     *  two encode differently and both have to be pinned. */
    const Block* first_write(bool bits) const {
        for (uint16_t i = 0; i < plan.block_count(); ++i) {
            const Block& b = plan.block(i);
            if (!is_read(b.fc) && is_bit(b.fc) == bits) return &b;
        }
        return nullptr;
    }
    /** @brief Demand @p x on row @p k of @p b, in whatever type the row is. A
     *  bool written into an F32 row lands as a denormal, which reads back as the
     *  same zero either way and quietly makes a comparison test pass. */
    void demand(const Block& b, uint16_t k, float x) {
        Var h = bank.var(plan.row((uint16_t)(b.first_row + k)));
        bank.override_set(h, (h.meta()->type == T_BOOL)
                                 ? from_bool(T_BOOL, x != 0.0f)
                                 : from_f32(h.meta()->type, x));
    }
    Var row(const Block& b, uint16_t k) {
        return bank.var(plan.row((uint16_t)(b.first_row + k)));
    }
};

}  // namespace

TEST_CASE("modbus image: a demand that moves in flight stays owed") {
    // The regression. collect_write() runs when the frame is built and
    // clear_dirty() when the echo comes back, several beats later. A program
    // that moves a demand in between marked a row dirty for a value this frame
    // never carried - and clearing the block wholesale dropped it until the
    // rewrite interval, five seconds of a stale command on an actuator.
    for (int pass = 0; pass < 2; ++pass) {
        const bool bits = (pass == 1);
        Bus bus;
        const Block* b = bus.first_write(bits);
        if (b == nullptr || b->row_count < 2) continue;
        CAPTURE(bits);

        // Two demands go out together.
        bus.demand(*b, 0, 1.0f);
        bus.demand(*b, 1, 1.0f);
        REQUIRE(block_dirty(bus.bank, bus.plan, *b));

        uint8_t sent[256];
        const uint16_t n = collect_write(bus.bank, bus.plan, *b, sent, sizeof sent);
        REQUIRE(n > 0);

        // ...and while the transaction is in flight, one of them moves.
        bus.demand(*b, 1, 0.0f);

        clear_dirty(bus.bank, bus.plan, *b, sent, n);

        INFO("'" << std::string(bus.row(*b, 0).meta()->name)
                 << "' went out in this frame");
        CHECK_FALSE(bus.row(*b, 0).field_dirty());
        INFO("'" << std::string(bus.row(*b, 1).meta()->name)
                 << "' changed after it was built");
        CHECK(bus.row(*b, 1).field_dirty());
    }
}

TEST_CASE("modbus image: a block whose demands held still clears whole") {
    Bus bus;
    const Block* b = bus.first_write(false);
    REQUIRE(b != nullptr);

    for (uint16_t k = 0; k < b->row_count; ++k) bus.demand(*b, k, 1.0f);

    uint8_t sent[256];
    const uint16_t n = collect_write(bus.bank, bus.plan, *b, sent, sizeof sent);
    REQUIRE(n > 0);
    clear_dirty(bus.bank, bus.plan, *b, sent, n);

    CHECK_FALSE(block_dirty(bus.bank, bus.plan, *b));
}

TEST_CASE("modbus image: with no payload to compare, the block clears wholesale") {
    // The caller could not say what went out, so the old behaviour stands - it
    // must not silently stop clearing and rewrite for ever.
    Bus bus;
    const Block* b = bus.first_write(false);
    REQUIRE(b != nullptr);

    for (uint16_t k = 0; k < b->row_count; ++k) bus.demand(*b, k, 1.0f);
    REQUIRE(block_dirty(bus.bank, bus.plan, *b));

    clear_dirty(bus.bank, bus.plan, *b, nullptr, 0);
    CHECK_FALSE(block_dirty(bus.bank, bus.plan, *b));
}

TEST_CASE("modbus image: an active-low coil is inverted on the way out") {
    Bus bus;
    const Block* b = bus.first_write(true);
    if (b == nullptr) return;   // no coil block on this site

    // Find a row on this block that declares F_INVERT, if the site has one.
    const Variable* inv = nullptr;
    uint16_t inv_off = 0;
    for (uint16_t k = 0; k < b->row_count; ++k) {
        const Variable* d = bus.bank.var(bus.plan.row((uint16_t)(b->first_row + k))).meta();
        if (d->flags & F_INVERT) { inv = d; inv_off = (uint16_t)(d->bind.reg - b->reg); break; }
    }
    if (inv == nullptr) return;   // nothing to check on this site

    bus.bank.override_set(bus.bank.var(bus.bank.find(inv->name)),
                          from_bool(T_BOOL, true));

    uint8_t sent[256];
    const uint16_t n = collect_write(bus.bank, bus.plan, *b, sent, sizeof sent);
    REQUIRE(n > 0);

    INFO("'" << std::string(inv->name) << "' is active-low");
    CHECK_FALSE(bit_at(sent, inv_off));
}

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
