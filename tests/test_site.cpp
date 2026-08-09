/**
 * @file test_site.cpp
 * @brief The selected site: names resolve, bindings hold, structs stay their size.
 *
 * Each section below is the former test_<name>.cpp, kept in its own
 * namespace so their fixtures and helpers cannot collide.
 */

#include <cstdint>              // uint16_t
#include <cstring>              // strcmp
#include "libbotanik_site.h"    // resolves BOTANIK_SITE -> BOTANIK_SITE_HEADER
#include "libbotanik.h"         // Vars::Bank::find / unresolved_count
#include "doctest.h"
#include BOTANIK_SITE_HEADER
#if BOTANIK_FEATURE_SUN_TRACKER
#  include "libbotanik/sun/sun_tracker.h"
#endif
#include <string>   // doctest streams a const char* as a pointer
#include <cstddef>              // size_t / offsetof

// --------------------------------------------------------------------------
// site_names
// --------------------------------------------------------------------------

namespace site_names {


using namespace Botanik;

namespace {

    /** @brief A bank over the site's real tables, seeded as bring-up leaves it.
     *  Deliberately not the firmware: this is about the tables, and building a
     *  whole firmware would drag in the UART and the terminal. */
    struct SiteBank {
        Vars::Bank bank;
        SiteBank() : bank(Site::g_site.vars) { bank.init(); }
    };

    const Botanik::SiteDescriptor& site() { return Site::g_site; }

}  // namespace

TEST_CASE("site: the name index is built, and resolves every row") {
    // Every lookup goes through Bank::find(). A site that forgot the index
    // scratch in its VarTables still resolves - by walking the whole table - so
    // nothing breaks visibly and the cost just sits there. Say so here instead.
    SiteBank f;
    INFO("the site's VarTables needs an `index` array of VAR_TOTAL uint16_t");
    CHECK(f.bank.name_index_built());

    // ...and the search has to land on the same rows the scan did.
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const char* n = f.bank.var(i).meta()->name;
        INFO("row " << i << " '" << n << "'");
        CHECK(f.bank.find(n).index == i);
        // The bounded form shares the mapping, so it has to agree - including
        // that a PREFIX of a real name is not that name.
        CHECK(f.bank.find(n, (uint16_t)std::strlen(n)).index == i);
        CHECK(f.bank.find(n, (uint16_t)(std::strlen(n) - 1)).index != i);
    }
    CHECK(f.bank.find("no_such_variable_at_all").index == Vars::VAR_NONE);
}

TEST_CASE("site: every published variable lands in a sensible group") {
    // Groups are derived from the name now, so there is no table to check - but
    // the derivation has to hold: a published row with no prefix would file
    // itself under its own full name and appear as a group of one.
    SiteBank f;
    uint16_t published = 0, groups = 0;
    const char* seen[Vars::VAR_GROUPS_MAX] = {};
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Variable& d = *f.bank.var(i).meta();
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
            REQUIRE(groups < Vars::VAR_GROUPS_MAX);   // else the mode line truncates
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
    // An alarm is a row now, so there is no name to resolve - but the code is
    // what the ALARMS table's first column and the alarm bar show, and a null
    // one would render as garbage.
    SiteBank f;
    uint16_t alarms = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Variable& d = *f.bank.var(i).meta();
        if (d.scope != Vars::SCOPE_A) continue;
        alarms++;
        const Vars::AlarmSpec sp = Vars::alarm_spec(d);
        INFO("alarm '" << d.name << "'");
        REQUIRE(sp.code != nullptr);
        CHECK(sp.code[0] != 0);
        CHECK(std::strlen(sp.code) <= 9);   // AlarmsPage::CODE_W
        // The desc IS the ALARM_* log message (AlarmSink::log_start), and it is
        // one line with no '\n' to break at. 40 is the house rule in
        // logs/format.h, under every topic's real first-line budget.
        CHECK(std::strlen(d.desc != nullptr ? d.desc : "") <= 40);
        CHECK(d.type == Vars::T_BOOL);      // a condition is a boolean
    }
    CHECK(alarms > 0);
}

