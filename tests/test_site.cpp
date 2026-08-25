/**
 * @file test_site.cpp
 * @brief The selected site's table integrity: every name a program reaches for
 *        resolves, and no two rows collide on a register or on pool bytes.
 *
 * A name matching no row resolves to the inert sink, which reads 0 forever, so
 * these are the cases that stop a typo becoming a silently dead input.
 */

#include <cstdint>
#include "libbotanik/plc/authority.h"
#include <cstring>
#include "libbotanik_site.h"
#include "libbotanik.h"
#include "doctest.h"
#include BOTANIK_SITE_HEADER
#if BOTANIK_FEATURE_SUN_TRACKER
#  include "libbotanik/sun/sun_tracker.h"
#endif
#include <string>
#include <cstddef>
#include "site_bank.h"
#include "sites/gh_mlb41/programs.h"

// site_names

namespace site_names {


using namespace Botanik;

namespace {

    /** @brief A bank over the site's real tables, seeded as bring-up leaves
     *  it - not the firmware (drags in UART/terminal). */
    struct SiteBank {
        Vars::Bank bank;
        Botanik::Plc::ControlAuthority auth{bank};
        SiteBank() { TestSite::build(bank); }
    };

    const Botanik::SiteDescriptor& site() { return Site::g_site; }

}  // namespace

namespace {

/**
 * @brief Every control program the site runs, constructed together.
 *
 * A new program goes HERE, and that is the price of them being firmware
 * members rather than a table: two cases below walk this list, and a program
 * missing from it is a program whose names nothing checks.
 */
struct AllPrograms {
    /** @brief The sun watch owns the tracker, so the harness gets it for free.
     *  Never registered here, so it stays unlocated and day_phase holds its
     *  phase - which is the boot path, and the one worth walking. */
    Site::SunWatch             sun_watch;

    Site::AcGridWatch          ac_grid;
    Site::AcLocalWatch         ac_local;
    Site::DcWatch              dc;
    Site::TempRHSensors        sensors;
    Site::TempRHZone           zones;
    Site::StratSensor          strat;
    Site::DayPhaseController   phase;
    Site::TargetTempController target;
    Site::CoolingController    cooling;
    Site::DestratController    destrat;
    Site::HumidityController   humidity;
    Site::IrrigationModulation irr_mod;
    Site::IrrMtrInput          irr_in;
    Site::IrrMtrOutput         irr_out;
    Site::IrrigationCycle      irrigation;
    Site::IrrValves            irr_valves;
    Site::ScreenController     screen;
    Site::WindowsController    windows;
    Site::IntractorController  intractors;
    Site::FanController        fans;
    Site::SkirtController      skirt;

    static const uint8_t COUNT = 22;
    Botanik::Plc::Program* all[COUNT];

