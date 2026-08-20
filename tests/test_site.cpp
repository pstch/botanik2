/**
 * @file test_site.cpp
 * @brief The selected site: names resolve, bindings hold, structs stay their size.
 *
 * Each section is its own namespace, isolating fixtures and helpers.
 */

#include <cstdint>
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
#include "sites/gh_mlb41/tasks.h"
#include "libbotanik/modbus/rtu/plan.h"

// site_names

namespace site_names {


using namespace Botanik;

namespace {

    /** @brief A bank over the site's real tables, seeded as bring-up leaves
     *  it - not the firmware (drags in UART/terminal). */
    struct SiteBank {
        Vars::Bank bank;
        SiteBank() { TestSite::build(bank); }
    };

    const Botanik::SiteDescriptor& site() { return Site::g_site; }

}  // namespace

TEST_CASE("site: the name index is built, and resolves every row") {
    // Missing the index array still resolves (by walking the table), hiding
    // the cost silently - assert it here.
    SiteBank f;
    INFO("the bank builds its name index at freeze()");
    CHECK(f.bank.name_index_built());

    // ...and the search has to land on the same rows the scan did.
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const char* n = f.bank.var(i).meta()->name;
        INFO("row " << i << " '" << n << "'");
        CHECK(f.bank.find(n).index == i);
        // The bounded form shares the mapping: a PREFIX of a real name isn't that name.
        CHECK(f.bank.find(n, (uint16_t)std::strlen(n)).index == i);
        CHECK(f.bank.find(n, (uint16_t)(std::strlen(n) - 1)).index != i);
    }
    CHECK(f.bank.find("no_such_variable_at_all").index == Vars::VAR_NONE);
}

TEST_CASE("site: every published variable lands in a sensible group") {
    // Groups derive from the name; a row with no prefix files under its own
    // full name, a group of one.
    SiteBank f;
    uint16_t published = 0, groups = 0;
    const char* seen[Vars::VAR_GROUPS_MAX] = {};
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Descriptor& d = *f.bank.var(i).meta();
        if (!Vars::var_published(d)) continue;
        published++;
        const char* g = Vars::var_group(d);
        INFO("'" << d.name << "' groups as '" << g << "'");
        CHECK(g[0] != 0);
        CHECK(std::strcmp(g, d.name) != 0);   // i.e. the name HAS a prefix
        bool known = false;
        for (uint16_t k = 0; k < groups; k++)
            if (std::strcmp(seen[k], g) == 0) { known = true; break; }
        if (!known) {
            REQUIRE(groups < Vars::VAR_GROUPS_MAX);   // else var_group() interns "?"
            seen[groups++] = g;
        }
    }
    CHECK(published > 0);
    CHECK(groups > 1);
}

TEST_CASE("site: every home-field name resolves to a variable") {
    SiteBank f;
    REQUIRE(site().home_refs != nullptr);   // else the home screen renders blank
    for (uint8_t i = 0; i < site().home_field_count; i++) {
        const Vars::HomeVar& h = site().home_fields[i];
        INFO("home field '" << h.label << "' = '" << h.name << "'");
        CHECK(f.bank.find(h.name).index != Vars::VAR_NONE);
    }
}

TEST_CASE("site: every alarm declares a code and a policy") {
    // The code drives the ALARMS table's first column and the alarm bar; a null one renders as garbage.
    SiteBank f;
    uint16_t alarms = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Descriptor& d = *f.bank.var(i).meta();
        if (d.scope != Vars::SCOPE_A) continue;
        alarms++;
        const Vars::AlarmSpec sp = Vars::alarm_spec(d);
        INFO("alarm '" << d.name << "'");
        REQUIRE(sp.code != nullptr);
        CHECK(sp.code[0] != 0);
        CHECK(std::strlen(sp.code) <= 9);   // AlarmsPage::CODE_W
        // desc IS the ALARM_* log message (one line, no '\n'); 40 is the house rule in logs/format.h.
        CHECK(std::strlen(d.desc != nullptr ? d.desc : "") <= 40);
        CHECK(d.type == Vars::T_BOOL);      // a condition is a boolean
    }
    CHECK(alarms > 0);
}

