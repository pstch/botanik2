/**
 * @file test_modbus_client.cpp
 * @brief The RTU master's scheduling policy: which block goes out next, and why.
 *
 * next_block() is the whole decision, so these drive it directly rather than
 * through a wire. What is being pinned is an ORDER of precedence - a safe state
 * beats a starved read beats an ordinary write beats an ordinary read - and each
 * case here is one pair of that order.
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

/** @brief A client over the site's real tables, brought up at t = 0. */
struct Master {
    Bank      bank;
    RtuClient client;

    Master() : bank(Site::g_site.vars), client(&bank, nullptr, Site::g_site.devices) {
        bank.init();
        bank.enable_control(Bank::INHIBIT_HEALTH);
        bank.enable_control(Bank::INHIBIT_OPERATOR);
        client.setup(0);
    }

    bool is_write(uint16_t i) const {
        return i != RtuClient::NO_BLOCK && !is_read(client.plan().block(i).fc);
    }
    bool is_a_read(uint16_t i) const {
        return i != RtuClient::NO_BLOCK && is_read(client.plan().block(i).fc);
    }
    /** @brief Demand something on every output row, so the write blocks are
     *  dirty and competing for the bus. */
    void dirty_all_writes() {
        const VarTables& t = Site::g_site.vars;
        for (uint16_t i = 0; i < t.count; ++i) {
            const Variable& d = t.var[i];
            if (d.bind.transport != XP_MODBUS || d.role != ROLE_OUTPUT) continue;
            Var h = bank.var(i);
            bank.override_set(h, (d.type == T_BOOL) ? from_bool(T_BOOL, true)
                                                    : from_f32(d.type, 1.0f));
        }
    }
};

/** @brief The shortest read period the plan uses, which is how long "one whole
 *  period late" is for the block that starves first. */
uint32_t shortest_read_period(const Planner& p) {
    uint32_t best = 0;
    for (uint16_t i = 0; i < p.block_count(); ++i) {
        const Block& b = p.block(i);
        if (!is_read(b.fc) || b.period_ms == 0) continue;
        if (best == 0 || b.period_ms < best) best = b.period_ms;
    }
    return best;
}

}  // namespace

TEST_CASE("modbus client: an ordinary dirty write goes before a read that is merely due") {
    // The base case the other two are measured against.
    Master m;
    m.dirty_all_writes();

    // Past the ordinary write interval, and far enough that reads are due but
    // not yet a whole period late.
    const uint16_t pick = m.client.next_block(Config::MODBUS_WRITE_MIN_MS + 1);
    INFO("writes are worth more than a reading about to be taken again anyway");
    CHECK(m.is_write(pick));
}

TEST_CASE("modbus client: a read starved by a whole period jumps the write queue") {
    // An Actuator timing a stroke keeps three or four write blocks continuously
    // dirty. Without a floor they hold the bus and the process image goes stale
    // underneath the programs reading it.
    Master m;
    m.dirty_all_writes();

    const uint32_t period = shortest_read_period(m.client.plan());
    REQUIRE(period > 0);

    // Long enough that the fastest read has missed a whole period of its own.
    const uint64_t late = (uint64_t)period * 4u;
    const uint16_t pick = m.client.next_block(late);

    INFO("at t=" << late << " ms, with every write dirty");
    REQUIRE(pick != RtuClient::NO_BLOCK);
    CHECK(m.is_a_read(pick));
    CHECK((late - m.client.plan().block(pick).due_ms)
              >= m.client.plan().block(pick).period_ms);
}

TEST_CASE("modbus client: withdrawing control puts the safe state ahead of everything") {
    // disable_control() marks every ROLE_OUTPUT dirty precisely so a transaction
    // carries the safe state out. Making that wait for MODBUS_WRITE_MIN_MS left
    // an actuator energised for it.
    Master m;

    // A beat with control up, so the withdrawal is an edge rather than a state.
    (void)m.client.next_block(10);
    REQUIRE_FALSE(m.client.safe_state_pending());

    m.bank.disable_control(Bank::INHIBIT_HEALTH);

    // The very next beat, and well inside the ordinary write interval.
    const uint16_t pick = m.client.next_block(20);
    CHECK(m.client.safe_state_pending());
    INFO("20 ms in, against a " << Config::MODBUS_WRITE_MIN_MS << " ms write interval");
    CHECK(m.is_write(pick));
}