TEST_CASE("site: the alarm model finds every alarm row, in table order") {
    SiteBank f;
    Logs::Logger logger;
    // One scan over one table: the site's alarms and the library's spliced-in
    // platform-health rows are all just SCOPE_A rows now.
    Alarms::AlarmController m(&f.bank, &logger);
    uint8_t slot = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Variable& d = *f.bank.var(i).meta();
        if (d.scope != Vars::SCOPE_A) continue;
        INFO("alarm '" << d.name << "' should be condition " << (int)slot);
        CHECK(m.condition(slot).ref.index == i);          // the row IS the condition
        CHECK(Utils::compare_str(m.condition(slot).name, d.name));
        slot++;
    }
    CHECK(m.count() == slot);

    // A site that left BOTANIK_PLATFORM_ALARM_VARS out of its table would build
    // and boot; the health monitor would just never be able to raise anything.
    // These are the names it binds.
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

TEST_CASE("site: nothing the site touches fails to resolve") {
    // The catch-all. Resolve every declarative table, then RUN the site: every
    // control program once, and the simulated field if this build has one. A
    // name misspelled in tasks.h or sim.h is invisible to the table walks above
    // (it is a string literal in code, not a row anywhere) but it still goes
    // through Bank::find(), so the counter sees it.
    SiteBank f;

    for (uint8_t i = 0; i < site().home_field_count; i++)
        (void)f.bank.find(site().home_fields[i].name);

    Logs::Logger logger;
    Alarms::AlarmController m(&f.bank, &logger);
    (void)m;

    // The state the programs actually run in. Not a precondition for the writes
    // below - Var::set() is not gated, because a two-pointer handle has no route
    // to the latch; authority is enforced where a writer is DISPATCHED (the scan
    // driver, and BotanikFirmware's on_control hook). This is here so the names
    // are resolved against a bank in its running state rather than its
    // powered-on-and-stopped one.
    f.bank.enable_control(Vars::Bank::INHIBIT_HEALTH);
#ifdef TARGET_SIMULATOR
    Site::simulate(f.bank, 1000);
#endif
    // step() is setup() (which binds the names) then one activation - the same
    // two things the driver does, said the same way, so this cannot drift from
    // the driver's contract the way a hand-rolled bind+run pair would.
    for (uint8_t t = 0; t < site().program_count; t++) {
        Plc::Program* p = site().programs[t];
        REQUIRE(p != nullptr);
        p->attach(f.bank);
        p->step(1000);
    }

    INFO("a non-zero count means a table or a control program names a variable "
         "that is not in vars.h; the bank logs which one");
    CHECK(f.bank.unresolved_count() == 0);

    // Nothing the site RUNS may write a parameter - Bank::set() is the
    // operator's path alone. Catches a write aimed at a row nobody meant: names
    // cannot drift, but a stale index can, and index zero is a real config row.
    for (uint16_t k = 0; k < f.bank.param_count(); k++) {
        const Vars::Var h = f.bank.param(k);
        if (h.meta()->type == Vars::T_STR) continue;   // lives in the pool
        INFO("parameter '" << h.meta()->name << "' was written by a control "
             "program or the simulated field");
        CHECK(h.raw().u == h.meta()->def.u);
    }
}

#if BOTANIK_FEATURE_SUN_TRACKER
TEST_CASE("site: the standalone components resolve their config names too") {
    // A standalone component is not in the SiteDescriptor, so the names it looks
    // up are reached by no table walk and no task run - and a miss reads 0
    // rather than failing. It had drifted exactly so: the tracker read
    // geo_lat_deg/geo_lon_deg against a fixture declaring geo_latitude, putting
    // the site on the equator with a 12-hour day all year, and nothing looked.
    SiteBank f;
    Logs::Logger logger;
    Sun::SunPositionTracker sun(&f.bank, &logger);

    // One beat is enough to reach the lookups: geo_serial defaults non-zero so
    // the "not configured" early-out does not fire, and the host wall clock is
    // real so the validity gate passes.
    sun.step(1000);
    REQUIRE(sun.ready());          // it actually computed, rather than returning early

    INFO("SunPositionTracker names a config variable that is not in vars.h");
    CHECK(f.bank.unresolved_count() == 0);
}
#endif