TEST_CASE("site: the alarm model finds every alarm row, in table order") {
    SiteBank f;
    Logs::Logger logger;
    // One scan: site alarms and library platform-health rows are all SCOPE_A rows.
    Vars::BankAlarms rows;
    rows.bind(&f.bank);
    Alarms::AlarmController m(Botanik::SetupContext(), &rows);
    m.setup(0);   // the scan is on_setup(): the rows exist now
    uint8_t slot = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Descriptor& d = *f.bank.var(i).meta();
        if (d.scope != Vars::SCOPE_A) continue;
        INFO("alarm '" << d.name << "' should be condition " << (int)slot);
        CHECK(rows.row(slot).index == i);                 // the row IS the condition
        CHECK(Utils::compare_str(m.condition(slot).name(), d.name));
        slot++;
    }
    CHECK(m.count() == slot);
    // count() saturates at ALARM_SLOTS; declared_count() does not, so the
    // pair tells "filled the slots exactly" from "overran, tail undriven".
    CHECK(m.declared_count() == slot);

    // A site missing BOTANIK_PLATFORM_ALARM_VARS still builds/boots; the
    // health monitor just never raises anything. Names it binds:
    static const char* const PLATFORM[] = {
        "alarm_system_reset", "alarm_cpu_xrun", "alarm_cpu_load",
        "alarm_stack",        "alarm_cpu_jitter", "alarm_flash_error",
    };
    for (const char* n : PLATFORM) {
        INFO("platform alarm '" << n << "' - is BOTANIK_PLATFORM_ALARM_VARS in "
             "the site's g_vars[]?");
        CHECK(m.index_of(n) >= 0);
    }
}

namespace {

/**
 * @brief Every control program the site runs, constructed together.
 *
 * A new program goes HERE, and that is the price of them being firmware
 * members rather than a table: two cases below walk this list, and a program
 * missing from it is a program whose names nothing checks.
 */
struct AllPrograms {
    /** @brief The site owns the tracker, so the harness has to too. Never
     *  registered here, so it stays unlocated and day_phase holds its phase -
     *  which is the boot path, and the one worth walking. */
    Botanik::Sun::SunPositionTracker sun;

    Site::AcGridWatch          ac_grid;
    Site::AcLocalWatch         ac_local;
    Site::DcWatch              dc;
    Site::TempRHZone           zones;
    Site::StratSensor          strat;
    Site::DayPhaseController   phase;
    Site::TargetTempController target;
    Site::CoolingController    cooling;
    Site::DestratController    destrat;
    Site::HumidityController   humidity;
    Site::IrrigationModulation irr_mod;
    Site::WaterMeters          irr_meters;
    Site::IrrigationCycle      irrigation;
    Site::ScreenController     screen;
    Site::WindowsController    windows;
    Site::IntractorController  intractors;
    Site::FanController        fans;
    Site::SkirtController      skirt;

    Botanik::Plc::Program* all[18];

