/**
 * @file test_site.cpp
 * @brief The selected site's table integrity: every name a program reaches for
 *        resolves, and no two rows collide on a register or on pool bytes.
 *
 * A name matching no row resolves to the inert sink, which reads 0 forever, so
 * these are the cases that stop a typo becoming a silently dead input.
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
#include "ram_store.h"
#include "site_bank.h"

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

}  // namespace

namespace {

/**
 * @brief Every control program the site runs, constructed together.
 *
 * Every program is ST now, so the resource IS the list: `Plant::wire()` binds
 * each one and records its health, and `programs.csv` is what cannot fall
 * behind - the same table matiec and plant.cpp are generated from.
 *
 * @note The tracker is never registered here, so it stays unlocated and
 * day_phase holds its phase - the boot path, and the one worth walking.
 */
}  // namespace

TEST_CASE("site: running the whole site leaves every parameter where it was") {
    // RUNS the site - each program once, plus simulate() - which is the one
    // thing no static check covers: a law or the simulated field writing a row
    // it does not own.
    //
    // What this case used to also do was resolve every name. Nothing resolves
    // now: a row is a generated index, and a name no table declares does not
    // compile.
    SiteBank f;

    Logs::Logger logger;
    Vars::BankAlarms rows;
    rows.bind(&f.bank);
    Alarms::AlarmController m(Botanik::SetupContext(), &rows);
    m.setup(0);   // the scan is on_setup(): the rows exist now
    (void)m;

    // Var::set() isn't gated (authority is enforced where a writer is
    // DISPATCHED); this resolves names against a running bank, not a stopped one.
#ifdef TARGET_SIMULATOR
    Site::simulate(f.bank, 1000);
#endif
    // Every program is ST, so the RESOURCE is what binds them: wire() resolves
    // each one's VAR_EXTERNALs and records whether it got them all.
    Botanik::Tasks::Registry prog_reg;
    Plc::Runtime::Plant st(Botanik::SetupContext(prog_reg), f.bank);
    st.wire();
    st.scan(1000);          // one activation of every law

    // Catches a write at a stale index - zero is a real row.
    for (uint16_t k = 0; k < f.bank.param_count(); k++) {
        const Vars::Var h = f.bank.param_at(k);
        if (h.meta().type == Vars::T_STR) continue;   // lives in the pool
        INFO("parameter '" << h.meta().name << "' was written by a control "
             "program or the simulated field");
        CHECK(h.value().v.u == h.meta().def.u);
    }
}

#if BOTANIK_FEATURE_SUN_TRACKER
TEST_CASE("site: every local row gets a terminal") {
    // Level inputs and pulse counters come off the SAME eight pins, so a table
    // can declare more than exist. Such a row reads its declared value for the
    // life of the run, which on a contact is indistinguishable from open.
    Botanik::Tasks::Registry reg;
    Vars::Bank bank;
    TestSite::build(bank);

    Io::LocalInputs local(Botanik::SetupContext(reg), bank);
    local.register_tasks();
    reg.setup(0);

    CHECK(local.refused() == 0);
    CHECK(local.count() <= Botanik::HAL::DigitalIn::CHANNELS);
    CHECK(local.pulse_count() <= Botanik::HAL::Pulse::MAX_CHANNELS);
}

#endif
TEST_CASE_FIXTURE(SiteBank, "vars: a row's quality IS its companion's value") {
    // One store, two names. A driver writes the quality through the row it
    // published; ST reads the companion. Nothing copies between them, so they
    // cannot lag or disagree.
    const uint16_t probe = bank.find("cli_temp_hi1");
    const uint16_t qlt   = bank.find("cli_temp_hi1_qlt");
    REQUIRE(probe != Vars::VAR_NONE);
    REQUIRE(qlt != Vars::VAR_NONE);
    CHECK(bank.var(probe).quality_row() == qlt);

    bank.var(probe).publish(Vars::from_f32(Vars::T_F32, 21.5f), 1000);
    CHECK(bank.var(probe).valid());
    CHECK(bank.var(qlt).as<uint8_t>() == Vars::Q_GOOD);

    INFO("and the other way: writing the companion is what makes the row bad");
    bank.var(qlt).set_i(Vars::Q_NONE);
    CHECK_FALSE(bank.var(probe).valid());
    CHECK(bank.var(probe).quality() == Vars::Q_NONE);
}

