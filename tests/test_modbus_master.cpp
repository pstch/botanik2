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

// --------------------------------------------------------------------------
// modbus_client
// --------------------------------------------------------------------------

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

    Master() : bank(Site::g_site.vars), client(&bank, nullptr, Site::g_site.devices) {
        bank.init();
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

TEST_CASE("modbus client: an ordinary dirty write goes before a read that is merely due") {
    Master m;
    m.dirty_all_writes();

    // Past the write interval; reads are due but not yet a whole period late.
    const uint16_t pick = m.client.next_block(Config::MODBUS_WRITE_MIN_MS + 1);
    INFO("writes are worth more than a reading about to be taken again anyway");
    CHECK(m.is_write(pick));
}

TEST_CASE("modbus client: a read starved by a whole period jumps the write queue") {
    // A stroking Actuator keeps writes dirty continuously; without a floor, reads starve and the image goes stale.
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
    // disable_control() dirties every ROLE_OUTPUT so the safe state ships at once, not after MODBUS_WRITE_MIN_MS.
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
    const Scalar before = h.raw();
    h.clear_field_dirty();

    m.client.reassert_outputs(w->peer);

    CHECK(h.field_dirty());
    INFO("'" << std::string(h.name()) << "' kept what it was demanding");
    CHECK(h.raw().u == before.u);
}

// Pacing
// ===========================================================================

TEST_CASE("modbus client: duty counts the beat a transaction is quantised to") {
    // A frame shorter than a beat still occupies the bus; idle time is unusable to the next sensor added.
    Master m;
    const Planner& p = m.client.plan();

    INFO("wire " << p.wire_duty_ppm() << " ppm, scheduled " << p.duty_ppm() << " ppm");
    CHECK(p.duty_ppm() >= p.wire_duty_ppm());
}

TEST_CASE("modbus client: a short frame on a quick device is charged a whole beat") {
    // Where wire and scheduled duty diverge; the reference site can't show it (MODBUS_TURNAROUND_MS is already a beat), so this uses a fast link.
    Bank bank(Site::g_site.vars);
    bank.init();
    Planner p;
    p.plan(bank, 115200, 1);          // 1 ms turnaround, fast link

    INFO("wire " << p.wire_duty_ppm() << " ppm, scheduled " << p.duty_ppm() << " ppm");
    CHECK(p.duty_ppm() > p.wire_duty_ppm());
}

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT

}  // namespace modbus_client

// --------------------------------------------------------------------------
// modbus_device
// --------------------------------------------------------------------------

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
    const VarTables& t = Site::g_site.vars;
    for (uint16_t i = 0; i < t.count; ++i) {
        const Variable& d = t.var[i];
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
    Bank bank(Site::g_site.vars);
    bank.init();

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
    static const Vars::Variable ROWS[] = {
        Vars::MO("wide_setpoint", Vars::T_U32, /*device*/7, FC_WRITE_REGISTERS,
                 /*reg*/100, /*regs*/2, Vars::Scalar::U(0), 0, "", "32-bit setpoint", ""),
    };
    static Vars::Value VALS[1];
    static uint16_t    IDX[1];
    Vars::VarTables t = {};
    t.var = ROWS; t.val = VALS; t.count = 1; t.index = IDX;
    Bank bank(t);
    bank.init();

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
    const VarTables& t = Site::g_site.vars;
    uint16_t seen = 0;
    for (uint16_t i = 0; i < t.count; ++i) {
        const Variable& d = t.var[i];
        if (d.bind.transport != XP_MODBUS || d.bind.device != 11) continue;
        seen++;
        INFO("'" << std::string(d.name) << "' reads unit 11 with fc " << (int)d.bind.fc);
        CHECK(d.bind.fc == FC_READ_INPUT);
    }
    CHECK(seen > 0);
}

TEST_CASE("modbus device: no coil block covers more relays than the board has") {
    Bank bank(Site::g_site.vars);
    bank.init();
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
    Bank bank(Site::g_site.vars);
    bank.init();
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
    Bank bank(Site::g_site.vars);
    bank.init();
    Planner p;
    p.plan(bank, 38400, 10, Site::g_site.devices);

    INFO(p.rows_dropped() << " row(s) unplanned and " << p.rows_unscanned()
                          << " never scanned");
    CHECK(p.rows_dropped() == 0);
    CHECK(p.rows_unscanned() == 0);
    CHECK(p.block_count() > 0);
}

TEST_CASE("modbus device: a planned read is aligned the way its device demands") {
    Bank bank(Site::g_site.vars);
    bank.init();
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
    Bank bank(Site::g_site.vars);
    bank.init();

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
    Bank bank(Site::g_site.vars);
    bank.init();

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

// --------------------------------------------------------------------------
// modbus_image
// --------------------------------------------------------------------------

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
    Bus() : bank(Site::g_site.vars) {
        bank.init();
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
    const Variable& d = *h.meta();
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
// ===========================================================================

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
    REQUIRE(bus.row(*b, 0).live_valid());

    // The device answers, and declines.
    invalidate_block(bus.bank, bus.plan, *b, Q_REFUSED);

    CHECK_FALSE(bus.row(*b, 0).live_valid());
    INFO("a refusal is a register map that does not match the device");
    CHECK(bus.row(*b, 0).live_quality() == Q_REFUSED);
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
    REQUIRE(bus.row(*b, b->row_count - 1).live_valid());

    // Now a short answer: only the first two registers arrive.
    r.len = 4;
    commit_read(bus.bank, bus.plan, *b, r, 2000);

    Var last = bus.row(*b, (uint16_t)(b->row_count - 1));
    INFO("'" << std::string(last.meta()->name) << "' was past the end of the payload");
    CHECK_FALSE(last.live_valid());
    CHECK(last.live_quality() == Q_NO_DATA);
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
    REQUIRE(temp.live_valid());
    REQUIRE(temp.live().f == doctest::Approx(20.0f));

    // A reading no thermometer produced. The band is -40..80.
    Put::f32(payload, (uint16_t)sizeof payload, -3000.0f);
    commit_read(bus.bank, bus.plan, *b, r, 2000);

    INFO("out of band at commit, with no reject_implausible() in between");
    CHECK_FALSE(temp.live_valid());
    CHECK(temp.live_quality() == Q_IMPLAUSIBLE);

    // A NaN is refused too, without being reported as a band failure.
    const float nan_v = 0.0f / (float)(1.0 - 1.0);
    Put::f32(payload, (uint16_t)sizeof payload, nan_v);
    commit_read(bus.bank, bus.plan, *b, r, 3000);
    CHECK(temp.live_quality() == Q_IMPLAUSIBLE);
}

TEST_CASE("modbus plan: an input that could never go stale is refused a place") {
    // expire_stale() skips timeout_ms == 0 and nothing else invalidates such a row,
    // so planning it would leave a forever-good reading from before the device died.
    const VarTables& t = Site::g_site.vars;
    uint16_t offenders = 0;
    for (uint16_t i = 0; i < t.count; ++i) {
        const Variable& d = t.var[i];
        if (d.bind.transport == XP_MODBUS && d.role == ROLE_INPUT &&
            is_read(d.bind.fc) && d.timeout_ms == 0) offenders++;
    }

    Bus bus;
    INFO("the site declares " << offenders << " such row(s)");
    CHECK(bus.plan.rows_dropped() == offenders);
}

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT

}  // namespace modbus_image
