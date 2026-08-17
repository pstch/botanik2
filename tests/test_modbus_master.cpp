/**
 * @file test_modbus_master.cpp
 * @brief The RTU master: block planning, the device table, and the register image.
 *
 * Each section below is the former test_<name>.cpp, kept in its own
 * namespace so their fixtures and helpers cannot collide.
 */

#include <cstdint>
#include <cstring>
#include <string>
#include "doctest.h"
#include "libbotanik_site.h"
#include "libbotanik.h"
#include BOTANIK_SITE_HEADER
#include "site_bank.h"
#if BOTANIK_FEATURE_MODBUS_RTU_SERVER && defined(TARGET_SIMULATOR)
#include <unistd.h>
#include "botanik_sim/fieldbus.h"
#include "libbotanik/hal/simulator.h"

#endif

namespace {
    /** @brief One frozen bank over the site's rows, shared by the read-only
     *  cases below. Static: the rows are flash either way. */
    const Botanik::Vars::Bank& site_bank() {
        static Botanik::Vars::Bank b;
        static bool once = (TestSite::build(b), true);
        (void)once;
        return b;
    }
}

// modbus_client

namespace modbus_client {

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

using namespace Botanik;
using namespace Botanik::Vars;
using namespace Botanik::Modbus;


namespace {

/** @brief A client over the site's real tables, brought up at t = 0. */
struct Master {
    Bank      bank;
    RtuClient client;

    Master() : client(Botanik::SetupContext(), &bank, Site::g_site.devices) {
        TestSite::build(bank);
        bank.set_health_hold(false);
        bank.set_control_mode(CONTROL_ENABLED);
        client.setup(0);
    }

    bool is_write(uint16_t i) const {
        return i != RtuClient::NO_BLOCK && !is_read(client.plan().block(i).fc);
    }
    bool is_a_read(uint16_t i) const {
        return i != RtuClient::NO_BLOCK && is_read(client.plan().block(i).fc);
    }
    /** @brief Dirties every output row so the write blocks compete for the bus. */
    void dirty_all_writes() {
        for (uint16_t i = 0; i < bank.count(); ++i) {
            const Descriptor& d = bank.descriptor(i);
            if (d.bind.transport != XP_MODBUS || d.role != ROLE_OUTPUT) continue;
            Var h = bank.var(i);
            bank.override_set(h, (d.type == T_BOOL) ? from_bool(T_BOOL, true)
                                                    : from_f32(d.type, 1.0f));
        }
    }
};

/** @brief The plan's shortest read period: one whole period late for the block that starves first. */
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

// Ranking
//
// One clock: every block is ranked on how far it is through the lateness it is
// allowed, in permille. These two pin that it cuts BOTH ways, which is the whole
// of it - under the old tier ladder the first was decided by kind, and the
// second could not happen at all.

TEST_CASE("modbus client: a read that is due outranks a demand only just asked for") {
    // 1 ms into a 500 ms delivery deadline is 2 permille; a read a fifth through
    // its period is a hundred times that.
    Master m;
    m.dirty_all_writes();

    const uint16_t pick = m.client.next_block(Config::MODBUS_WRITE_MIN_MS + 1);
    INFO("a demand 1 ms past its floor is not yet owed anything");
    CHECK(m.is_a_read(pick));
}

TEST_CASE("modbus client: a demand past its deadline outranks a read that is late") {
    Master m;                                    // ...and the other way
    m.dirty_all_writes();

    const uint32_t period = shortest_read_period(m.client.plan());
    REQUIRE(period > 0);
    // Needs the fastest rung at >= 1 s: a 200 ms rung climbs at 5 permille/ms
    // against a 500 ms deadline's 2, so no static instant shows the write ahead.
    // Non-starvation still holds dynamically there (the live-bus case measures it).
    INFO("the site's fastest read rung is " << period << " ms");
    REQUIRE(period >= 1000);

    // 1.8 s into a 500 ms deadline is 3600 permille; the fastest read is 2 s into
    // a 1 s period, so 2000.
    const uint16_t pick = m.client.next_block(2000);
    REQUIRE(pick != RtuClient::NO_BLOCK);
    CHECK(m.is_write(pick));
}

TEST_CASE("modbus client: withdrawing control puts the safe state ahead of everything") {
    // Leaving CONTROL_ENABLED dirties every ROLE_OUTPUT so the safe state ships at once, not after MODBUS_WRITE_MIN_MS.
    Master m;

    // A beat with control up, so the withdrawal is an edge rather than a state.
    (void)m.client.next_block(10);
    REQUIRE_FALSE(m.client.safe_state_pending());

    m.bank.set_health_hold(true);

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

    // Same instant as above, but now a safe state is owed and outranks it.
    m.bank.set_health_hold(true);
    const uint16_t pick = m.client.next_block(late);

    REQUIRE(m.client.safe_state_pending());
    CHECK(m.is_write(pick));
}

TEST_CASE("modbus client: the urgency is spent once the safe state is out") {
    // Leaving control disabled must not keep the bus at emergency priority forever.
    Master m;
    (void)m.client.next_block(10);
    m.bank.set_health_hold(true);
    REQUIRE(m.client.next_block(20) != RtuClient::NO_BLOCK);
    REQUIRE(m.client.safe_state_pending());

    // Pretend the sweep ran: nothing is owed to the field.
    for (uint16_t i = 0; i < m.bank.count(); ++i)
        if (m.bank.descriptor(i).bind.transport == XP_MODBUS && m.bank.descriptor(i).role == ROLE_OUTPUT)
            m.bank.var(i).clear_field_dirty();

    (void)m.client.next_block(30);
    CHECK_FALSE(m.client.safe_state_pending());
}


// Device health

TEST_CASE("modbus client: a device that came back is owed its outputs again") {
    // A rebooted device returns to power-on defaults; nothing re-dirties it since the demand hasn't moved, so the plant sits wrong.
    Master m;
    m.dirty_all_writes();

    // Pretend every write has landed - where the trouble starts.
    for (uint16_t i = 0; i < m.client.plan().block_count(); ++i) {
        const Block& b = m.client.plan().block(i);
        if (is_read(b.fc)) continue;
        for (uint16_t k = 0; k < b.row_count; ++k)
            m.bank.var(m.client.plan().row((uint16_t)(b.first_row + k)))
                .clear_field_dirty();
    }
    REQUIRE_FALSE(m.is_write(m.client.next_block(1000)));

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

    // Its rows are re-owed; every other device's stay settled.
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
    // The value stands; only delivery is re-owed, or a bus glitch would reset every actuator's demand.
    Master m;
    const Block* w = nullptr;
    for (uint16_t i = 0; i < m.client.plan().block_count(); ++i)
        if (!is_read(m.client.plan().block(i).fc)) { w = &m.client.plan().block(i); break; }
    REQUIRE(w != nullptr);

    Var h = m.bank.var(m.client.plan().row(w->first_row));
    m.bank.override_set(h, (h.meta()->type == T_BOOL) ? from_bool(T_BOOL, true)
                                                      : from_f32(h.meta()->type, 42.0f));
    const Scalar before = h.value().v;
    h.clear_field_dirty();

    m.client.reassert_outputs(w->peer);

    CHECK(h.field_dirty());
    INFO("'" << std::string(h.name()) << "' kept what it was demanding");
    CHECK(h.value().v.u == before.u);
}

// Pacing

TEST_CASE("modbus client: duty counts the beat a transaction is quantised to") {
    // A frame shorter than a beat still occupies the bus; idle time is unusable to the next sensor added.
    Master m;
    const Planner& p = m.client.plan();

    INFO("wire " << p.wire_duty_ppm() << " ppm, scheduled " << p.duty_ppm() << " ppm");
    CHECK(p.duty_ppm() >= p.wire_duty_ppm());
}

TEST_CASE("modbus client: a short frame on a quick device is charged a whole beat") {
    // Where wire and scheduled duty diverge; the reference site can't show it (MODBUS_TURNAROUND_MS is already a beat), so this uses a fast link.
    Bank bank;
    TestSite::build(bank);
    Planner p;
    p.plan(bank, 115200, 1);          // 1 ms turnaround, fast link

    INFO("wire " << p.wire_duty_ppm() << " ppm, scheduled " << p.duty_ppm() << " ppm");
    CHECK(p.duty_ppm() > p.wire_duty_ppm());
}

TEST_CASE("modbus client: control returning spends the safe-state urgency") {
    // Otherwise the flag's lifetime depends on the sweep finding no dirty write,
    // which a block on a backed-off device makes impossible: see the rising-edge
    // half of _track_control().
    Master m;

    (void)m.client.next_block(10);                       // control up: an edge to fall from
    m.bank.set_control_mode(CONTROL_DISABLED);
    (void)m.client.next_block(20);
    REQUIRE(m.client.safe_state_pending());

    m.bank.set_control_mode(CONTROL_ENABLED);
    (void)m.client.next_block(30);
    INFO("with control back the programs drive the outputs again");
    CHECK_FALSE(m.client.safe_state_pending());
}

// The failure paths, against devices that really answer
//
// Sim::FieldBus stands a real RtuSlave up per unit the site addresses and
// carries framed bytes over the socketpair, so these drive the master's own
// state machine rather than a mock of it. What they need beyond that is the
// ability to make ONE unit misbehave, which is Sim::FieldBus::set_fault().

#if BOTANIK_FEATURE_MODBUS_RTU_SERVER && defined(TARGET_SIMULATOR)

namespace {

/** @brief A master with the far end of the wire actually connected. */
struct LiveBus {
    Bank          bank;
    RtuClient     client;
    Sim::FieldBus bus;
    uint64_t      t = 0;