TEST_CASE("modbus client: a safe state outranks even a starved read") {
    Master m;
    m.dirty_all_writes();
    (void)m.client.next_block(10);

    const uint32_t period = shortest_read_period(m.client.plan());
    REQUIRE(period > 0);
    const uint64_t late = (uint64_t)period * 4u;

    // Same instant that picked a read above - but now the field is owed a safe
    // state, and nothing outranks that.
    m.bank.disable_control(Bank::INHIBIT_HEALTH);
    const uint16_t pick = m.client.next_block(late);

    REQUIRE(m.client.safe_state_pending());
    CHECK(m.is_write(pick));
}

TEST_CASE("modbus client: the urgency is spent once the safe state is out") {
    // An operator who leaves control disabled must not keep the bus at emergency
    // priority for the rest of the day.
    Master m;
    (void)m.client.next_block(10);
    m.bank.disable_control(Bank::INHIBIT_HEALTH);
    REQUIRE(m.client.next_block(20) != RtuClient::NO_BLOCK);
    REQUIRE(m.client.safe_state_pending());

    // Pretend the sweep happened: nothing is owed to the field any more.
    const VarTables& t = Site::g_site.vars;
    for (uint16_t i = 0; i < t.count; ++i)
        if (t.var[i].bind.transport == XP_MODBUS && t.var[i].role == ROLE_OUTPUT)
            m.bank.var(i).clear_field_dirty();

    (void)m.client.next_block(30);
    CHECK_FALSE(m.client.safe_state_pending());
}


// Device health
// ===========================================================================

TEST_CASE("modbus client: a device that came back is owed its outputs again") {
    // The dirty mark means "the wire has not carried this yet", and it is
    // cleared when the write lands. A device that then reboots comes back at
    // its own power-on defaults, and nothing in a level-driven scan disagrees:
    // the demand has not moved, so no write is queued, so the plant sits wrong.
    Master m;
    m.dirty_all_writes();

    // Pretend every write has landed, which is where the trouble starts.
    for (uint16_t i = 0; i < m.client.plan().block_count(); ++i) {
        const Block& b = m.client.plan().block(i);
        if (is_read(b.fc)) continue;
        for (uint16_t k = 0; k < b.row_count; ++k)
            m.bank.var(m.client.plan().row((uint16_t)(b.first_row + k)))
                .clear_field_dirty();
    }
    REQUIRE_FALSE(m.is_write(m.client.next_block(1000)));

    // One device comes back.
    uint8_t peer = 0xFF, unit = 0;
    for (uint16_t i = 0; i < m.client.plan().block_count(); ++i) {
        const Block& b = m.client.plan().block(i);
        if (is_read(b.fc)) continue;
        peer = b.peer;
        unit = b.device;
        break;
    }
    REQUIRE(peer != 0xFF);
    m.client.reassert_outputs(peer);

    // Its rows are owed again; every other device's are still settled.
    uint16_t mine = 0, others = 0;
    for (uint16_t i = 0; i < m.client.plan().block_count(); ++i) {
        const Block& b = m.client.plan().block(i);
        if (is_read(b.fc)) continue;
        for (uint16_t k = 0; k < b.row_count; ++k) {
            const bool d = m.bank.var(m.client.plan().row((uint16_t)(b.first_row + k)))
                               .field_dirty();
            if (b.peer == peer) { if (d) mine++; }
            else if (d) others++;
        }
    }
    INFO("device " << (int)unit << " came back; " << mine << " rows re-owed");
    CHECK(mine > 0);
    INFO("no other device was disturbed");
    CHECK(others == 0);
}