TEST_CASE("vars: which qualities carry a number, and which read the declared one") {
    // The one rule Ref::get() branches on, and what MQTT publishes. Pinned
    // here because adding a code is where it would silently be got wrong: a
    // new code holds nothing until somebody says it does.
    CHECK(Vars::usable(Vars::Q_GOOD));
    CHECK(Vars::usable(Vars::Q_HELD));
    CHECK_FALSE(Vars::usable(Vars::Q_NONE));

    INFO("...and usable is NOT measured: a fallback is worth computing on and "
         "still means the plant cannot be seen");
    CHECK(Vars::measured(Vars::Q_GOOD));
    CHECK_FALSE(Vars::measured(Vars::Q_HELD));
    CHECK_FALSE(Vars::measured(Vars::Q_NONE));

    INFO("every code renders, and nothing past them does - that null is the "
         "range check a `_qlt` row is written through");
    CHECK(Vars::quality_label(Vars::Q_NONE) != nullptr);
    CHECK(Vars::quality_label(Vars::Q_GOOD) != nullptr);
    CHECK(Vars::quality_label(Vars::Q_HELD) != nullptr);
    CHECK(Vars::quality_label(3) == nullptr);
}

TEST_CASE_FIXTURE(SiteBank, "vars: a row with no companion is valid, always") {
    // The rule that keeps the table small: a parameter cannot stop being one,
    // and a companion has no quality of its own - which is what stops the
    // regress.
    const uint16_t param = bank.find("cli_coldBankOfs_degC");
    REQUIRE(param != Vars::VAR_NONE);
    CHECK(bank.var(param).quality_row() == Vars::VAR_NONE);
    CHECK(bank.var(param).valid());

    const uint16_t qlt = bank.find("cli_temp_hi1_qlt");
    REQUIRE(qlt != Vars::VAR_NONE);
    CHECK(bank.var(qlt).quality_row() == Vars::VAR_NONE);
    bank.var(qlt).set_i(Vars::Q_NONE);
    CHECK(bank.var(qlt).valid());
}

TEST_CASE_FIXTURE(SiteBank, "vars: a forced value is believed, a forced quality wins") {
    // Forcing a dead probe's reading so the plant runs on it is what an
    // override is FOR, so the force does not inherit the doubt underneath it.
    const uint16_t probe = bank.find("cli_temp_hi1");
    bank.var(probe).quality_var().set_i(Vars::Q_NONE);
    REQUIRE_FALSE(bank.var(probe).valid());

    bank.var(probe).override_set(Vars::from_f32(Vars::T_F32, 25.0f));
    CHECK(bank.var(probe).valid());
    CHECK(bank.var(probe).as_f32() == doctest::Approx(25.0f));

    INFO("...and forcing the QUALITY on top of it is the operator saying no");
    bank.var(probe).quality_var().override_set(Vars::Scalar::U(Vars::Q_NONE));
    CHECK_FALSE(bank.var(probe).valid());
    CHECK(bank.var(probe).as_f32() == doctest::Approx(25.0f));
}

TEST_CASE_FIXTURE(SiteBank, "vars: forcing a companion is how a row reads dead") {
    // The override layer carries no validity of its own any more: "make this
    // sensor read failed" is an ordinary force of an ordinary row.
    const uint16_t probe = bank.find("cli_temp_hi1");
    bank.var(probe).publish(Vars::from_f32(Vars::T_F32, 21.5f), 1000);
    REQUIRE(bank.var(probe).valid());

    bank.var(probe).quality_var().override_set(Vars::Scalar::U(Vars::Q_NONE));
    CHECK_FALSE(bank.var(probe).valid());
    INFO("the reading itself is untouched - it is the belief that was forced");
    CHECK(bank.var(probe).as_f32() == doctest::Approx(21.5f));
}