TEST_CASE("site: the parameters are one contiguous block") {
    // The PARAMS page and `set` address parameters by a dense 0-based index over
    // that run, so a ROLE_PARAM row outside it is simply unreachable - shown
    // nowhere, editable nowhere, and silently not persisted.
    SiteBank f;
    uint16_t total = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++)
        if (f.bank.var(i).meta()->role == Vars::ROLE_PARAM) total++;
    CHECK(total == f.bank.param_count());
    CHECK(f.bank.param_count() > 0);
    for (uint16_t k = 0; k < f.bank.param_count(); k++)
        CHECK(f.bank.param(k).meta()->role == Vars::ROLE_PARAM);
}

TEST_CASE("site: no two variables share a name") {
    // Names are the identity now: a duplicate makes the second row unreachable
    // (find() returns the first), so a status row or an alarm would quietly
    // track its namesake instead.
    SiteBank f;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const char* n = f.bank.var(i).meta()->name;
        REQUIRE(n != nullptr);
        INFO("row " << i << " = '" << n << "'");
        CHECK(f.bank.find(n).index == i);
    }
}


TEST_CASE("site: no row reads the payload arm it did not set") {
    // The union's whole hazard: str_off is WRITTEN through (copy_bounded into
    // the pool), so a numeric row read as text would hand it an arbitrary
    // offset. text_slot() must hand back capacity 0 there, which every writer
    // already treats as "no slot".
    SiteBank f;
    uint16_t text_rows = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Variable& d = *f.bank.var(i).meta();
        INFO("row " << i << " '" << d.name << "'");
        if (d.type == Vars::T_STR) {
            text_rows++;
            CHECK(Vars::text_slot(d).cap != 0);
            // Bank::init() lays the slot out, so the row must actually have got
            // one: an overflow leaves it at offset 0 with no room.
            CHECK((uint32_t)f.bank.var(i).str_off() + Vars::text_slot(d).cap
                      <= f.bank.tables().pool_bytes);
            // A band is meaningless here and must read as unset, not as the
            // two pool halves reinterpreted as Scalars.
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
    // Bank::init() lays these out, so an overlap would be a bug in the layout
    // rather than in the table - but it is invisible either way until two
    // settings start overwriting each other, so it stays asserted.
    SiteBank f;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Variable& a = *f.bank.var(i).meta();
        if (a.type != Vars::T_STR) continue;
        const uint16_t ao = f.bank.var(i).str_off(), ac = Vars::text_slot(a).cap;
        for (uint16_t j = (uint16_t)(i + 1); j < f.bank.count(); j++) {
            const Vars::Variable& b = *f.bank.var(j).meta();
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
    /** @brief Does this row publish a Modbus TCP slave register? The rule lives
     *  in vars/access.h beside the other "what a row is decides where it
     *  appears" rules - restating it here would let the map serve a scope this
     *  test does not police. */
    bool mapped(const Vars::Variable& d) { return Vars::var_mapped(d); }
    /** @brief Whether @p d's register sits in the block its scope belongs to.
     *
     *  Site rows below 900, the library's above; alarms in 800..899 and, for the
     *  library's own, the top of its reserve. The number then says what kind of
     *  row it is, which is the whole reason for blocking them - see
     *  docs/design/modbus-tcp-server.md. */
    bool in_block(const Vars::Variable& d) {
        const uint16_t r = d.bind.reg;
        return (d.scope == Vars::SCOPE_A) ? ((r >= 800 && r <= 899) ||
                                             (r >= 990 && r <= 999))
                                          : ((r >= 1 && r <= 799) ||
                                             (r >= 900 && r <= 989));
    }
}

TEST_CASE("site: no two rows claim the same Modbus register") {
    // Parameters, status values and alarms share one register space on the TCP
    // slave, and their numbers are hand-assigned precisely so inserting a row
    // does not renumber the map an external master holds. Nothing derives them,
    // so nothing catches a duplicate but this.
    SiteBank f;
    uint16_t checked = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Variable& a = *f.bank.var(i).meta();
        if (!mapped(a)) continue;
        INFO("'" << a.name << "' has no register");
        CHECK(a.bind.reg != 0);
        checked++;
        for (uint16_t j = (uint16_t)(i + 1); j < f.bank.count(); j++) {
            const Vars::Variable& b = *f.bank.var(j).meta();
            if (!mapped(b)) continue;
            INFO("'" << a.name << "' and '" << b.name << "' both claim register "
                     << a.bind.reg);
            CHECK(a.bind.reg != b.bind.reg);
        }
    }
    CHECK(checked > 0);
}

TEST_CASE("site: every register sits in its scope's block") {
    // Uniqueness alone would let an alarm take 750 and read as a parameter to
    // every master downstream. The block a row's number falls in is part of the
    // published map, so it is asserted rather than merely intended.
    SiteBank f;
    uint16_t alarms = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Variable& d = *f.bank.var(i).meta();
        if (!mapped(d)) continue;
        INFO("'" << d.name << "' holds register " << d.bind.reg
                 << ", outside the block for its scope");
        CHECK(in_block(d));
        if (d.scope == Vars::SCOPE_A) alarms++;
    }
    // The platform alarms come in through BOTANIK_LIBRARY_VARS, so a table that
    // dropped the splice would otherwise pass this silently.
    CHECK(alarms >= 6);
}