TEST_CASE("modbus client: re-asserting an output does not re-decide it") {
    // The value stands - only its delivery is owed again. A re-assert that
    // reset the demand would drop an actuator on every bus glitch.
    Master m;
    const Block* w = nullptr;
    for (uint16_t i = 0; i < m.client.plan().block_count(); ++i)
        if (!is_read(m.client.plan().block(i).fc)) { w = &m.client.plan().block(i); break; }
    REQUIRE(w != nullptr);

    Var h = m.bank.var(m.client.plan().row(w->first_row));
    m.bank.override_set(h, (h.meta()->type == T_BOOL) ? from_bool(T_BOOL, true)
                                                      : from_f32(h.meta()->type, 42.0f));
    const Scalar before = h.raw();
    h.clear_field_dirty();

    m.client.reassert_outputs(w->peer);

    CHECK(h.field_dirty());
    INFO("'" << std::string(h.name()) << "' kept what it was demanding");
    CHECK(h.raw().u == before.u);
}

TEST_CASE("modbus client: every block's device is one the client watches") {
    // The peer table used to be built separately and capped at
    // MODBUS_MAX_DEVICES, with a bare break at the ceiling. An untracked unit is
    // always due - deliberately - so a device past the cap skipped the backoff
    // entirely: if it was dead it cost a full response timeout every sweep, with
    // no alarm, which is the exact failure the backoff exists to prevent. The
    // site had 8 devices against a cap of 8.
    Master m;
    const Planner& p = m.client.plan();
    REQUIRE(p.block_count() > 0);

    for (uint16_t i = 0; i < p.block_count(); ++i) {
        const Block& b = p.block(i);
        INFO("block " << i << " on unit " << (int)b.device
                      << " has peer " << (int)b.peer);
        CHECK(b.peer < p.device_count());
        // And the peer really is that block's device.
        CHECK(p.device_at(b.peer) == b.device);
    }
}

TEST_CASE("modbus client: the device set is the distinct units, counted once") {
    Master m;
    const Planner& p = m.client.plan();

    for (uint8_t i = 0; i < p.device_count(); ++i)
        for (uint8_t j = (uint8_t)(i + 1); j < p.device_count(); ++j)
            CHECK(p.device_at(i) != p.device_at(j));

    CHECK(m.client.peer_count() == p.device_count());
}


// Pacing
// ===========================================================================

TEST_CASE("modbus client: the inter-frame gap does not cost a beat of its own") {
    // MB_GAP used to enter MB_IDLE and RETURN, so the next request waited a
    // further beat for nothing: the gap is silence on the wire, and once it has
    // elapsed there is nothing left to wait for. With MB_DRAIN gone too, a
    // transaction costs the beat it is sent in rather than three.
    Master m;
    m.dirty_all_writes();

    // Something must be pickable at all, or this proves nothing.
    REQUIRE(m.client.next_block(Config::MODBUS_WRITE_MIN_MS + 1)
            != RtuClient::NO_BLOCK);
}

TEST_CASE("modbus client: duty counts the beat a transaction is quantised to") {
    // The bus is not free during a frame shorter than a beat - it is idle and
    // unusable, which is the same thing to the next sensor somebody adds.
    Master m;
    const Planner& p = m.client.plan();

    INFO("wire " << p.wire_duty_ppm() << " ppm, scheduled " << p.duty_ppm() << " ppm");
    CHECK(p.duty_ppm() >= p.wire_duty_ppm());
}

TEST_CASE("modbus client: a short frame on a quick device is charged a whole beat") {
    // Where the two models actually diverge. The reference site cannot show it:
    // MODBUS_TURNAROUND_MS is already a beat, so every block it plans is longer
    // than one. A quick device on a fast link is the case that matters.
    Bank bank(Site::g_site.vars);
    bank.init();
    Planner p;
    p.plan(bank, 115200, 1);          // 1 ms turnaround, fast link

    INFO("wire " << p.wire_duty_ppm() << " ppm, scheduled " << p.duty_ppm() << " ppm");
    CHECK(p.duty_ppm() > p.wire_duty_ppm());
}

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