#if BOTANIK_FEATURE_PLC
TEST_CASE("site: a program that opted into health publishes it, off its own name") {
    // The row is derived from the roster's name, so a renamed program would
    // quietly stop publishing and the row it used to drive would sit at its
    // declared FAULTED for ever. The generator refuses a law that owes a health
    // and does not write it; what is left for here is that the two agree at
    // RUN time about which row that is.
    Vars::Bank bank;
    Botanik::Tasks::Registry reg;
    TestSite::build(bank);
    Plc::Runtime::Plant st(Botanik::SetupContext(reg), bank);
    st.wire();
    st.register_tasks();
    reg.setup(0);

    for (uint8_t i = 0; i < Plc::Runtime::PROGRAM_COUNT; i++) {
        const Plc::Runtime::Program& p = Plc::Runtime::programs[i];
        if (p.health_row == Vars::VAR_NONE) continue;
        char row[48];
        snprintf(row, sizeof row, "prog_%s_health", p.name);
        INFO("'" << p.name << "' should write '" << row << "'");
        const uint16_t declared = bank.find(row, (uint16_t)strlen(row));
        REQUIRE(declared == p.health_row);
        INFO("...and having run, it says something the vocabulary carries");
        CHECK(Plc::health_label(bank.var(declared).value().as<uint64_t>())
              != nullptr);
    }
}
#endif

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
TEST_CASE("site: every mapped row fits the TCP map") {
    // Outgrowing it serves a SHORT map - the rows past the end answer nothing,
    // which an integrator reads as a device that does not have them. The slave
    // says so at bring-up, but a boot warning is not a thing anyone reads on
    // the way past, and the table grows a row at a time.
    SiteBank f;
    Modbus::Map map(&f.bank);
    map.build();

    INFO("mapped " << map.count() << " of " << Config::MODBUS_TCP_MAX_ROWS
                   << ", " << map.overflow() << " did not fit");
    CHECK(map.overflow() == 0);
    CHECK(map.count() > 0);

    // Counted here rather than left implicit: the cap is a site override, and
    // the number it has to clear is this one. A hostname is the one mapped row
    // the map does not serve - sixteen bits cannot hold one - so it is not in
    // the count either.
    uint16_t mapped = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++) {
        const Vars::Descriptor& d = f.bank.var(i).meta();
        if (Vars::mapped(d) && Modbus::Map::published(d)) mapped++;
    }
    CHECK(map.count() == mapped);
}
#endif
TEST_CASE("library: a site row REDECLARES a library one, and the site's wins") {
    // The library declares geo_lat_deg (SUN_TRACKER needs a location) and the
    // site redeclares it with this greenhouse's own. The GENERATOR applies the
    // override, in place: the whole row is the site's - the band and the unit
    // included, not just the value - and it keeps the library's register.
    SiteBank f;
    const uint16_t r = f.bank.find("geo_lat_deg");
    REQUIRE(r != Vars::VAR_NONE);

    INFO("one row, not two: the site's replaced the library's in place");
    uint16_t seen = 0;
    for (uint16_t i = 0; i < f.bank.count(); i++)
        if (strcmp(f.bank.descriptor(i).name, "geo_lat_deg") == 0) seen++;
    CHECK(seen == 1);

    INFO("and it is the SITE's row: the library declares no band and no unit");
    const Vars::Descriptor& d = f.bank.descriptor(r);
    CHECK(f.bank.var(r).as_f32() == doctest::Approx(47.96f));
    CHECK(strcmp(d.unit, "deg") == 0);
    INFO("...keeping the register the library assigned it");
    CHECK(d.bind.reg == 941);
}

