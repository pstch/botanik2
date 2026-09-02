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

    const Botanik::SiteDescriptor& site() { return Site::g_site; }

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
#ifdef TARGET_SIMULATOR
    Site::simulate(f.bank, 1000);
#endif
    // Every program is ST, so the RESOURCE is what binds them: wire() resolves
    // each one's VAR_EXTERNALs and records whether it got them all.
    Botanik::Tasks::Registry prog_reg;
    Plc::Runtime::Plant st(Botanik::SetupContext(prog_reg), f.bank);
    st.wire();
    st.scan(1000);          // one activation of every law

    INFO("a non-zero count means a table or a control program names a variable "
         "that is not in vars.h; the bank logs which one");
    CHECK(f.bank.unresolved_count() == 0);

    INFO("and every one of them bound: a program that missed a name is INVALID "
         "at bring-up, which is what says so on hardware where no test runs");
    for (uint8_t i = 0; i < Plc::Runtime::PROGRAM_COUNT; i++) {
        INFO("program ", Plc::Runtime::programs[i].name);
        CHECK(st.health(i) != Plc::Health::INVALID);
    }

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

    Plc::Runtime::Plant st(Botanik::SetupContext(reg), bank);
    st.wire();
    st.register_tasks();
    reg.setup(0);

    INFO("a name that is absent, or a row bound at the wrong type, counts here");
    CHECK(bank.unresolved_count() == before);

    INFO("and every registration fitted: MAX_TASKS is a HARD limit, and a "
         "program past it is refused rather than dropped quietly");
    CHECK(reg.overflow() == 0);
}

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

TEST_CASE_FIXTURE(SiteBank, "vars: a field row's quality reaches its companion") {
    // A C++ law asks the bank whether a reading is believable. ST has no way
    // to ask anything, so for a FIELD row the answer has to BE a row, and the
    // bank is what puts it there - never a program, which has nothing to copy.
    Botanik::Tasks::Registry reg;
    Vars::QualityMirror m(Botanik::SetupContext(reg), bank);
    m.wire();
    REQUIRE(m.pairs() > 0);
    REQUIRE(m.refused() == 0);

    const Vars::VarRef probe = bank.find("cli_temp_hi1");
    const Vars::VarRef qlt   = bank.find("cli_temp_hi1_qlt");
    REQUIRE(probe.index != Vars::VAR_NONE);
    REQUIRE(qlt.index != Vars::VAR_NONE);

    bank.var(probe).publish(Vars::from_f32(Vars::T_F32, 21.5f), 1000);
    m.scan();
    CHECK(bank.var(qlt).as_bool());

    INFO("and it follows the row down: a probe that stops answering is what "
         "the companion is for");
    bank.var(probe).invalidate(Vars::Q_NO_DATA);
    m.scan();
    CHECK_FALSE(bank.var(qlt).as_bool());
}

TEST_CASE_FIXTURE(SiteBank, "vars: a DERIVED row's companion is not mirrored") {
    // cli_temp_in_qlt is temp_rh_zone's verdict about a number it computed.
    // Nothing measured it, so there is no quality to copy, and mirroring it
    // would overwrite the law that owns it.
    Botanik::Tasks::Registry reg;
    Vars::QualityMirror m(Botanik::SetupContext(reg), bank);
    m.wire();

    const Vars::VarRef derived = bank.find("cli_temp_in_qlt");
    REQUIRE(derived.index != Vars::VAR_NONE);
    bank.var(derived).set(true);
    bank.var(bank.find("cli_temp_in")).invalidate(Vars::Q_NO_DATA);
    m.scan();
    INFO("the law's verdict stands: the mirror never touched it");
    CHECK(bank.var(derived).as_bool());
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

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
#if BOTANIK_FEATURE_PLC
TEST_CASE("site: every program publishes its health, off its own name") {
    // The base derives "prog_<name>" and stays SILENT when the site declares
    // none - a harness rig has no such row. Silent is the right behaviour and
    // the wrong thing to leave untested: a renamed program would quietly stop
    // publishing, and the row it used to drive would sit at its default
    // reading HEALTHY for ever.
    Vars::Bank bank;
    Botanik::Tasks::Registry reg;
    TestSite::build(bank);
    Plc::Runtime::Plant st(Botanik::SetupContext(reg), bank);
    st.wire();
    st.register_tasks();
    reg.setup(0);

    for (uint8_t i = 0; i < Plc::Runtime::PROGRAM_COUNT; i++) {
        const char* name = Plc::Runtime::programs[i].name;
        char row[40];
        snprintf(row, sizeof row, "prog_%s", name);
        INFO("'" << name << "' should drive '" << row << "'");
        const Vars::VarRef declared = bank.find(row, (uint16_t)strlen(row));
        REQUIRE(declared.index != Vars::VAR_NONE);   // the site declares one
        INFO("...and it reads its health rather than the INHIBITED it declares");
        CHECK(bank.var(declared).value().as<uint32_t>()
              == (uint32_t)Plc::Health::HEALTHY);
    }
}
#endif

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
        const Vars::Descriptor& d = *f.bank.var(i).meta();
        if (Vars::var_mapped(d) && Modbus::Map::published(d)) mapped++;
    }
    CHECK(map.count() == mapped);
}
#endif

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