    AllPrograms(const Botanik::SetupContext& ctx, Vars::Bank& io)
        : sun(ctx),
          ac_grid(ctx, io), ac_local(ctx, io), dc(ctx, io), zones(ctx, io),
          strat(ctx, io), phase(ctx, io, sun), target(ctx, io), cooling(ctx, io),
          destrat(ctx, io), humidity(ctx, io),
          irr_mod(ctx, io), irr_meters(ctx, io), irrigation(ctx, io), screen(ctx, io), windows(ctx, io),
          intractors(ctx, io), fans(ctx, io), skirt(ctx, io) {
        Botanik::Plc::Program* const list[] = {
            &ac_grid, &ac_local, &dc, &zones, &strat, &phase, &target,
            &cooling, &destrat, &humidity, &irr_mod, &irr_meters, &irrigation, &screen,
            &windows, &intractors, &fans, &skirt
        };
        for (uint8_t i = 0; i < 18; i++) all[i] = list[i];
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
    f.bank.set_health_hold(false);
#ifdef TARGET_SIMULATOR
    Site::simulate(f.bank, 1000);
#endif
    // The site's programs are members of its firmware class now, so there is no
    // table to walk: AllPrograms is the list instead, and the check it buys is
    // that no program names a row the tables do not declare.
    Botanik::Tasks::Registry prog_reg;
    AllPrograms progs(Botanik::SetupContext(prog_reg), f.bank);
    for (Plc::Program* p : progs.all)
        p->step(1000);      // setup() binds the names, then one activation

    INFO("a non-zero count means a table or a control program names a variable "
         "that is not in vars.h; the bank logs which one");
    CHECK(f.bank.unresolved_count() == 0);

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
    TestSite::build(bank);
    const uint16_t before = bank.unresolved_count();

    AllPrograms progs(Botanik::SetupContext(reg), bank);
    for (Botanik::Plc::Program* p : progs.all) p->register_tasks();
    reg.setup(0);

    INFO("a name that is absent, or a row bound at the wrong type, counts here");
    CHECK(bank.unresolved_count() == before);
}

TEST_CASE("local io: a terminal reaches the bank as a row, active-low folded in") {
    // What the RTU client is to the field bus, LocalInputs is to the wiring
    // that lands on the controller. The row is the value: nothing above it
    // knows which side of the boundary the bit came from.
    SiteBank f;
    Botanik::Tasks::Registry reg;
    Botanik::Io::LocalInputs local(Botanik::SetupContext(reg), f.bank);

    local.register_tasks();
    reg.setup(0);
    INFO("the rows are found by walking the table, not from a channel list "
         "beside it");
    REQUIRE(local.count() >= 1);

    const Vars::VarRef contact = f.bank.find("pwr_upsLowBat");
    REQUIRE(contact.index != Vars::VAR_NONE);
    const uint8_t ch = (uint8_t)f.bank.var(contact).meta()->bind.reg;

    Botanik::HAL::DigitalIn::inject(ch, false);
    reg.run_phase(Botanik::Tasks::PHASE_INPUT, 100);
    CHECK_FALSE(f.bank.var(contact).b());
    CHECK(f.bank.var(contact).valid());

    Botanik::HAL::DigitalIn::inject(ch, true);
    reg.run_phase(Botanik::Tasks::PHASE_INPUT, 200);
    CHECK(f.bank.var(contact).b());

    INFO("an edge-counted terminal is NOT one of them: it is a counter, and "
         "one pin configured two ways is a hardware fault");
    for (uint8_t i = 0; i < Site::pulse_input_count; i++) {
        const Vars::VarRef r = f.bank.find(Site::pulse_inputs[i].name);
        REQUIRE(r.index != Vars::VAR_NONE);
        CHECK((f.bank.var(r).meta()->flags & Vars::F_PULSE) != 0);
    }
    uint8_t polled = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Descriptor& d = f.bank.descriptor(i);
        if (d.bind.transport == Vars::XP_LOCAL && d.scope == Vars::SCOPE_DI
            && (d.flags & Vars::F_PULSE) == 0) polled++;
    }
    INFO("the site has one to skip, or this case proves nothing");
    REQUIRE(Site::pulse_input_count > 0);
    CHECK(local.count() == polled);
}

TEST_CASE("site: the standalone components resolve their config names too") {
    // A standalone component's miss reads 0 instead of failing - it drifted
    // once (geo_lat_deg vs geo_latitude), parking the site on a flat 12-hour equator.
    SiteBank f;
    Logs::Logger logger;
    Sun::SunPositionTracker sun(Botanik::SetupContext(nullptr, &logger));
    Vars::ConfigBinder binder(Botanik::SetupContext(nullptr, &logger), &f.bank);
    Vars::bind_sun(binder, sun);

    // geo_serial defaults non-zero (a group with no rows is never pushed at
    // all); host clock is real.
    binder.apply_all();
    sun.step(1000);
    REQUIRE(sun.ready());          // it actually computed, rather than returning early

    INFO("the geo binding names a config variable that is not in vars.h");
    CHECK(f.bank.unresolved_count() == 0);
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

TEST_CASE("site: no row reads the payload arm it did not set") {
    // str_off is WRITTEN through (copy_bounded); text_slot() must return
    // capacity 0 for a numeric row, or it gets an arbitrary offset.
    SiteBank f;
    uint16_t text_rows = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Descriptor& d = *f.bank.var(i).meta();
        INFO("row " << i << " '" << d.name << "'");
        if (d.type == Vars::T_STR) {
            text_rows++;
            CHECK(Vars::text_slot(d).cap != 0);
            // Bank::init() lays the slot out; an overflow leaves offset 0, no room.
            CHECK((uint32_t)f.bank.var(i).str_off() + Vars::text_slot(d).cap
                      <= Botanik::Config::STR_POOL_BYTES);
            // A band is meaningless here; must read as unset, not pool halves as Scalars.
            CHECK(Vars::num_band(d).min.u == 0);
            CHECK(Vars::num_band(d).max.u == 0);
        } else {
            CHECK(Vars::text_slot(d).cap == 0);
            CHECK(f.bank.var(i).str_off() == 0);
        }
    }
    CHECK(text_rows > 0);   // else the T_STR half of the union is untested
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

TEST_CASE("site: every analog calibration resolves and is coherent") {
    // No row points at these blocks yet (AI-1/AO-1 maps open); keeps them honest.
    SiteBank f;
    REQUIRE(Site::g_analog_cal_count > 0);
    for (uint8_t b = 0; b < Site::g_analog_cal_count; b++) {
        const Vars::AnalogCal& c = *Site::g_analog_cals[b];
        const Vars::Tunable* fields[] = { &c.raw_min, &c.raw_max, &c.eng_min,
                                          &c.eng_max, &c.min,     &c.max };
        for (uint8_t i = 0; i < 6; i++) {
            if (fields[i]->param == nullptr) continue;      // K(): nothing to resolve
            INFO("calibration block " << (int)b << " field " << (int)i
                 << " names '" << fields[i]->param << "'");
            CHECK(f.bank.find(fields[i]->param).index != Vars::VAR_NONE);
        }
        // Both ends of a span must share a unit, or the scale is off by
        // their ratio; mV and uA both look plausible, and nothing else checks this.
        const Vars::VarRef lo = f.bank.find(c.raw_min.param);
        const Vars::VarRef hi = f.bank.find(c.raw_max.param);
        if (lo.index != Vars::VAR_NONE && hi.index != Vars::VAR_NONE) {
            INFO("block " << (int)b << ": raw endpoints disagree on their unit");
            CHECK(Utils::compare_str(f.bank.var(lo).meta()->unit,
                                     f.bank.var(hi).meta()->unit));
        }
        const Vars::VarRef elo = f.bank.find(c.eng_min.param);
        const Vars::VarRef ehi = f.bank.find(c.eng_max.param);
        if (elo.index != Vars::VAR_NONE && ehi.index != Vars::VAR_NONE) {
            INFO("block " << (int)b << ": engineering endpoints disagree on their unit");
            CHECK(Utils::compare_str(f.bank.var(elo).meta()->unit,
                                     f.bank.var(ehi).meta()->unit));
        }
        // A degenerate span divides by zero; scale() floors it, but that's a site mistake.
        const Vars::AnalogValues v = Vars::analog_values(&f.bank, &c);
        INFO("block " << (int)b << " has a zero-width raw span");
        CHECK(v.raw_max != v.raw_min);
        INFO("block " << (int)b << " has an inverted plausibility band");
        CHECK(v.min <= v.max);
    }
}

TEST_CASE("analog: the calibration converts both ways") {
    // A tunable with no parameter is its constant, so K()s need no bank at all.
    const Vars::AnalogCal cal = {
        Vars::K(4000.0f), Vars::K(6000.0f),    // 4-6 V across the divider
        Vars::K(20.0f),   Vars::K(30.0f),      // is a 20-30 V bus
        Vars::K(18.0f),   Vars::K(32.0f),      // believable a little either side
    };
    const Vars::AnalogValues v = Vars::analog_values(nullptr, &cal);
    CHECK(v.scale()  == doctest::Approx(0.005f));
    CHECK(v.offset() == doctest::Approx(0.0f));
    CHECK(v.to_eng(4000.0f) == doctest::Approx(20.0f));
    CHECK(v.to_eng(6000.0f) == doctest::Approx(30.0f));
    CHECK(v.to_eng(5000.0f) == doctest::Approx(25.0f));
    // An output runs it backwards.
    CHECK(v.to_raw(25.0f) == doctest::Approx(5000.0f));
    // The band is wider than the calibrated span: past the endpoint is still a measurement.
    CHECK(v.plausible(31.0f));
    CHECK_FALSE(v.plausible(33.0f));
    CHECK_FALSE(v.plausible(17.0f));
}

TEST_CASE("analog: a degenerate calibration yields no infinities") {
    const Vars::AnalogCal flat = {
        Vars::K(1000.0f), Vars::K(1000.0f),    // both endpoints the same
        Vars::K(0.0f),    Vars::K(10.0f),
        Vars::K(0.0f),    Vars::K(10.0f),
    };
    const Vars::AnalogValues v = Vars::analog_values(nullptr, &flat);
    CHECK(v.scale() == 0.0f);
    CHECK(v.to_eng(9999.0f) == doctest::Approx(0.0f));   // not inf/NaN
    CHECK(v.to_raw(5.0f) == doctest::Approx(1000.0f));
}

TEST_CASE("analog: a tunable falls back to its constant when the parameter is absent") {
    SiteBank f;
    // R() falls back to its own value when the name is missing, not to zero.
    CHECK(Vars::tunable(&f.bank, Vars::R("no_such_parameter", 42.0f))
              == doctest::Approx(42.0f));
    CHECK(Vars::tunable(&f.bank, Vars::K(7.0f)) == doctest::Approx(7.0f));
    // ...and the miss is counted, which is what the catch-all test asserts.
    CHECK(f.bank.unresolved_count() > 0);
}

}  // namespace site_names

// site_bindings

namespace site_bindings {

using namespace Botanik;
using namespace Botanik::Vars;

namespace {

// One frozen bank over the site's rows, built the way the firmware builds it.
// Static: the descriptors are flash and the slots are the bank's, so every case
// can read the same one.
const Bank& tables() {
    static Bank b;
    static bool once = (TestSite::build(b), true);
    (void)once;
    return b;
}

bool on_the_bus(const Descriptor& d) {
    return d.bind.transport == XP_MODBUS;
}

/** @brief Registers, or coils, that @p d occupies: [reg, reg + regs). */
uint32_t span_end(const Descriptor& d) {
    const uint16_t n = d.bind.regs != 0 ? d.bind.regs : 1;
    return (uint32_t)d.bind.reg + n;
}

/** @brief doctest stringifies const char* as an address; route through std::string. */
std::string nm(const Descriptor& d) { return std::string(d.name); }

}  // namespace

TEST_CASE("site bindings: no two field-bus rows overlap on a device") {
    const Bank& t = tables();

    for (uint16_t i = 0; i < t.count(); ++i) {
        const Descriptor& a = t.descriptor(i);
        if (!on_the_bus(a)) continue;
        for (uint16_t j = (uint16_t)(i + 1); j < t.count(); ++j) {
            const Descriptor& b = t.descriptor(j);
            if (!on_the_bus(b)) continue;
            // Same device and address space: bit/register function codes are numbered apart.
            if (a.bind.device != b.bind.device) continue;
            if (Modbus::is_bit(a.bind.fc) != Modbus::is_bit(b.bind.fc)) continue;
            // A read and a write of the same coil are the same coil.
            const bool overlap = (uint32_t)a.bind.reg < span_end(b) &&
                                 (uint32_t)b.bind.reg < span_end(a);
            INFO("dev " << (int)a.bind.device
                        << ": '" << nm(a) << "' [" << a.bind.reg << ","
                        << span_end(a) << ") overlaps '" << nm(b) << "' ["
                        << b.bind.reg << "," << span_end(b) << ")");
            CHECK_FALSE(overlap);
        }
    }
}

TEST_CASE("site bindings: a row's span matches the width its wire type needs") {
    const Bank& t = tables();

    for (uint16_t i = 0; i < t.count(); ++i) {
        const Descriptor& d = t.descriptor(i);
        if (!on_the_bus(d)) continue;
        if (Modbus::is_bit(d.bind.fc)) {
            INFO("'" << nm(d) << "' is a bit row spanning " << d.bind.regs);
            CHECK(d.bind.regs <= 1);
            continue;
        }
        // 32-bit wire values take a register pair; 16-bit ones take one.
        const uint16_t want = (uint16_t)((type_width(d.bind.wire) + 1) / 2);
        INFO("'" << nm(d) << "' wire type wants " << want
                 << " register(s), declares " << d.bind.regs);
        CHECK(d.bind.regs == want);
    }
}

TEST_CASE("site bindings: every field-bus input can go stale") {
    // expire_stale() skips timeout_ms==0; a row on a dead device with no
    // failed-read invalidation would hold GOOD forever.
    const Bank& t = tables();

    for (uint16_t i = 0; i < t.count(); ++i) {
        const Descriptor& d = t.descriptor(i);
        if (!on_the_bus(d) || d.role != ROLE_INPUT) continue;
        INFO("'" << nm(d) << "' is a field-bus input with no timeout");
        CHECK(d.timeout_ms > 0);
    }
}

TEST_CASE("site bindings: no read block can exceed a device's own request limit") {
    // The SDM630 refuses more than 40 parameters (80 registers) with an
    // exception; checks the site stays inside MODBUS_MAX_REGS_PER_BLOCK.
    const Bank& t = tables();

    for (uint16_t i = 0; i < t.count(); ++i) {
        const Descriptor& d = t.descriptor(i);
        if (!on_the_bus(d) || Modbus::is_bit(d.bind.fc)) continue;
        INFO("'" << nm(d) << "' spans " << d.bind.regs << " registers");
        CHECK(d.bind.regs <= Config::MODBUS_MAX_REGS_PER_BLOCK);
    }
}

}  // namespace site_bindings

// sizes

namespace sizes {

// Concrete instantiations matching what the simulator builds (botanik_sim/main.cpp).
using HomePage_t   = Botanik::Pages::HomePage;
using StatusPage_t = Botanik::Pages::StatusPage;

// Guards a type against runaway growth; the failure message names the type
// and both numbers.
#define REPORT_SIZE(T, budget)                                               \
    CHECK_MESSAGE(sizeof(T) <= (size_t)(budget),                             \
                  #T " = " << sizeof(T) << " bytes, budget " << (budget))

// Budgets are loose ceilings, a tripwire for bloat; page sizes move with
// Config::*_LINES / LOG_* buffers, not code churn.
TEST_CASE("sizeof: terminal & input objects") {
    // AnsiTerminal owns two GRID_ROWS x GRID_COLS cell grids (~8 KB), diffed
    // by present(); size tracks the grid, not device state.
    REPORT_SIZE(Botanik::Terminal::AnsiTerminal, 12288);
    // The editor borrows its buffer/row arrays; bytes live in EditorBuffers.
    REPORT_SIZE(Botanik::Terminal::TextEditor,    256);
    REPORT_SIZE(Botanik::Terminal::InputParser,   512);
}

// Actual editor footprint: moves with SHELL_INPUT_LEN / EDITOR_MAX_ROWS / NOTES_*.
TEST_CASE("sizeof: editor buffers") {
    using ShellBufs = Botanik::Terminal::EditorBuffers<Botanik::Config::SHELL_INPUT_LEN,
                                                       Botanik::Config::EDITOR_MAX_ROWS>;
    REPORT_SIZE(ShellBufs, 2048);
}

TEST_CASE("sizeof: pages") {
    REPORT_SIZE(HomePage_t,                     32768);
    REPORT_SIZE(StatusPage_t,                    4096);
    REPORT_SIZE(Botanik::Pages::LogsPage,       65536);
    REPORT_SIZE(Botanik::Pages::ShellPage,      65536);
    REPORT_SIZE(Botanik::Pages::MetricPage,      4096);
    REPORT_SIZE(Botanik::Pages::ShellScrollback,16384);
    REPORT_SIZE(Botanik::Pages::LogsScrollback, 32768);
    REPORT_SIZE(Botanik::Pages::HomeScrollback,  4096);
}

TEST_CASE("sizeof: logging & serial") {
    REPORT_SIZE(Botanik::Logs::LogEntry,             1024);
    REPORT_SIZE(Botanik::Logs::Logger,               1024);
    REPORT_SIZE(Botanik::Logs::BufferedLogWriter,   32768);
    REPORT_SIZE(Botanik::Logs::SerialLogWriter,     32768);
    REPORT_SIZE(Botanik::Utils::BufferedSerial,      8192);
}

TEST_CASE("sizeof: the site's variable table and value store") {
    // Parameters, status, actuators and alarms are rows of one table; descriptors are flash.
    REPORT_SIZE(Botanik::Vars::Descriptor, 128);
    REPORT_SIZE(Botanik::Vars::Slot, 32);
}

}  // namespace sizes


#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
TEST_CASE("TEMP: what the planner does with unit 21") {
    site_names::SiteBank f;
    Botanik::Modbus::Planner plan;
    plan.plan(f.bank, 38400, 20, &Site::g_device_table);
    MESSAGE("rows=" << plan.row_count() << " blocks=" << plan.block_count()
            << " dropped=" << plan.rows_dropped() << " unscanned=" << plan.rows_unscanned());
    for (uint16_t b = 0; b < plan.block_count(); b++) {
        const Botanik::Modbus::Block& blk = plan.block(b);
        if (blk.device != 21 && blk.device != 5 && blk.device != 41) continue;
        MESSAGE("blk dev=" << (int)blk.device << " fc=" << (int)blk.fc
                << " reg=" << blk.reg << " regs=" << blk.regs
                << " rows=" << blk.row_count << " period=" << blk.period_ms);
        for (uint16_t r = 0; r < blk.row_count; r++)
            MESSAGE("   row " << std::string(f.bank.descriptor(plan.row(blk.first_row + r)).name));
    }
}
#endif