TEST_CASE("site: the two DC rails share one calibration") {
    // Both rails hang off the same divider design, so a recalibration is one
    // edit and not two that can drift apart. Sharing the BLOCK is what shares
    // the parameters: the six tunables name rows, and the rows are the same.
    SiteBank f;
    const uint16_t dc1 = f.bank.find("pwr_dc1_V");
    const uint16_t dc2 = f.bank.find("pwr_dc2_V");
    REQUIRE(dc1 != Vars::VAR_NONE);
    REQUIRE(dc2 != Vars::VAR_NONE);

    const Vars::AnalogCal* c1 = Vars::analog_cal(f.bank.descriptor(dc1));
    const Vars::AnalogCal* c2 = Vars::analog_cal(f.bank.descriptor(dc2));
    REQUIRE(c1 != nullptr);
    CHECK(c1 == c2);

    INFO("and every tunable is a row, so the sharing is of something editable");
    for (uint8_t i = 0; i < 6; i++) {
        INFO("tunable " << (int)i);
        CHECK(c1->row[i] != Vars::VAR_NONE);
    }

    // One edit, both readings. Moving the engineering top end changes what a
    // given count means, and it has to move BOTH rails or the merge bought
    // nothing.
    const uint16_t top = f.bank.find("pwr_dc_engMax_V");
    REQUIRE(top != Vars::VAR_NONE);
    f.bank.var(dc1).publish_wire(Vars::Scalar::U(5000), 1000);
    f.bank.var(dc2).publish_wire(Vars::Scalar::U(5000), 1000);
    const float was1 = f.bank.var(dc1).as_f32();
    const float was2 = f.bank.var(dc2).as_f32();

    REQUIRE(f.bank.var(top).param_set(40.0f) == Vars::SET_OK);
    f.bank.var(dc1).publish_wire(Vars::Scalar::U(5000), 2000);
    f.bank.var(dc2).publish_wire(Vars::Scalar::U(5000), 2000);
    CHECK(f.bank.var(dc1).as_f32() != doctest::Approx(was1));
    CHECK(f.bank.var(dc2).as_f32() != doctest::Approx(was2));
    INFO("the same count on either rail reads the same volts");
    CHECK(f.bank.var(dc1).as_f32() == doctest::Approx(f.bank.var(dc2).as_f32()));
}

TEST_CASE("site: a pair that must stay ordered cannot be edited out of order") {
    // Each of these is legal on its own and only the PAIR is wrong, which a
    // fixed band cannot see: a target floor above its own ceiling holds the
    // house at the floor, and a ramp starting past where it ends inverts.
    SiteBank f;
    // Every figure here sits INSIDE the pair's own fixed rail, so a refusal can
    // only be the cross-reference. Against the rail, `too_high` would be
    // refused whether the rows knew about each other or not.
    struct Pair {
        const char* lo; const char* hi;
        float below, above;       // a settled pair, with room either side
        float too_high, too_low;  // past the OTHER row, but well inside the rail
    };
    static const Pair pairs[] = {
        { "cli_targetMin_degC", "cli_targetMax_degC",  // rail 0 .. 40
          10.0f, 30.0f, 35.0f, 5.0f },
        { "cli_intakeStartDem", "cli_intakeFullDem",   // rail 0 .. 1
          0.20f, 0.60f, 0.80f, 0.10f },
        { "cli_windowStartDem", "cli_windowFullDem",   // rail 0 .. 1
          0.20f, 0.60f, 0.80f, 0.10f },
    };
    for (const Pair& p : pairs) {
        Vars::Var lo = f.bank.var(f.bank.find(p.lo));
        Vars::Var hi = f.bank.var(f.bank.find(p.hi));
        REQUIRE_FALSE(lo.null());
        REQUIRE_FALSE(hi.null());
        INFO(doctest::String(p.lo) << " against " << doctest::String(p.hi));

        REQUIRE(lo.param_set(p.below) == Vars::SET_OK);
        REQUIRE(hi.param_set(p.above) == Vars::SET_OK);

        INFO("the floor may not be raised past the ceiling");
        CHECK(lo.param_set(p.too_high) == Vars::SET_INVALID);
        INFO("...nor the ceiling dropped under the floor");
        CHECK(hi.param_set(p.too_low) == Vars::SET_INVALID);

        // Both refused values are legal once the OTHER row has made room, which
        // is what keeps the constraint from locking an operator out: widen by
        // moving the ceiling first, narrow by moving the floor first.
        REQUIRE(hi.param_set(p.too_high) == Vars::SET_OK);
        CHECK(lo.param_set(p.too_high) == Vars::SET_OK);
        REQUIRE(lo.param_set(p.too_low) == Vars::SET_OK);
        CHECK(hi.param_set(p.too_low) == Vars::SET_OK);
    }
}

}  // namespace site_names

