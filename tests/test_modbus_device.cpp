/**
 * @file test_modbus_device.cpp
 * @brief The device table (modbus/rtu/device.h) and the constraints it puts on
 *        the planner.
 *
 * Two things are checked here, and they fail for different reasons. That the
 * PLANNER honours a model's ceiling and alignment is library behaviour, driven
 * against models made up for the purpose. That the SITE's own devices are
 * described correctly is a fact about gh_mlb41 and its manuals, so it is checked
 * against the real table.
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

TEST_CASE("modbus device: an undescribed unit gets the protocol's own limits") {
    // A model entry is only as good as the manual behind it, so a unit nobody
    // named must not acquire invented constraints.
    const DeviceModel& g = model_of(nullptr, 5);
    CHECK(g.max_regs == 0);          // 0 defers to max_quantity(fc)
    CHECK(g.addr_align == 1);
    CHECK(g.qty_align == 1);
    CHECK(g.write_multiple);
    CHECK(g.min_gap_ms == 0);

    // ...and so does a unit a table exists for but does not mention.
    const DeviceModel m = { "described", 80, 2, 2, false, 0 };
    const Device rows[] = { { 5, &m } };
    const DeviceTable t = { rows, 1 };
    CHECK(std::string(model_of(&t, 5).name) == "described");
    CHECK(std::string(model_of(&t, 9).name) == std::string(generic_model().name));
}

TEST_CASE("modbus device: the site describes every unit its table addresses") {
    // A device with rows but no model is not an error - it plans as generic -
    // but on this site it means somebody added hardware and stopped there.
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
    // Task 6's acceptance check. An over-long request is answered with an
    // exception rather than a short read, which on a bus reads as a bad cable.
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
    // Padding a block out to its device's alignment can push it past that
    // device's ceiling, and a block dropped there would be signals that are
    // never scanned. Both counters are logged as criticals at bring-up; this
    // says the reference site does not trip either.
    Bank bank(Site::g_site.vars);
    bank.init();
    Planner p;
    p.plan(bank, 38400, 10, Site::g_site.devices);

    INFO(p.rows_dropped() << " row(s) and " << p.blocks_dropped()
                          << " block(s) dropped");
    CHECK(p.rows_dropped() == 0);
    CHECK(p.blocks_dropped() == 0);
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
    // The planner has to actually READ max_regs, not merely carry it. Same
    // table, two models, and the strict one must produce more blocks.
    Bank bank(Site::g_site.vars);
    bank.init();

    const DeviceModel loose  = { "loose",  0, 2, 2, false, 0 };
    const DeviceModel strict = { "strict", 4, 2, 2, false, 0 };

    const Device lrows[] = { { 5, &loose } };
    const Device srows[] = { { 5, &strict } };
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
    // A device storing pairs answers exception 02 to a request landing in the
    // middle of one. The planner grows the request to the boundary instead - the
    // extra registers are read and ignored, which costs a few byte-times.
    Bank bank(Site::g_site.vars);
    bank.init();

    const DeviceModel pairs = { "pairs", 80, 2, 2, false, 0 };
    const Device rows[] = { { 11, &pairs } };   // the AI board, 1-register rows
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
