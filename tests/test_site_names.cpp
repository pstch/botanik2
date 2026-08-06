/**
 * @file test_site_names.cpp
 * @brief The selected site's tables name their variables; this checks the names
 *        are real.
 *
 * Nothing addresses a variable by index any more. The status groups, the home
 * fields, the alarm conditions and the control programs all spell a name, and
 * Vars::Bank resolves it once at bring-up. That removed a 249-line enum whose
 * order had to be kept in step with the table by hand - and with it, the whole
 * class of bug where inserting a row silently re-points every reference after
 * it.
 *
 * What it costs is that a typo no longer fails to compile: an unknown name
 * resolves to the inert sink and reads as a dead variable forever. That is what
 * this suite exists to catch, in the gate rather than on a running machine.
 * Bank counts every miss, so the check is not "did I remember to test this row"
 * but "did anything, anywhere, fail to resolve".
 *
 * @see @ref cpptests
 */
#include <cstdint>                    // uint16_t - the table indices walked
#include <cstring>                    // strcmp - matching a resolved name back to its row

#include "libbotanik_site.h"          // resolves BOTANIK_SITE -> BOTANIK_SITE_HEADER
#include "libbotanik.h"               // Vars::Bank::find / unresolved_count - what this test asserts on

#include "doctest.h"                  // the test framework (vendored single header); last so it sees our types
#include BOTANIK_SITE_HEADER          // the selected site's tables - the names under test

#if BOTANIK_FEATURE_SUN
#  include "libbotanik/sun/sun_tracker.h"
#endif

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

    // Control has to be on or every ROLE_OUTPUT write is refused before it ever
    // names anything.
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

    // Nothing the site RUNS may write a parameter. Bank::set() is the only way
    // one legitimately moves, and that is the operator's path (the shell, the
    // PARAMS page) - never a control program or the simulated field. This is
    // the check that catches a write aimed at a row nobody meant: an index that
    // is stale or simply zero lands somewhere real, and with the parameters
    // leading the table, zero is the first config row rather than a harmless
    // one. Names cannot drift, but a leftover index still can.
    for (uint16_t k = 0; k < f.bank.param_count(); k++) {
        const Vars::Var h = f.bank.param(k);
        if (h.meta()->type == Vars::T_STR) continue;   // lives in the pool
        INFO("parameter '" << h.meta()->name << "' was written by a control "
             "program or the simulated field");
        CHECK(h.raw().u == h.meta()->def.u);
    }
}

#if BOTANIK_FEATURE_SUN
TEST_CASE("site: the standalone components resolve their config names too") {
    // The catch-all above runs what the SiteDescriptor bundles. A standalone
    // component is not in it - BotanikFirmware never instantiates the sun
    // tracker, a site owns the instance - so the names it looks up are reached
    // by no table walk and no task run. They are string literals in
    // sun/sun_tracker.cpp, and a miss reads 0 rather than failing.
    //
    // It had drifted exactly that way: the tracker reads geo_lat_deg/geo_lon_deg
    // and the test fixture declared geo_latitude/geo_longitude, so both sides
    // resolved to the sink and the site sat on the equator at 0 N 0 E, with a
    // 12-hour day all year. Nothing failed, because nothing looked.
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
    /** @brief Does this row publish a Modbus TCP slave register? Parameters,
     *  status values and alarms do, and they share one space. */
    bool mapped(const Vars::Variable& d) {
        return d.scope == Vars::SCOPE_P || d.scope == Vars::SCOPE_SV ||
               d.scope == Vars::SCOPE_A;
    }
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