namespace site_params {

using namespace Botanik;

TEST_CASE("params: restating a value owes the store nothing") {
    Vars::Bank bank;
    TestSite::build(bank);
    test_store::RamStore store;
    Vars::Persist persist(Botanik::SetupContext(), bank, &store);

    const uint16_t r = bank.find("irr_soak_sec");
    REQUIRE(r != Vars::VAR_NONE);
    REQUIRE(bank.var(r).param_set("5") == Vars::SET_OK);
    CHECK(bank.var(r).param_unsaved());
    REQUIRE(persist.save_all() == 1);
    const uint16_t after_first = store.writes;
    CHECK(after_first == 1);

    INFO("the same value again is not a write: a Modbus master re-asserting a "
         "setpoint block every poll would otherwise erase flash every poll");
    REQUIRE(bank.var(r).param_set("5") == Vars::SET_OK);
    CHECK_FALSE(bank.var(r).param_unsaved());
    CHECK(persist.save_all() == 0);
    CHECK(store.writes == after_first);

    INFO("...and a value that really moved still is");
    REQUIRE(bank.var(r).param_set("6") == Vars::SET_OK);
    CHECK(bank.var(r).param_unsaved());
    CHECK(persist.save_all() == 1);
    CHECK(store.writes == (uint16_t)(after_first + 1));
}

TEST_CASE("retained: the cadence writes what moved, not what is retained") {
    Vars::Bank bank;
    TestSite::build(bank);
    test_store::RamStore store;
    Vars::Persist persist(Botanik::SetupContext(), bank, &store);
    Vars::runtime_bank = &bank;

    const uint16_t total = bank.find("irr_inTotal_L");
    REQUIRE(total != Vars::VAR_NONE);
    REQUIRE(Vars::retained(bank.var(total).meta()));

    INFO("nothing has moved, so the cadence owes the store nothing: without "
         "this it is an erase per retained row per sys_retain_sec, for ever");
    CHECK(persist.save_retained() == 0);
    CHECK(store.writes == 0);

    const Vars::Ref<float> meter = { total };
    meter.drive(202.0f);
    CHECK(bank.var(total).retain_owed());
    CHECK(persist.save_retained() == 1);
    CHECK(store.writes == 1);
    CHECK_FALSE(bank.var(total).retain_owed());
    CHECK(store.stored("irr_inTotal_L", "ret."));

    INFO("a law restates its counter every scan, and restating is not moving");
    for (int i = 0; i < 20; i++) meter.drive(202.0f);
    CHECK(persist.save_retained() == 0);
    CHECK(store.writes == 1);

    INFO("...and a litre that really arrived still reaches flash");
    meter.drive(203.0f);
    CHECK(persist.save_retained() == 1);
    CHECK(store.writes == 2);
}

TEST_CASE("params: a program's write waits on the cadence, an operator's on the operator") {
    Vars::Bank bank;
    TestSite::build(bank);
    test_store::RamStore store;
    Vars::Persist persist(Botanik::SetupContext(), bank, &store);

    // The program's: a watering date, through the parameter path a law uses.
    const uint16_t date = bank.find("irr_last_tray1");
    REQUIRE(date != Vars::VAR_NONE);
    // What the generated __SET_EXTERNAL does on a PARAMETER row: the write is
    // an edit, owed to the retain cadence rather than to the operator.
    Vars::runtime_bank = &bank;
    const Vars::Ref<uint32_t> stamp = { bank.find("irr_last_tray1") };
    stamp.drive(20000u);
    INFO("OWED, not unsaved: the params page counts what a PERSON typed, and a "
         "watering date is not that");
    CHECK(bank.var(date).param_owed());
    CHECK_FALSE(bank.var(date).param_unsaved());
    CHECK(bank.owed_count() == 1);
    CHECK(bank.unsaved_count() == 0);

    INFO("and it did not reach flash on the way past: an erase inside a scan "
         "is a stall, and repeated it is a dead part");
    CHECK(store.writes == 0);

    // The operator's: an ordinary setting, typed.
    const uint16_t setting = bank.find("irr_soak_sec");
    REQUIRE(setting != Vars::VAR_NONE);
    REQUIRE(bank.var(setting).param_set("5") == Vars::SET_OK);
    CHECK(bank.var(setting).param_unsaved());

    INFO("the cadence takes the program's and leaves the operator's: a setting "
         "that breaks its own console has to stay recoverable by rebooting");
    CHECK(persist.save_owed() == 1);
    CHECK(store.writes == 1);
    CHECK_FALSE(bank.var(date).param_owed());
    CHECK(bank.var(setting).param_unsaved());

    INFO("...and `params save` is the operator saying so, which takes both");
    CHECK(persist.save_all() == 1);
    CHECK(store.writes == 2);
    CHECK_FALSE(bank.var(setting).param_unsaved());
}

}  // namespace site_params