namespace site_params {

using namespace Botanik;

/** @brief A store that counts writes and nothing else. What a wear question
 *  needs: the interesting number is how many times flash was asked, not what
 *  landed in it. */
struct CountingStore final : public Vars::VarStore {
    uint16_t writes = 0;
    bool load(const Vars::Descriptor&, Vars::Slot&, char*, uint16_t) override { return false; }
    bool save(const Vars::Descriptor&, const Vars::Slot&, const char*, uint16_t) override {
        writes++; return true;
    }
    bool load_override(const Vars::Descriptor&, Vars::Slot&) override { return false; }
    bool save_override(const Vars::Descriptor&, const Vars::Slot&) override { return true; }
    bool clear_override(const Vars::Descriptor&) override { return true; }
    bool clear(const Vars::Descriptor&) override { return true; }
    bool load_retained(const Vars::Descriptor&, Vars::Slot&) override { return false; }
    bool save_retained(const Vars::Descriptor&, const Vars::Slot&) override { return true; }
};

TEST_CASE("params: restating a value owes the store nothing") {
    Vars::Bank bank;
    TestSite::build(bank);
    CountingStore store;
    bank.set_store(&store);

    const Vars::VarRef r = bank.find("irr_soak_sec");
    REQUIRE(r.index != Vars::VAR_NONE);
    REQUIRE(bank.param_set(bank.var(r), "5") == Vars::SET_OK);
    CHECK(bank.var(r).param_unsaved());
    REQUIRE(bank.save_params() == 1);
    const uint16_t after_first = store.writes;
    CHECK(after_first == 1);

    INFO("the same value again is not a write: a Modbus master re-asserting a "
         "setpoint block every poll would otherwise erase flash every poll");
    REQUIRE(bank.param_set(bank.var(r), "5") == Vars::SET_OK);
    CHECK_FALSE(bank.var(r).param_unsaved());
    CHECK(bank.save_params() == 0);
    CHECK(store.writes == after_first);

    INFO("...and a value that really moved still is");
    REQUIRE(bank.param_set(bank.var(r), "6") == Vars::SET_OK);
    CHECK(bank.var(r).param_unsaved());
    CHECK(bank.save_params() == 1);
    CHECK(store.writes == (uint16_t)(after_first + 1));
}

TEST_CASE("params: a program's write waits on the cadence, an operator's on the operator") {
    Vars::Bank bank;
    TestSite::build(bank);
    CountingStore store;
    bank.set_store(&store);

    // The program's: a watering date, through the parameter path a law uses.
    const Vars::VarRef date = bank.find("irr_last_tray1");
    REQUIRE(date.index != Vars::VAR_NONE);
    Vars::Ref<uint32_t> stamp = bank.resolve<uint32_t>("irr_last_tray1");
    REQUIRE(stamp.edit(20000u));
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
    const Vars::VarRef setting = bank.find("irr_soak_sec");
    REQUIRE(setting.index != Vars::VAR_NONE);
    REQUIRE(bank.param_set(bank.var(setting), "5") == Vars::SET_OK);
    CHECK(bank.var(setting).param_unsaved());

    INFO("the cadence takes the program's and leaves the operator's: a setting "
         "that breaks its own console has to stay recoverable by rebooting");
    CHECK(bank.save_owed_params() == 1);
    CHECK(store.writes == 1);
    CHECK_FALSE(bank.var(date).param_owed());
    CHECK(bank.var(setting).param_unsaved());

    INFO("...and `params save` is the operator saying so, which takes both");
    CHECK(bank.save_params() == 1);
    CHECK(store.writes == 2);
    CHECK_FALSE(bank.var(setting).param_unsaved());
}

}  // namespace site_params

namespace site_capacity {

using namespace Botanik;

TEST_CASE("site: every row the site declares actually fits in the bank") {
    // LIBRARY_ROWS is hand-kept and cannot be taken from the library, so the
    // static_assert in vars.h guards a number that can itself be wrong. This
    // is the check that cannot be: the bank counts what it had no room for,
    // and a dropped row is a name that resolves to the inert sink for the life
    // of the run. It went 66 against 70 once, and 573 "fitted" in 576 while
    // the real total was 577.
    Vars::Bank bank;
    TestSite::build(bank);
    CHECK(bank.refused_count() == 0);
    CHECK(bank.param_stray_count() == 0);
    INFO("...and the hand-kept LIBRARY_ROWS is not UNDER the truth, which is "
         "the direction that overflows. It may be over: freeze() compacts a "
         "redeclared row out, so the live count is the declared total minus "
         "however many rows this site restates.");
    CHECK(bank.count() <=
          (uint16_t)(Site::LIBRARY_ROWS + Site::ROWS_COUNT));
}


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