TEST_CASE("site: every analog calibration resolves and is coherent") {
    // These blocks have no row pointing at them yet - the AI-1 / AO-1 channel
    // maps are still open - so this is what keeps them honest until one does.
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
        // Both ends of a span must be measured in the same unit, or the derived
        // scale is off by whatever the ratio between them is. Nothing else
        // checks this: mV and uA are both plausible-looking numbers.
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
        // A degenerate span would divide by zero; scale() floors it at 0, but
        // a site that declared one has made a mistake, not a choice.
        const Vars::AnalogValues v = Vars::analog_values(&f.bank, &c);
        INFO("block " << (int)b << " has a zero-width raw span");
        CHECK(v.raw_max != v.raw_min);
        INFO("block " << (int)b << " has an inverted plausibility band");
        CHECK(v.min <= v.max);
    }
}

TEST_CASE("analog: the calibration converts both ways") {
    // A tunable with no parameter is its constant, so a block of K()s needs no
    // bank at all - which is what makes the arithmetic testable on its own.
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
    // The band is wider than the calibrated span, which is the point of having
    // both: a reading past the top endpoint is still a measurement.
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
    // R() carries the value to use if the name is not in the table, so a typo
    // degrades to the declared number rather than to zero.
    CHECK(Vars::tunable(&f.bank, Vars::R("no_such_parameter", 42.0f))
              == doctest::Approx(42.0f));
    CHECK(Vars::tunable(&f.bank, Vars::K(7.0f)) == doctest::Approx(7.0f));
    // ...and the miss is counted, which is what the catch-all test asserts.
    CHECK(f.bank.unresolved_count() > 0);
}

}  // namespace site_names

// --------------------------------------------------------------------------
// site_bindings
// --------------------------------------------------------------------------