namespace site_capacity {

using namespace Botanik;

TEST_CASE("local io: a DO or RO row on device 0 reaches a pin") {
    // There was no path at all until Io::LocalOutputs: the HAL had DigitalIn
    // and Pulse and nothing for outputs, and LocalInputs is inputs-only. So a
    // local relay row was declarable, generated, checked - and never written.
    REQUIRE(HAL::DigitalOut::attach(HAL::Pin::RELAY_1));
    CHECK_FALSE(HAL::DigitalOut::read(HAL::Pin::RELAY_1));

    HAL::DigitalOut::write(HAL::Pin::RELAY_1, true);
    CHECK(HAL::DigitalOut::read(HAL::Pin::RELAY_1));

    INFO("a pin that is not an output is refused rather than driven");
    CHECK_FALSE(HAL::DigitalOut::attach(HAL::Pin::IN1));
    CHECK_FALSE(HAL::DigitalOut::attach(HAL::Pin::BUTTON));
    INFO("...and an input pin is not one of DigitalIn's either way round");
    CHECK_FALSE(HAL::DigitalIn::attach(HAL::Pin::RELAY_1));

    INFO("a terminal is a LEVEL or a VOLTAGE, never both: whichever row claimed"
         " it configured the pin, and the other is refused rather than"
         " reconfiguring it under whichever attached last");
    REQUIRE(HAL::AnalogIn::attach(HAL::Pin::IN3));
    HAL::AnalogIn::inject(HAL::Pin::IN3, 2048);
    CHECK(HAL::AnalogIn::read(HAL::Pin::IN3) == 2048);
    CHECK_FALSE(HAL::AnalogIn::owns(HAL::Pin::BUTTON));
    CHECK_FALSE(HAL::AnalogIn::owns(HAL::Pin::RELAY_1));
    INFO("...and a count is clamped to the converter's full scale");
    HAL::AnalogIn::inject(HAL::Pin::IN3, 60000);
    CHECK(HAL::AnalogIn::read(HAL::Pin::IN3) == HAL::AnalogIn::FULL_SCALE);

    INFO("the button IS an input, though it is not one of the eight terminals");
    CHECK(HAL::DigitalIn::attach(HAL::Pin::BUTTON));
    HAL::DigitalIn::inject(HAL::Pin::BUTTON, true);
    CHECK(HAL::DigitalIn::read(HAL::Pin::BUTTON));
}

TEST_CASE("local io: every pin label is claimable by exactly one direction") {
    // The labels are one flat space, so a typo lands on a real pin of the
    // wrong kind rather than out of range. owns() is what keeps that a
    // refusal instead of a silent mis-drive.
    for (uint16_t p = 0; p < HAL::Pin::COUNT; p++) {
        const bool both = HAL::DigitalIn::owns(p) && HAL::DigitalOut::owns(p);
        INFO("pin ", p);
        CHECK_FALSE(both);
    }
    CHECK(HAL::DigitalIn::owns(HAL::Pin::IN8));
    CHECK(HAL::DigitalOut::owns(HAL::Pin::LED_4));
    CHECK(HAL::DigitalOut::owns(HAL::Pin::LED_BLUE));
}


}  // namespace site_capacity