    explicit LiveBus(const DeviceTable* devs = Site::g_site.devices)
        : client(Botanik::SetupContext(), &bank, devs) {
        TestSite::build(bank);
        // Both halves: the system's hold defaults to held, so the operator's
        // mode alone leaves the effective mode DISABLED.
        bank.set_health_hold(false);
        bank.set_control_mode(CONTROL_ENABLED);
        // The socketpair is one per PROCESS, so a previous case's unread request
        // bytes are still in the peer's buffer and would be answered as if they
        // were ours. setup() drains the master's end; this drains the devices'.
        const int fd = HAL::Serial::fieldbus_peer_fd();
        if (fd >= 0) { uint8_t junk[512]; while (::read(fd, junk, sizeof junk) > 0) {} }
        client.setup(0);
        // Baud 0 drops the simulator's host-clock byte pacing: the beat below is
        // virtual, so a paced line reads as permanently busy and tx_idle() lets
        // one transaction out per beat. (BOTANIK_SIM_CLOCK=manual is the general
        // fix, but it is per-process and the rest of this binary wants the host
        // clock.)
        HAL::Serial::fieldbus_begin(0, 8, HAL::Serial::PARITY_NONE, 1);
        bus.bind(&bank);
    }

    /** @brief One beat, in the phase order firmware.cpp uses. */
    void beat(uint32_t dt = 10) {
        t += dt;
        client.step(t, Botanik::Tasks::PHASE_INPUT);
        bank.expire_stale(t);     // ...and it runs ahead of this, as in firmware.cpp
        client.step(t, Botanik::Tasks::PHASE_OUTPUT);
        bus.step(t);              // and the far end
    }
    void beats(uint32_t n) { for (uint32_t i = 0; i < n; ++i) beat(); }

    /** @brief Owe every output to the field again, i.e. what a control program
     *  that moved its demand looks like to the collector. */
    void dirty_all_writes() {
        for (uint8_t p = 0; p < client.peer_count(); ++p) client.reassert_outputs(p);
    }

    /** @brief Peer index of @p unit, or NO_PEER. */
    uint8_t peer_of(uint8_t unit) const {
        for (uint8_t p = 0; p < client.peer_count(); ++p)
            if (client.peer_unit(p) == unit) return p;
        return RtuClient::NO_PEER;
    }

    /** @brief The fastest read block, which is the one that starves first. */
    uint16_t fastest_read() const {
        uint16_t best = RtuClient::NO_BLOCK;
        for (uint16_t i = 0; i < client.plan().block_count(); ++i) {
            const Block& b = client.plan().block(i);
            if (!is_read(b.fc) || b.period_ms == 0) continue;
            if (best == RtuClient::NO_BLOCK ||
                b.period_ms < client.plan().block(best).period_ms) best = i;
        }
        return best;
    }