    AllPrograms(const Botanik::SetupContext& ctx, Vars::Bank& io)
        : sun_watch(ctx, io), ac_grid(ctx, io), ac_local(ctx, io), dc(ctx, io), sensors(ctx, io), zones(ctx, io),
          strat(ctx, io), phase(ctx, io), target(ctx, io), cooling(ctx, io),
          destrat(ctx, io), humidity(ctx, io),
          irr_mod(ctx, io), irr_in(ctx, io), irr_out(ctx, io), irrigation(ctx, io), irr_valves(ctx, io), screen(ctx, io), windows(ctx, io),
          intractors(ctx, io), fans(ctx, io), skirt(ctx, io) {
        Botanik::Plc::Program* const list[] = {
            &ac_grid, &ac_local, &dc, &sensors, &zones, &strat, &phase, &target,
            &cooling, &destrat, &humidity, &irr_mod, &irr_in, &irr_out, &irrigation, &irr_valves, &screen,
            &sun_watch,
            &windows, &intractors, &fans, &skirt
        };
        // Sized from the list, never a literal: a hand-kept count silently
        // drops whichever program is last the day one is added.
        for (uint8_t i = 0; i < COUNT; i++) all[i] = list[i];
    }
};

}  // namespace

TEST_CASE("site: nothing the site touches fails to resolve") {
    // Resolves every table, then RUNS the site (each program once, plus
    // simulate()), catching names tasks.h/sim.h misspell that the walks above miss.
    SiteBank f;

    for (uint8_t i = 0; i < site().home_field_count; i++)
        (void)f.bank.find(site().home_fields[i].name);

    Logs::Logger logger;
    Vars::BankAlarms rows;
    rows.bind(&f.bank);
    Alarms::AlarmController m(Botanik::SetupContext(), &rows);
    m.setup(0);   // the scan is on_setup(): the rows exist now
    (void)m;

    // Var::set() isn't gated (authority is enforced where a writer is
    // DISPATCHED); this resolves names against a running bank, not a stopped one.
    f.auth.set_health_hold(false);
#ifdef TARGET_SIMULATOR
    Site::simulate(f.bank, 1000);
#endif
    // The site's programs are members of its firmware class now, so there is no
    // table to walk: AllPrograms is the list instead, and the check it buys is
    // that no program names a row the tables do not declare.
    Botanik::Tasks::Registry prog_reg;
    Botanik::SetupContext prog_ctx(prog_reg);
    // The firmware hands this over; a program built without one is FAULTED for
    // a reason that has nothing to do with the names this case is checking.
    Botanik::Sun::SunPositionTracker tracker(prog_ctx);
    prog_ctx.sun = &tracker;
    AllPrograms progs(prog_ctx, f.bank);
    for (Plc::Program* p : progs.all)
        p->step(1000);      // setup() binds the names, then one activation

    INFO("a non-zero count means a table or a control program names a variable "
         "that is not in vars.h; the bank logs which one");
    CHECK(f.bank.unresolved_count() == 0);

    INFO("and every one of them bound: a program that missed a name is FAULTED "
         "at bring-up, which is what says so on hardware where no test runs");
    for (Plc::Program* p : progs.all)
        CHECK(p->health() != Plc::Health::FAULTED);

    // Bank::set() is the operator's path alone; catches a write at a stale index - zero is a real row.
    for (uint16_t k = 0; k < f.bank.param_count(); k++) {
        const Vars::Var h = f.bank.param_at(k);
        if (h.meta()->type == Vars::T_STR) continue;   // lives in the pool
        INFO("parameter '" << h.meta()->name << "' was written by a control "
             "program or the simulated field");
        CHECK(h.value().v.u == h.meta()->def.u);
    }
}

#if BOTANIK_FEATURE_SUN_TRACKER
TEST_CASE("site: every control program binds every name it reaches for") {
    // The name-resolution case above walks the TABLES. A program binds names
    // the tables never mention it wants, and with a TYPE - so a row that is a
    // T_U16 read as a float resolves to the sink and the law silently sees 0.
    // Constructing and binding them all is the only thing that covers it.
    Botanik::Tasks::Registry reg;
    Vars::Bank bank;
    Botanik::Plc::ControlAuthority auth{bank};
    TestSite::build(bank);
    const uint16_t before = bank.unresolved_count();

    AllPrograms progs(Botanik::SetupContext(reg), bank);
    for (Botanik::Plc::Program* p : progs.all) p->register_tasks();
    reg.setup(0);

    INFO("a name that is absent, or a row bound at the wrong type, counts here");
    CHECK(bank.unresolved_count() == before);
}

#endif

TEST_CASE("site: the parameters are one contiguous block") {
    // PARAMS/`set` address parameters by a dense 0-based index; a row outside
    // it is unreachable and silently not persisted.
    SiteBank f;
    uint16_t total = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++)
        if (f.bank.var(i).meta()->role == Vars::ROLE_PARAM) total++;
    CHECK(total == f.bank.param_count());
    CHECK(f.bank.param_count() > 0);
    for (uint16_t k = 0; k < f.bank.param_count(); k++)
        CHECK(f.bank.param_at(k).meta()->role == Vars::ROLE_PARAM);
}

TEST_CASE("site: no two text parameters share pool bytes") {
    // Bank::init() lays these out; an overlap is a layout bug, invisible
    // until settings start overwriting each other.
    SiteBank f;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Descriptor& a = *f.bank.var(i).meta();
        if (a.type != Vars::T_STR) continue;
        const uint16_t ao = f.bank.var(i).str_off(), ac = Vars::text_slot(a).cap;
        for (uint16_t j = (uint16_t)(i + 1); j < f.bank.count(); j++) {
            const Vars::Descriptor& b = *f.bank.var(j).meta();
            if (b.type != Vars::T_STR) continue;
            const uint16_t bo = f.bank.var(j).str_off(), bc = Vars::text_slot(b).cap;
            INFO("'" << a.name << "' [" << ao << "," << ao + ac << ") overlaps '"
                     << b.name << "' [" << bo << "," << bo + bc << ")");
            CHECK((ao + ac <= bo || bo + bc <= ao));
        }
    }
    INFO("the site's string pool is too small for its text parameters");
    CHECK(f.bank.pool_overflow() == 0);
}