namespace site_bindings {

using namespace Botanik;
using namespace Botanik::Vars;

namespace {

const VarTables& tables() { return Site::g_site.vars; }

bool on_the_bus(const Variable& d) {
    return d.bind.transport == XP_MODBUS;
}

/** @brief Registers, or coils, that @p d occupies: [reg, reg + regs). */
uint32_t span_end(const Variable& d) {
    const uint16_t n = d.bind.regs != 0 ? d.bind.regs : 1;
    return (uint32_t)d.bind.reg + n;
}

/** @brief doctest stringifies a const char* as an address, so names go
 *  through std::string or a failure names nothing useful. */
std::string nm(const Variable& d) { return std::string(d.name); }

}  // namespace

TEST_CASE("site bindings: no two field-bus rows overlap on a device") {
    const VarTables& t = tables();

    for (uint16_t i = 0; i < t.count; ++i) {
        const Variable& a = t.var[i];
        if (!on_the_bus(a)) continue;
        for (uint16_t j = (uint16_t)(i + 1); j < t.count; ++j) {
            const Variable& b = t.var[j];
            if (!on_the_bus(b)) continue;
            // Same device, and the same address space: bit function codes and
            // register function codes are numbered apart.
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
    const VarTables& t = tables();

    for (uint16_t i = 0; i < t.count; ++i) {
        const Variable& d = t.var[i];
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
    // expire_stale() skips a row whose timeout_ms is 0, and the driver does not
    // invalidate on a failed read - so such a row would hold its last value for
    // ever after the device died, reading GOOD the whole time.
    const VarTables& t = tables();

    for (uint16_t i = 0; i < t.count; ++i) {
        const Variable& d = t.var[i];
        if (!on_the_bus(d) || d.role != ROLE_INPUT) continue;
        INFO("'" << nm(d) << "' is a field-bus input with no timeout");
        CHECK(d.timeout_ms > 0);
    }
}

TEST_CASE("site bindings: no read block can exceed a device's own request limit") {
    // The SDM630 refuses a request for more than 40 parameters (80 registers),
    // and answers with an exception rather than data. The planner's ceiling is
    // MODBUS_MAX_REGS_PER_BLOCK; this checks the site stays inside it, which is
    // what keeps a merged block legal at the far end.
    const VarTables& t = tables();

    for (uint16_t i = 0; i < t.count; ++i) {
        const Variable& d = t.var[i];
        if (!on_the_bus(d) || Modbus::is_bit(d.bind.fc)) continue;
        INFO("'" << nm(d) << "' spans " << d.bind.regs << " registers");
        CHECK(d.bind.regs <= Config::MODBUS_MAX_REGS_PER_BLOCK);
    }
}

}  // namespace site_bindings

// --------------------------------------------------------------------------
// sizes
// --------------------------------------------------------------------------

namespace sizes {

// Concrete instantiations of the templated pages, matching what the
// simulator actually builds (see botanik_sim/main.cpp).
using HomePage_t   = Botanik::Pages::HomePage;
// StatusPage is an unregistered building block; its template parameter is what
// a site subclass draws in its own data rows. Nothing at this site has one
// any more - variables are reached through the bank - so it is sized here on a
// stand-in, and the parameter is arguably vestigial now.
struct StatusPayloadStandIn { uint32_t whatever; };
using StatusPage_t = Botanik::Pages::StatusPage<StatusPayloadStandIn>;

// Guard a type against runaway growth. The budget is a loose ceiling - bump it
// deliberately if a structure legitimately needs it. The failure message names
// the type and both numbers, so a trip reads without a separate size table.
#define REPORT_SIZE(T, budget)                                               \
    CHECK_MESSAGE(sizeof(T) <= (size_t)(budget),                             \
                  #T " = " << sizeof(T) << " bytes, budget " << (budget))

// Budgets are loose round ceilings above the current host-build size — a
// regression tripwire for accidental bloat, not a tight spec. The page sizes are
// dominated by config-driven line/log buffers (scrollback + BufferedLogWriter),
// so they move with the Config::*_LINES / LOG_* knobs, not with code churn.
TEST_CASE("sizeof: terminal & input objects") {
    // AnsiTerminal owns the screen back-buffer: two GRID_ROWS x GRID_COLS cell
    // grids (front + back, ~8 KB) diffed by present(); its size tracks the grid
    // dimensions, not the VT device state (~100 B).
    REPORT_SIZE(Botanik::Terminal::AnsiTerminal, 12288);
    // The editor borrows its text buffer and row arrays, so it is just cursor
    // and layout state; the bytes live in whatever EditorBuffers fed it.
    REPORT_SIZE(Botanik::Terminal::TextEditor,    256);
    REPORT_SIZE(Botanik::Terminal::InputParser,   512);
}

// The editors' actual footprint, one instantiation per job. These are the sizes
// that move when SHELL_INPUT_LEN / EDITOR_MAX_ROWS / NOTES_* change.
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
    REPORT_SIZE(Botanik::Logs::BufferedLogWriter,   16384);
    REPORT_SIZE(Botanik::Logs::SerialLogWriter,     16384);
    REPORT_SIZE(Botanik::Utils::BufferedSerial,      8192);
}

TEST_CASE("sizeof: the site's variable table and value store") {
    // The site payload is gone entirely: parameters, status, actuators and
    // alarms are all rows of one table now. The descriptors are flash, the
    // values are the only RAM cost.
    REPORT_SIZE(Botanik::Vars::Variable, 128);
    REPORT_SIZE(Botanik::Vars::Value, 32);
}

}  // namespace sizes