    /** @brief Peer index of the first device carrying a write block, and the
     *  block itself. Both are what an output device means to the client. */
    uint16_t write_block() const {
        for (uint16_t i = 0; i < client.plan().block_count(); ++i)
            if (!is_read(client.plan().block(i).fc)) return i;
        return RtuClient::NO_BLOCK;
    }
    /** @brief Run until @p pred holds or @p limit beats have passed. */
    template <typename F> bool until(F pred, uint32_t limit = 2000) {
        for (uint32_t i = 0; i < limit; ++i) { if (pred()) return true; beat(); }
        return pred();
    }
};

}  // namespace

TEST_CASE("modbus fault: an absent unit is backed off and the rest keep answering") {
    // The harness itself, and the baseline the two cases below stand on: if this
    // fails they are testing nothing.
    LiveBus m;
    const uint16_t w = m.write_block();
    REQUIRE(w != RtuClient::NO_BLOCK);
    const uint8_t peer = m.client.plan().block(w).peer;

    m.bus.set_fault(m.client.peer_unit(peer), Sim::FieldBus::FAULT_ABSENT);
    m.client.reassert_outputs(peer);   // owe the block, so the misses come at the write interval
    REQUIRE(m.until([&] { return m.client.peer_dead(peer); }));

    const uint32_t before = m.client.txns();
    m.beats(300);
    INFO("dev " << (unsigned)m.client.peer_unit(peer) << " is out of the rotation");
    CHECK(m.client.txns() > before);          // the live devices never stopped
    CHECK(m.client.dead_count() == 1);
}

TEST_CASE("modbus live: a modulating output and the read image share the bus") {
    // Neither side may starve the other, and only a run shows it: both urgencies
    // grow and both reset on service.
    LiveBus m;
    const uint16_t w = m.write_block();
    const uint16_t r = m.fastest_read();
    REQUIRE(w != RtuClient::NO_BLOCK);
    REQUIRE(r != RtuClient::NO_BLOCK);

    m.beats(200);                                  // let the sweep settle

    uint64_t rlast = m.client.plan().block(r).last_ms;
    uint64_t wlast = m.client.plan().block(w).last_ms;
    uint32_t reads = 0, writes = 0;
    for (uint32_t i = 0; i < 800; ++i) {           // 8 s of beats
        if (i % 50 == 0) m.dirty_all_writes();     // the demand moves twice a second
        m.beat();
        const uint64_t rn = m.client.plan().block(r).last_ms;
        const uint64_t wn = m.client.plan().block(w).last_ms;
        if (rn != rlast) { reads++;  rlast = rn; }
        if (wn != wlast) { writes++; wlast = wn; }
    }

    INFO(reads << " polls of the fastest read block and " << writes
               << " deliveries of a continuously moving demand, over 8 s");
    CHECK(reads  >= 5);    // 7 measured, against a nominal 8 for a 1 s rung
    CHECK(writes >= 10);   // 16 measured, one per time the demand moved
}

TEST_CASE("modbus probe: six dead devices cannot take the bus between them") {
    // One suspect at a time behind MODBUS_PROBE_SPACING_MS bounds the bus a dead
    // spur can take at timeout/(timeout + spacing), however many are down.
    LiveBus m;
    const uint16_t r = m.fastest_read();
    REQUIRE(r != RtuClient::NO_BLOCK);
    const uint8_t keep = m.client.plan().block(r).peer;

    uint8_t killed = 0;
    for (uint8_t p = 0; p < m.client.peer_count() && killed < 6; ++p) {
        if (p == keep) continue;
        m.bus.set_fault(m.client.peer_unit(p), Sim::FieldBus::FAULT_ABSENT);
        killed++;
    }
    REQUIRE(killed == 6);

    // A device that has never missed is presumed good, so a FIRST failure cannot
    // be gated: six of them cost six timeouts, once. The bound is on steady state.
    const uint64_t t0 = m.t;
    REQUIRE(m.until([&] { return m.client.dead_count() == 6; }, 6000));
    const uint64_t discovery_ms = m.t - t0;

    uint32_t polls = 0, faults = m.client.faults();
    const uint32_t faults0 = faults;
    uint64_t rlast = m.client.plan().block(r).last_ms;
    uint64_t prev_fault_ms = 0;
    uint64_t min_gap = 0xFFFFFFFFull;
    for (uint32_t i = 0; i < 3000; ++i) {          // 30 s of steady state
        m.beat();
        if (m.client.faults() != faults) {
            faults = m.client.faults();
            if (prev_fault_ms != 0) {
                const uint64_t gap = m.t - prev_fault_ms;
                if (gap < min_gap) min_gap = gap;
            }
            prev_fault_ms = m.t;
        }
        const uint64_t rn = m.client.plan().block(r).last_ms;
        if (rn != rlast) { polls++; rlast = rn; }
    }
    const uint32_t probes = faults - faults0;

    INFO("discovery took " << discovery_ms << " ms; then " << probes
         << " probes in 30 s, closest pair " << min_gap
         << " ms apart, live block polled " << polls << " times");
    // No two unanswered transactions inside one spacing interval. That IS the
    // bound, and it does not depend on six being the number.
    CHECK(min_gap >= Config::MODBUS_PROBE_SPACING_MS);
    // Which caps what the dead can take: six of them, at a 300 ms timeout each.
    CHECK(probes * Config::MODBUS_TIMEOUT_MS <= 30000u / 4u);
    // And the live device never stopped being served.
    CHECK(polls >= 24);      // 28 measured, against a nominal 30
    CHECK(m.client.dead_count() == 6);
}

TEST_CASE("modbus probe: the probes go round the dead devices, not at one of them") {
    // A cursor rather than lateness deciding: without it the rotation is a
    // by-product of which suspect happens to be latest, and one device can be
    // probed over and over while another is never revisited.
    LiveBus m;
    const uint16_t r = m.fastest_read();
    REQUIRE(r != RtuClient::NO_BLOCK);
    const uint8_t keep = m.client.plan().block(r).peer;

    uint8_t killed = 0;
    for (uint8_t p = 0; p < m.client.peer_count() && killed < 4; ++p) {
        if (p == keep) continue;
        m.bus.set_fault(m.client.peer_unit(p), Sim::FieldBus::FAULT_ABSENT);
        killed++;
    }

    // Long enough for every one of them to have been visited twice over.
    m.beats(3000);

    uint8_t probed = 0;
    for (uint8_t p = 0; p < m.client.peer_count(); ++p)
        if (m.client.peer_misses(p) > 0) probed++;

    INFO(probed << " of the 4 absent devices have misses against them");
    CHECK(probed == 4);
    CHECK(m.client.dead_count() == 4);
}

// The site's device table with ONE unit given a deadline of its own. Built from
// the site's own models, or the other nineteen would quietly plan as generic.
namespace {

const uint8_t SLOW_UNIT = 41;          // a Temp/RH sensor

struct SlowTable {
    DeviceModel model;
    Device      devs[64];
    DeviceTable table;