namespace {
    /** @brief Does this row publish a Modbus TCP register? Delegates to
     *  vars/access.h. */
    bool mapped(const Vars::Descriptor& d) { return Vars::var_mapped(d); }
    /** @brief Whether @p d's register sits in its scope's block: site rows
     *  below 900, library's above; alarms 800-899 (library's at the top).
     *  See @ref dev_modbus. */
    bool in_block(const Vars::Descriptor& d) {
        const uint16_t r = d.bind.reg;
        return (d.scope == Vars::SCOPE_A) ? ((r >= 800 && r <= 899) ||
                                             (r >= 986 && r <= 999))
                                          : ((r >= 1 && r <= 799) ||
                                             (r >= 900 && r <= 985));
    }
}

TEST_CASE("site: no two rows claim the same Modbus register") {
    // Registers are hand-assigned so inserting a row never renumbers the
    // map; nothing else catches a duplicate.
    SiteBank f;
    uint16_t checked = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Descriptor& a = *f.bank.var(i).meta();
        if (!mapped(a)) continue;
        INFO("'" << a.name << "' has no register");
        CHECK(a.bind.reg != 0);
        checked++;
        for (uint16_t j = (uint16_t)(i + 1); j < f.bank.count(); j++) {
            const Vars::Descriptor& b = *f.bank.var(j).meta();
            if (!mapped(b)) continue;
            INFO("'" << a.name << "' and '" << b.name << "' both claim register "
                     << a.bind.reg);
            CHECK(a.bind.reg != b.bind.reg);
        }
    }
    CHECK(checked > 0);
}

TEST_CASE("site: every register sits in its scope's block") {
    // Uniqueness alone would let an alarm take register 750 and read as a parameter downstream.
    SiteBank f;
    uint16_t alarms = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Descriptor& d = *f.bank.var(i).meta();
        if (!mapped(d)) continue;
        INFO("'" << d.name << "' holds register " << d.bind.reg
                 << ", outside the block for its scope");
        CHECK(in_block(d));
        if (d.scope == Vars::SCOPE_A) alarms++;
    }
    // Platform alarms are the library's own rows; a missed registration passes silently.
    CHECK(alarms >= 6);
}



TEST_CASE("site: no two local rows claim the same terminal") {
    // A local row has no register to address, so `reg` carries the PIN
    // instead. Two rows on one pin both attach and both publish the same
    // level, which reads as two independent contacts that never disagree.
    SiteBank f;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Descriptor& a = *f.bank.var(i).meta();
        if (a.bind.transport != Vars::XP_LOCAL) continue;
        for (uint16_t j = (uint16_t)(i + 1); j < f.bank.count(); j++) {
            const Vars::Descriptor& b = *f.bank.var(j).meta();
            if (b.bind.transport != Vars::XP_LOCAL) continue;
            if (a.scope != b.scope) continue;
            INFO(doctest::String(a.name) << " and " << doctest::String(b.name)
                     << " both claim terminal " << a.bind.reg);
            CHECK(a.bind.reg != b.bind.reg);
        }
    }
}

TEST_CASE("site: an alarm carries a code the ALARMS page can show") {
    // The page prints AlarmsPage::CODE_W characters and no more, so a longer
    // one is not a long code - it is a truncated one, on the display an
    // operator reads at three in the morning.
    SiteBank f;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Descriptor& d = *f.bank.var(i).meta();
        if (d.scope != Vars::SCOPE_A) continue;
        const char* code = Vars::alarm_spec(d).code;
        INFO(doctest::String(d.name) << ": its alarm code");
        REQUIRE(code != nullptr);
        CHECK(code[0] != '\0');
        CHECK(strlen(code) <= 9u);   // AlarmsPage::CODE_W
    }
}

TEST_CASE("site: every numeric row's default sits inside its own band") {
    // A default outside the band is one an operator cannot type back in: the
    // band refuses the write, so the row can never return to where it booted.
    SiteBank f;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Descriptor& d = *f.bank.var(i).meta();
        // Through the checked accessor: an analog row keeps its band in the
        // PARAMETER table and a text row keeps a pool slot there, so reading
        // the union directly hands you another arm's bytes as numbers.
        const Vars::NumBand band = Vars::num_band(d);
        const float lo = Vars::as_f32(d.type, band.min);
        const float hi = Vars::as_f32(d.type, band.max);
        if (lo == 0.0f && hi == 0.0f) continue;      // no band declared
        const float def = Vars::as_f32(d.type, d.def);
        INFO(doctest::String(d.name) << ": default " << def
                 << " against " << lo << " .. " << hi);
        CHECK(lo <= hi);
        CHECK(def >= lo);
        CHECK(def <= hi);
    }
}

}  // namespace site_names