    explicit SlowTable(uint32_t timeout_ms) {
        const DeviceTable* site = Site::g_site.devices;
        model = model_of(site, SLOW_UNIT);      // same limits, same alignment
        model.timeout_ms = timeout_ms;          // ...and its own patience
        for (uint8_t i = 0; i < site->count; ++i) {
            devs[i] = site->devices[i];
            if (devs[i].unit == SLOW_UNIT) devs[i].model = &model;
        }
        table.devices = devs;
        table.count   = site->count;
    }
};

}  // namespace

TEST_CASE("modbus slow: a unit past the bus deadline is faulted like a dead one") {
    // F3, and the behaviour that makes the fix worth having. Nothing is wrong
    // with this device except that it is slower than one global number.
    LiveBus m;                                   // the site table: no timeout declared
    const uint8_t peer = m.peer_of(SLOW_UNIT);
    REQUIRE(peer != RtuClient::NO_PEER);

    m.bus.set_fault(SLOW_UNIT, Sim::FieldBus::FAULT_LATE,
                    Config::MODBUS_TIMEOUT_MS + 100);

    REQUIRE(m.until([&] { return m.client.peer_dead(peer); }, 4000));
    INFO("it answers 100 ms after the master has stopped listening");
    // And the answer it did send arrived too late to be anyone's, which is the
    // other half of the symptom: a device in backoff that is talking.
    CHECK(m.client.stale_rx() > 0);
}

TEST_CASE("modbus slow: a unit that declares its own deadline is not") {
    SlowTable slow(Config::MODBUS_TIMEOUT_MS * 2u);
    LiveBus m(&slow.table);
    const uint8_t peer = m.peer_of(SLOW_UNIT);
    REQUIRE(peer != RtuClient::NO_PEER);

    m.bus.set_fault(SLOW_UNIT, Sim::FieldBus::FAULT_LATE,
                    Config::MODBUS_TIMEOUT_MS + 100);

    m.beats(2000);
    INFO("declared " << slow.model.timeout_ms << " ms, answers in "
         << (Config::MODBUS_TIMEOUT_MS + 100) << " ms");
    CHECK_FALSE(m.client.peer_dead(peer));
    CHECK(m.client.peer_misses(peer) == 0);
    CHECK(m.client.dead_count() == 0);
}

TEST_CASE("modbus line: one device quiet is not the same fault as all of them") {
    // F4. A babbler corrupts whatever is in flight, so the miss is charged to
    // whoever was polled and every healthy device faults in turn - which from the
    // boolean alone reads as twenty devices failing at once.
    LiveBus m;
    const Vars::VarRef dead_ref = m.bank.find("alarm_fieldbus");
    const Vars::VarRef line_ref = m.bank.find("alarm_fieldbus_line");
    REQUIRE(dead_ref.index != Vars::VAR_NONE);
    REQUIRE(line_ref.index != Vars::VAR_NONE);

    const uint8_t units = m.client.peer_count();
    REQUIRE(units > 1);

    // One device down: the bus is faulty, the segment is not.
    m.bus.set_fault(m.client.peer_unit(0), Sim::FieldBus::FAULT_ABSENT);
    REQUIRE(m.until([&] { return m.client.dead_count() == 1; }, 4000));
    CHECK(as_bool(Vars::T_BOOL, m.bank.var(dead_ref).value().v));
    INFO("1 of " << (unsigned)units << " devices quiet");
    CHECK_FALSE(as_bool(Vars::T_BOOL, m.bank.var(line_ref).value().v));

    // The whole segment: same boolean, different fault, different engineer.
    for (uint8_t p = 1; p < units; ++p)
        m.bus.set_fault(m.client.peer_unit(p), Sim::FieldBus::FAULT_ABSENT);
    REQUIRE(m.until([&] { return m.client.dead_count() == units; }, 20000));
    CHECK(as_bool(Vars::T_BOOL, m.bank.var(line_ref).value().v));
}

TEST_CASE("modbus fault: a dead output device does not pin the safe-state urgency") {
    // F1. Without the _due() test the clearing sweep never comes back empty (a
    // backed-off device's write stays dirty), pinning every live device's writes
    // at safe-state priority for the rest of the boot.
    LiveBus m;
    const uint16_t w = m.write_block();
    REQUIRE(w != RtuClient::NO_BLOCK);
    const uint8_t peer = m.client.plan().block(w).peer;

    m.bus.set_fault(m.client.peer_unit(peer), Sim::FieldBus::FAULT_ABSENT);
    m.client.reassert_outputs(peer);   // owe the block, so the misses come at the write interval
    REQUIRE(m.until([&] { return m.client.peer_dead(peer); }));

    m.bank.set_control_mode(CONTROL_DISABLED);
    m.beat();
    REQUIRE(m.client.safe_state_pending());

    INFO("an unreachable dirty block must not hold the urgency");
    CHECK(m.until([&] { return !m.client.safe_state_pending(); }));
}

TEST_CASE("modbus fault: a refused write backs the block off, not the device") {
    // F2. An exception leaves the row dirty, correctly - it never reached the
    // field - so nothing else slows the retry down, and MODBUS_WRITE_MIN_MS
    // alone means five transactions a second for the life of the boot.
    LiveBus m;
    const uint16_t w = m.write_block();
    REQUIRE(w != RtuClient::NO_BLOCK);
    const uint8_t peer = m.client.plan().block(w).peer;

    m.bus.set_fault(m.client.peer_unit(peer), Sim::FieldBus::FAULT_REFUSE);
    m.client.reassert_outputs(peer);   // owe the block, so it retries at the write interval

    REQUIRE(m.until([&] { return m.client.plan().block(w).refusals >= 3; }));

    // The RATE is the property, not the field behind it: ten seconds into the
    // doubling (1.6, 3.2, 6.4 s) the block gets a handful of attempts where
    // MODBUS_WRITE_MIN_MS alone would give it fifty.
    const uint8_t before = m.client.plan().block(w).refusals;
    m.beats(1000);
    const uint8_t added = (uint8_t)(m.client.plan().block(w).refusals - before);

    INFO(added << " further refusals in 10 s, at t=" << m.t);
    CHECK(added <= 4);
    CHECK(m.client.plan().block(w).due_ms > m.t);      // still held off
    // The device answered every one of them, so it is not the device at fault
    // and its reads must keep their rates.
    CHECK_FALSE(m.client.peer_dead(peer));
    CHECK(m.client.refusals() > 0);
}


TEST_CASE("modbus tuning: rtu_retries decides when a device counts as dead") {
    // The point of the rtu_ group: a knob whose right value is a property of the
    // cable run, moved without a build. Read live off the bound row, so this
    // takes effect on the next miss with nothing to re-initialise.
    LiveBus m;
    const uint16_t w = m.write_block();
    REQUIRE(w != RtuClient::NO_BLOCK);
    const uint8_t peer = m.client.plan().block(w).peer;
    Var retries = m.bank.var(m.bank.find("rtu_retries"));
    REQUIRE(retries.meta()->name != nullptr);       // the library spliced the row
    REQUIRE(retries.i() == Config::MODBUS_RETRIES); // ...at its factory value

    // Patience raised well past what the run below will spend.
    REQUIRE(m.bank.param_set(retries, "5") != SET_INVALID);

    m.bus.set_fault(m.client.peer_unit(peer), Sim::FieldBus::FAULT_ABSENT);
    m.client.reassert_outputs(peer);
    REQUIRE(m.until([&] { return m.client.peer_misses(peer) >= 3; }));
    INFO("3 misses, 5 retries allowed");
    CHECK_FALSE(m.client.peer_dead(peer));
    CHECK(m.client.dead_count() == 0);

    SUBCASE("and lowering it declares the same device dead on the spot") {
        // No new miss needed: the threshold is read where it is used, so the
        // misses already counted are enough the moment the bar drops under them.
        REQUIRE(m.bank.param_set(retries, "1") != SET_INVALID);
        CHECK(m.client.peer_dead(peer));
        // dead_count() is a PUBLISHED snapshot, refreshed when a transaction
        // resolves rather than per beat, so it catches up on the next miss and
        // not on the edit. Two different things, deliberately.
        CHECK(m.until([&] { return m.client.dead_count() == 1; }));
    }
}

TEST_CASE("modbus tuning: an absent rtu_ row leaves the Config default in force") {
    // A site without the library splice must behave exactly as it did before the
    // rows existed - _tune() falls back to the constant they default from.
    LiveBus m;
    const uint16_t w = m.write_block();
    REQUIRE(w != RtuClient::NO_BLOCK);
    const uint8_t peer = m.client.plan().block(w).peer;

    // The row resolves here, so simulate its absence the only honest way: an
    // override forcing it INVALID, which is what _tune() checks for.
    Var retries = m.bank.var(m.bank.find("rtu_retries"));
    m.bank.override_set(retries, retries.value().v, /*valid=*/false);
    REQUIRE_FALSE(retries.valid());

    m.bus.set_fault(m.client.peer_unit(peer), Sim::FieldBus::FAULT_ABSENT);
    m.client.reassert_outputs(peer);
    REQUIRE(m.until([&] { return m.client.peer_dead(peer); }));
    INFO("dead at the Config threshold, not at a zero read off an invalid row");
    CHECK(m.client.peer_misses(peer) > Config::MODBUS_RETRIES);
}

#endif // BOTANIK_FEATURE_MODBUS_RTU_SERVER && TARGET_SIMULATOR

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT

}  // namespace modbus_client

// modbus_device

namespace modbus_device {

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

using namespace Botanik;
using namespace Botanik::Vars;
using namespace Botanik::Modbus;

TEST_CASE("modbus device: an undescribed unit gets the protocol's own limits") {
    // A model is only as good as its manual; an unnamed unit must not acquire invented constraints.
    const DeviceModel& g = model_of(nullptr, 5);
    CHECK(g.max_regs == 0);          // 0 defers to max_quantity(fc)
    CHECK(g.max_bits == 0);
    CHECK(g.addr_align == 1);
    CHECK(g.qty_align == 1);
    CHECK(g.write_multiple);
    CHECK(g.min_gap_ms == 0);

    // ...and so does a unit a table exists for but does not mention.
    const DeviceModel m = meter("described", 80, 2);
    const Device rows[] = { { 5, &m, "described" } };
    const DeviceTable t = { rows, 1 };
    CHECK(std::string(model_of(&t, 5).name) == "described");
    CHECK(std::string(model_of(&t, 9).name) == std::string(generic_model().name));
}

TEST_CASE("modbus device: the site describes every unit its table addresses") {
    // Rows with no model plan as generic, not an error - but here means hardware was added without one.
    const Bank& io = site_bank();
    for (uint16_t i = 0; i < io.count(); ++i) {
        const Descriptor& d = io.descriptor(i);
        if (d.bind.transport != XP_MODBUS || d.bind.device == 0) continue;
        INFO("unit " << (int)d.bind.device << " carries '"
                     << std::string(d.name) << "' but has no model");
        CHECK(std::string(model_of(Site::g_site.devices, d.bind.device).name)
              != std::string(generic_model().name));
    }
}

TEST_CASE("modbus device: the site names every unit it addresses") {
    // A tag turns "dev 34 not answering" into a cabinet; a commissioned site should never leave one nameless.
    const DeviceTable* t = Site::g_site.devices;
    REQUIRE(t != nullptr);
    for (uint8_t i = 0; i < t->count; ++i) {
        INFO("unit " << (int)t->devices[i].unit << " has no tag");
        REQUIRE(t->devices[i].tag != nullptr);
        CHECK(t->devices[i].tag[0] != '\0');
    }
}

TEST_CASE("modbus device: a board without FC15/FC16 is driven a point at a time") {
    // write_multiple must be read, not merely carried: a device without FC15/FC16
    // must never be sent them (exception 01); the planner chooses the wire form.
    Bank bank;
    TestSite::build(bank);

    // Relay board 31: once as it is, once as a model without multi-write; only the model differs.
    DeviceModel single = relay_board("single", 8);
    single.write_multiple = false;
    const Device srows[] = { { 31, &single, "no-multi" } };
    const DeviceTable st = { srows, 1 };

    Planner multi, one;
    multi.plan(bank, 38400, 10, Site::g_site.devices);
    one.plan(bank, 38400, 10, &st);

    bool saw_multi = false, saw_single = false;
    for (uint16_t i = 0; i < multi.block_count(); ++i) {
        const Block& b = multi.block(i);
        if (b.device != 31 || is_read(b.fc)) continue;
        saw_multi = true;
        CHECK(b.fc == FC_WRITE_COILS);
    }
    for (uint16_t i = 0; i < one.block_count(); ++i) {
        const Block& b = one.block(i);
        if (b.device != 31 || is_read(b.fc)) continue;
        saw_single = true;
        INFO("block at " << b.reg << " x " << b.regs);
        CHECK(b.fc == FC_WRITE_COIL);
        // FC05 carries exactly one coil; the block must not coalesce.
        CHECK(b.regs == 1);
    }
    REQUIRE(saw_multi);
    REQUIRE(saw_single);
}

TEST_CASE("modbus device: a wide write no device will take is dropped, not half sent") {
    // The merge loop admits a block's first row unconditionally and skips read
    // alignment on writes; FC06 (device without FC16, span_cap 1) sends a
    // 32-bit setpoint's HIGH WORD ALONE. gh_mlb41 has none yet - latent until mapped.
    static const Vars::Descriptor ROWS[] = {
        Vars::MO("wide_setpoint", Vars::T_U32, /*device*/7, FC_WRITE_REGISTERS,
                 /*reg*/100, /*regs*/2, Vars::Scalar::U(0), 0, "", "32-bit setpoint", ""),
    };
    Bank bank;
    bank.register_vars(ROWS, 1);
    bank.freeze();

    DeviceModel narrow = meter("narrow", 80, 1);
    narrow.write_multiple = false;              // FC05/FC06 only
    const Device rows[] = { { 7, &narrow, "no-fc16" } };
    const DeviceTable dt = { rows, 1 };

    Planner plan;
    plan.plan(bank, 38400, 10, &dt);

    for (uint16_t i = 0; i < plan.block_count(); ++i) {
        const Block& b = plan.block(i);
        INFO("block fc " << (int)b.fc << " reg " << b.reg << " regs " << b.regs);
        // FC06 carries exactly one register; a wider block must not be planned.
        if (b.fc == FC_WRITE_REGISTER) CHECK(b.regs == 1);
    }
    // Unscannable on this device; the planner says so instead of sending half. rows_unscanned() is a bring-up critical.
    CHECK(plan.rows_unscanned() == 1);

    // ...and the same row on a device that DOES take FC16 plans normally.
    DeviceModel wide = meter("wide", 80, 1);
    wide.write_multiple = true;
    const Device wrows[] = { { 7, &wide, "fc16" } };
    const DeviceTable wdt = { wrows, 1 };
    Planner ok;
    ok.plan(bank, 38400, 10, &wdt);
    CHECK(ok.rows_unscanned() == 0);
    REQUIRE(ok.block_count() == 1);
    CHECK(ok.block(0).fc == FC_WRITE_REGISTERS);
    CHECK(ok.block(0).regs == 2);
}

TEST_CASE("modbus device: the Waveshare boards are what their manuals say") {
    // Waveshare "Development Protocol V2" (relay), "Development Protocol" (analog boards).
    const DeviceModel& relay = model_of(Site::g_site.devices, 31);
    INFO("Relay (D): eight relays as coils 0x0000-0x0007, written with 05 or 0F");
    CHECK(relay.max_bits == 8);
    CHECK(relay.write_multiple);

    const DeviceModel& ai = model_of(Site::g_site.devices, 11);
    INFO("AI 8CH: quantity must not exceed the number of channels");
    CHECK(ai.max_regs == 8);

    const DeviceModel& ao = model_of(Site::g_site.devices, 16);
    CHECK(ao.max_regs == 8);
}

TEST_CASE("modbus device: the AI board's channels are read where they live") {
    // Channel readings are INPUT registers 3x0000-3x0007 (fc 0x04); the board's holding registers start at 4x1000, so fc 0x03 lands on nothing (exception 02).
    uint16_t seen = 0;
    const Bank& io = site_bank();
    for (uint16_t i = 0; i < io.count(); ++i) {
        const Descriptor& d = io.descriptor(i);
        if (d.bind.transport != XP_MODBUS || d.bind.device != 11) continue;
        seen++;
        INFO("'" << std::string(d.name) << "' reads unit 11 with fc " << (int)d.bind.fc);
        CHECK(d.bind.fc == FC_READ_INPUT);
    }
    CHECK(seen > 0);
}

TEST_CASE("modbus device: no coil block covers more relays than the board has") {
    Bank bank;
    TestSite::build(bank);
    Planner p;
    p.plan(bank, 38400, 10, Site::g_site.devices);

    for (uint16_t i = 0; i < p.block_count(); ++i) {
        const Block& b = p.block(i);
        if (!is_bit(b.fc)) continue;
        const DeviceModel& m = model_of(Site::g_site.devices, b.device);
        if (m.max_bits == 0) continue;
        INFO("unit " << (int)b.device << " block at " << b.reg
                     << " covers " << b.regs << " coils, board has " << m.max_bits);
        CHECK(b.regs <= m.max_bits);
    }
}

TEST_CASE("modbus device: the SDM630's own limits are what the manual says") {
    const DeviceModel& m = model_of(Site::g_site.devices, 5);
    INFO("Eastron SDM630-Modbus V2 manual V1.6B");
    // "a maximum of 40 values in a single transaction; therefore the maximum
    // number of registers requestable is 80"
    CHECK(m.max_regs == 80);
    // "the Starting Address and the Number of Points must be even numbers"
    CHECK(m.addr_align == 2);
    CHECK(m.qty_align == 2);
}

TEST_CASE("modbus device: no planned read block exceeds its device's ceiling") {
    // An over-long request draws an exception, not a short read, which reads as a bad cable on the bus.
    Bank bank;
    TestSite::build(bank);
    Planner p;
    p.plan(bank, 38400, 10, Site::g_site.devices);
    REQUIRE(p.block_count() > 0);

    for (uint16_t i = 0; i < p.block_count(); ++i) {
        const Block& b = p.block(i);
        if (!is_read(b.fc) || is_bit(b.fc)) continue;
        const DeviceModel& m = model_of(Site::g_site.devices, b.device);
        if (m.max_regs == 0) continue;
        INFO("unit " << (int)b.device << " block at " << b.reg
                     << " spans " << b.regs << ", model allows " << m.max_regs);
        CHECK(b.regs <= m.max_regs);
    }
}

TEST_CASE("modbus device: the site's own plan loses nothing to its constraints") {
    // Alignment padding can push a block past its ceiling and drop it - signals
    // never scanned. Both counters are bring-up criticals; the site must trip neither.
    Bank bank;
    TestSite::build(bank);
    Planner p;
    p.plan(bank, 38400, 10, Site::g_site.devices);

    INFO(p.rows_dropped() << " row(s) unplanned and " << p.rows_unscanned()
                          << " never scanned");
    CHECK(p.rows_dropped() == 0);
    CHECK(p.rows_unscanned() == 0);
    CHECK(p.block_count() > 0);
}

TEST_CASE("modbus device: a planned read is aligned the way its device demands") {
    Bank bank;
    TestSite::build(bank);
    Planner p;
    p.plan(bank, 38400, 10, Site::g_site.devices);

    for (uint16_t i = 0; i < p.block_count(); ++i) {
        const Block& b = p.block(i);
        if (!is_read(b.fc) || is_bit(b.fc)) continue;
        const DeviceModel& m = model_of(Site::g_site.devices, b.device);
        INFO("unit " << (int)b.device << " block at " << b.reg << " x " << b.regs);
        CHECK((b.reg % (m.addr_align ? m.addr_align : 1)) == 0);
        CHECK((b.regs % (m.qty_align ? m.qty_align : 1)) == 0);
    }
}

TEST_CASE("modbus device: a tight ceiling cuts a run into more blocks") {
    // The planner must actually READ max_regs, not merely carry it.
    Bank bank;
    TestSite::build(bank);

    const DeviceModel loose  = meter("loose",  0, 2);
    const DeviceModel strict = meter("strict", 4, 2);

    const Device lrows[] = { { 5, &loose, "loose" } };
    const Device srows[] = { { 5, &strict, "strict" } };
    const DeviceTable lt = { lrows, 1 };
    const DeviceTable st = { srows, 1 };

    Planner a, b;
    a.plan(bank, 38400, 10, &lt);
    b.plan(bank, 38400, 10, &st);

    uint16_t la = 0, lb = 0;
    for (uint16_t i = 0; i < a.block_count(); ++i)
        if (a.block(i).device == 5 && is_read(a.block(i).fc)) la++;
    for (uint16_t i = 0; i < b.block_count(); ++i) {
        if (b.block(i).device != 5 || !is_read(b.block(i).fc)) continue;
        lb++;
        INFO("a 4-register ceiling must bind every block");
        CHECK(b.block(i).regs <= 4);
    }
    INFO(la << " block(s) unconstrained, " << lb << " at a 4-register ceiling");
    CHECK(lb > la);
}

TEST_CASE("modbus device: an odd row is widened, not sent to be refused") {
    // A device storing pairs answers exception 02 mid-pair; the planner grows the request to the boundary instead.
    Bank bank;
    TestSite::build(bank);

    const DeviceModel pairs = meter("pairs", 80, 2);
    const Device rows[] = { { 11, &pairs, "pairs" } };   // the AI board, 1-register rows
    const DeviceTable t = { rows, 1 };

    Planner p;
    p.plan(bank, 38400, 10, &t);

    bool saw = false;
    for (uint16_t i = 0; i < p.block_count(); ++i) {
        const Block& b = p.block(i);
        if (b.device != 11 || !is_read(b.fc) || is_bit(b.fc)) continue;
        saw = true;
        INFO("block at " << b.reg << " x " << b.regs);
        CHECK((b.reg % 2) == 0);
        CHECK((b.regs % 2) == 0);
    }
    CHECK(saw);
}

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT

}  // namespace modbus_device

// modbus_image

namespace modbus_image {

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

using namespace Botanik;
using namespace Botanik::Vars;
using namespace Botanik::Modbus;

namespace {

/** @brief A bank over the site's real tables, plus a plan over it. */
struct Bus {
    Bank    bank;
    Planner plan;
    Bus() {
        TestSite::build(bank);
        bank.set_health_hold(false);
        bank.set_control_mode(CONTROL_ENABLED);
        plan.plan(bank, 38400, 10);
    }
    /** @brief First write block carrying registers or bits; both encode differently and need pinning. */
    const Block* first_write(bool bits) const {
        for (uint16_t i = 0; i < plan.block_count(); ++i) {
            const Block& b = plan.block(i);
            if (!is_read(b.fc) && is_bit(b.fc) == bits) return &b;
        }
        return nullptr;
    }
    /** @brief Demand @p x on row @p k of @p b, in its own type. A bool in an F32
     *  row lands as a denormal reading zero either way, hiding a bad comparison. */
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
    // collect_write() builds the frame, clear_dirty() lands beats later; a demand that moves in between must survive the wholesale clear.
    for (int pass = 0; pass < 2; ++pass) {
        const bool bits = (pass == 1);
        Bus bus;
        const Block* b = bus.first_write(bits);
        if (b == nullptr || b->row_count < 2) continue;
        CAPTURE(bits);

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
    // With no payload to compare, the block must still clear wholesale, not silently stop clearing.
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

    // Find a row that declares F_INVERT, if the site has one.
    const Descriptor* inv = nullptr;
    uint16_t inv_off = 0;
    for (uint16_t k = 0; k < b->row_count; ++k) {
        const Descriptor* d = bus.bank.var(bus.plan.row((uint16_t)(b->first_row + k))).meta();
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


TEST_CASE("modbus image: an AO row goes out through its calibration") {
    // An AO's wire value is its engineering value through the two-point span, everything else as-is - easy to lose silently in a merge.
    Bus bus;
    const Block* b = bus.first_write(false);
    REQUIRE(b != nullptr);

    // Find an AO row on the block; the reference site's are the intractors.
    uint16_t k = 0xFFFF;
    for (uint16_t i = 0; i < b->row_count; ++i)
        if (bus.row(*b, i).meta()->scope == SCOPE_AO) { k = i; break; }
    if (k == 0xFFFF) return;                    // no calibrated output here

    Var h = bus.row(*b, k);
    const Descriptor& d = *h.meta();
    const AnalogCal* cal = analog_cal(d);
    REQUIRE(cal != nullptr);

    // Inside the span but not at an endpoint, so a skipped calibration would visibly differ.
    const AnalogValues av = analog_values(&bus.bank, cal);
    const float eng = av.eng_min + (av.eng_max - av.eng_min) * 0.25f;
    bus.bank.override_set(h, from_f32(d.type, eng));

    uint8_t sent[256];
    const uint16_t n = collect_write(bus.bank, bus.plan, *b, sent, sizeof sent);
    REQUIRE(n > 0);

    const uint16_t off  = (uint16_t)(d.bind.reg - b->reg);
    const uint16_t got  = rd16(sent + off * 2);
    const uint16_t want = (uint16_t)(int64_t)av.to_raw(eng);

    INFO("'" << std::string(d.name) << "' eng " << eng
             << " -> raw " << av.to_raw(eng));
    CHECK(got == want);
    // Confirms it isn't the raw engineering value uncalibrated, or the test proves nothing.
    if ((uint16_t)(int64_t)eng != want) CHECK(got != (uint16_t)(int64_t)eng);
}

// Saying so when a read fails

TEST_CASE("modbus image: a refused read invalidates its rows, and says why") {
    Bus bus;
    const Block* b = nullptr;
    for (uint16_t i = 0; i < bus.plan.block_count(); ++i)
        if (is_read(bus.plan.block(i).fc)) { b = &bus.plan.block(i); break; }
    REQUIRE(b != nullptr);

    // Give the block a good reading first, so there is something to lose.
    uint8_t payload[256];
    memset(payload, 0, sizeof payload);
    Response r;
    r.data = payload;
    r.len  = (uint16_t)(b->regs * 2u);
    r.unit = b->device;
    r.fc   = b->fc;
    r.ex   = EX_NONE;
    REQUIRE(commit_read(bus.bank, bus.plan, *b, r, 1000) > 0);
    REQUIRE(bus.row(*b, 0).live().good());

    // The device answers, and declines.
    invalidate_block(bus.bank, bus.plan, *b, Q_REFUSED);

    CHECK_FALSE(bus.row(*b, 0).live().good());
    INFO("a refusal is a register map that does not match the device");
    CHECK(bus.row(*b, 0).live().q == Q_REFUSED);
    CHECK(std::string(quality_str(Q_REFUSED)) != std::string(quality_str(Q_STALE)));
}

TEST_CASE("modbus image: a row the payload never reached is not left reading good") {
    Bus bus;
    const Block* b = nullptr;
    for (uint16_t i = 0; i < bus.plan.block_count(); ++i) {
        const Block& c = bus.plan.block(i);
        if (is_read(c.fc) && !is_bit(c.fc) && c.row_count >= 2) { b = &c; break; }
    }
    if (b == nullptr) return;

    uint8_t payload[256];
    memset(payload, 0, sizeof payload);
    Response r;
    r.data = payload;
    r.len  = (uint16_t)(b->regs * 2u);
    r.unit = b->device; r.fc = b->fc; r.ex = EX_NONE;
    REQUIRE(commit_read(bus.bank, bus.plan, *b, r, 1000) > 0);
    REQUIRE(bus.row(*b, b->row_count - 1).live().good());

    // Now a short answer: only the first two registers arrive.
    r.len = 4;
    commit_read(bus.bank, bus.plan, *b, r, 2000);

    Var last = bus.row(*b, (uint16_t)(b->row_count - 1));
    INFO("'" << std::string(last.meta()->name) << "' was past the end of the payload");
    CHECK_FALSE(last.live().good());
    CHECK(last.live().q == Q_NO_DATA);
}

TEST_CASE("modbus image: a bounded direct row is judged at commit, not at a sweep") {
    // A T_F32 sensor with a band on the descriptor: the band must be enforced at
    // commit, not only by reject_implausible()'s background sweep. No sweep runs here.
    Bus bus;
    const Block* b = nullptr;
    for (uint16_t i = 0; i < bus.plan.block_count(); ++i) {
        const Block& c = bus.plan.block(i);
        if (is_read(c.fc) && c.device == 41) { b = &c; break; }
    }
    REQUIRE(b != nullptr);

    uint8_t payload[256];
    Response r;
    r.data = payload;
    r.len  = (uint16_t)(b->regs * 2u);
    r.unit = b->device;
    r.fc   = b->fc;
    r.ex   = EX_NONE;

    // High word first: these rows carry no F_WORD_SWAP.
    struct Put {
        static void f32(uint8_t* p, uint16_t n, float v) {
            uint32_t u;
            memcpy(&u, &v, sizeof u);
            memset(p, 0, n);
            p[0] = (uint8_t)(u >> 24); p[1] = (uint8_t)(u >> 16);
            p[2] = (uint8_t)(u >> 8);  p[3] = (uint8_t)u;
        }
    };

    Var temp = bus.row(*b, 0);
    REQUIRE(std::string(temp.meta()->name) == "cli_temp_ext1");

    // In band, so it lands - which also pins the encoding for what follows.
    Put::f32(payload, (uint16_t)sizeof payload, 20.0f);
    REQUIRE(commit_read(bus.bank, bus.plan, *b, r, 1000) > 0);
    REQUIRE(temp.live().good());
    REQUIRE(temp.live().v.f == doctest::Approx(20.0f));

    // A reading no thermometer produced. The band is -40..80.
    Put::f32(payload, (uint16_t)sizeof payload, -3000.0f);
    commit_read(bus.bank, bus.plan, *b, r, 2000);

    INFO("out of band at commit, with no reject_implausible() in between");
    CHECK_FALSE(temp.live().good());
    CHECK(temp.live().q == Q_IMPLAUSIBLE);

    // A NaN is refused too, without being reported as a band failure.
    const float nan_v = 0.0f / (float)(1.0 - 1.0);
    Put::f32(payload, (uint16_t)sizeof payload, nan_v);
    commit_read(bus.bank, bus.plan, *b, r, 3000);
    CHECK(temp.live().q == Q_IMPLAUSIBLE);
}

TEST_CASE("modbus plan: an input that could never go stale is refused a place") {
    // expire_stale() skips timeout_ms == 0 and nothing else invalidates such a row,
    // so planning it would leave a forever-good reading from before the device died.
    uint16_t offenders = 0;
    const Bank& io = site_bank();
    for (uint16_t i = 0; i < io.count(); ++i) {
        const Descriptor& d = io.descriptor(i);
        if (d.bind.transport == XP_MODBUS && d.role == ROLE_INPUT &&
            is_read(d.bind.fc) && d.timeout_ms == 0) offenders++;
    }

    Bus bus;
    INFO("the site declares " << offenders << " such row(s)");
    CHECK(bus.plan.rows_dropped() == offenders);
}

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT

}  // namespace modbus_image
