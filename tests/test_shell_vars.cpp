/**
 * @file test_shell_vars.cpp
 * @brief The shell's write path: `params`, `overrides`, `programs`, `alarms`.
 *
 * These are the commands that change what the plant does, and every one of them
 * runs against the site's real table rather than a hand-built one - a band a
 * `params set` is judged by, a `_qlt` companion an override drags with it, the
 * scan roster a `programs disable` takes a law out of. `test_shell.cpp` wires no
 * SystemContext and so tests the dispatcher; this tests what the verbs do.
 *
 * Every assertion here is on the operator-visible answer, not on a member: a
 * verb that silently did the right thing and said the wrong one has failed at
 * the only interface it has.
 */

#include <string>

#include "doctest.h"

#include "ram_store.h"
#include "shell_rig.h"

namespace tests { namespace shell_vars {

using namespace Botanik;

#if BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_VARS

using shell_rig::Rig;

using test_store::RamStore;

// params

TEST_CASE("shell params: set applies live, and says the edit is not durable yet") {
    Rig f;
    // 3.0 is the table's default; the reply has to carry the NEW value, or an
    // operator cannot tell an accepted edit from a refused one.
    CHECK(f.run("params set cli_windowRedatum 7.5").find("7.5") != std::string::npos);
    CHECK(f.said("unsaved"));
    INFO("the reply must name the command that makes it durable");
    CHECK(f.said("params save cli_windowRedatum"));
    // ...and it actually moved.
    CHECK(f.var("cli_windowRedatum").as_f32() == doctest::Approx(7.5f));
    CHECK(f.bank.unsaved_count() == 1);
}

TEST_CASE("shell params: a value outside the row's band is refused, and nothing moves") {
    Rig f;
    const float before = f.var("cli_windowRedatum").as_f32();
    // The band is 0.0 .. 50.0. Both ends, and a word that is not a number.
    for (const char* bad : { "999", "-1", "banana" }) {
        INFO(bad);
        std::string line = std::string("params set cli_windowRedatum ") + bad;
        CHECK(f.run(line.c_str()).find("invalid value") != std::string::npos);
        INFO("a refusal must name the row, or a script cannot tell which set failed");
        CHECK(f.said("cli_windowRedatum"));
    }
    CHECK(f.var("cli_windowRedatum").as_f32() == doctest::Approx(before));
    INFO("a refused edit owes storage nothing");
    CHECK(f.bank.unsaved_count() == 0);
}

TEST_CASE("shell params: a non-parameter is refused, and points at the verb that would work") {
    Rig f;
    // cli_temp_in is a reading. Refusing without saying `overrides set` is the
    // difference between a wall and a signpost.
    f.run("params set cli_temp_in 5");
    CHECK(f.said("not a parameter"));
    CHECK(f.said("overrides set cli_temp_in"));
    CHECK(f.var("cli_temp_in").as_f32() != doctest::Approx(5.0f));
}

TEST_CASE("shell params: an unknown name is a typo, not a defect") {
    Rig f;
    f.run("params set nosuchrow 1");
    CHECK(f.said("unknown parameter"));
}

TEST_CASE("shell params: save commits, and says so only when storage took it") {
    Rig f;
    RamStore store;
    f.persist.set_store(&store);

    f.run("params set cli_windowRedatum 7.5");
    f.run("params save cli_windowRedatum");
    CHECK(store.stored("cli_windowRedatum"));
    CHECK(f.bank.unsaved_count() == 0);

    // ...and a storage that refuses must not report success.
    store.refuse = true;
    f.run("params set cli_windowRedatum 8.5");
    f.run("params save cli_windowRedatum");
    INFO("a refused write reported as saved is how a setting is silently lost");
    CHECK(f.said("NOT saved"));
}

TEST_CASE("shell params: with no store at all, save refuses rather than pretending") {
    Rig f;                                  // no store bound
    f.run("params set cli_windowRedatum 7.5");
    f.run("params save cli_windowRedatum");
    CHECK(f.said("NOT saved"));
    CHECK(f.said("storage refused"));
}

TEST_CASE("shell params: clear drops the unsaved edit, reset drops the stored copy") {
    Rig f;
    RamStore store;
    f.persist.set_store(&store);
    const float factory = f.var("cli_windowRedatum").as_f32();

    f.run("params set cli_windowRedatum 7.5");
    f.run("params clear cli_windowRedatum");
    INFO("clear puts the row back to what a reboot would give it");
    CHECK(f.var("cli_windowRedatum").as_f32() == doctest::Approx(factory));
    CHECK(f.bank.unsaved_count() == 0);

    f.run("params set cli_windowRedatum 9.0");
    f.run("params save cli_windowRedatum");
    REQUIRE(store.stored("cli_windowRedatum"));
    f.run("params reset cli_windowRedatum");
    INFO("reset is the destructive sibling: the stored copy goes too");
    CHECK(f.var("cli_windowRedatum").as_f32() == doctest::Approx(factory));
    CHECK_FALSE(store.stored("cli_windowRedatum"));
}

TEST_CASE("shell params: reset refuses a RETAINED row and says why") {
    Rig f;
    // irr_last_tray1 is when tray 1 was last watered. Its "default" is not a
    // factory setting to go back to - it is a fact that has not happened yet.
    f.run("params reset irr_last_tray1");
    CHECK(f.said("retained"));
    CHECK(f.said("leaves it alone"));
    INFO("the refusal must say which verb DOES move it");
    CHECK(f.said("params set"));
}

TEST_CASE("shell params: list filters on the glob and counts what it showed") {
    Rig f;
    const std::string all = f.run("params list cli_window*");
    CHECK(all.find("cli_windowRedatum") != std::string::npos);
    CHECK(all.find("9 parameter(s)") != std::string::npos);
    INFO("the filter must actually filter");
    CHECK(all.find("irr_last_tray1") == std::string::npos);
    // A glob matching nothing is an answer, not silence - and it names the
    // noun that was asked, since three of them share the walk.
    CHECK(f.run("params list zzz*").find("params: nothing matches") != std::string::npos);
    CHECK(f.run("vars list zzz*").find("vars: nothing matches") != std::string::npos);
}

// overrides

TEST_CASE("shell overrides: a force takes the row AND its quality companion") {
    Rig f;
    f.run("overrides set cli_temp_in 21.5");
    CHECK(f.said("forced to 21.5"));
    CHECK(f.var("cli_temp_in").as_f32() == doctest::Approx(21.5f));

    // Two rows, not one: forcing a reading without forcing its quality would
    // leave a law substituting the declared value over the force.
    const std::string list = f.run("overrides list");
    CHECK(list.find("cli_temp_in") != std::string::npos);
    CHECK(list.find("cli_temp_in_qlt") != std::string::npos);
    CHECK(list.find("2 override(s) active") != std::string::npos);
    INFO("a forced value is BELIEVED, or forcing a dead probe could not work");
    CHECK(f.var("cli_temp_in").valid());
}

TEST_CASE("shell overrides: `invalid` forces the companion, and the reading survives it") {
    Rig f;
    f.run("overrides set cli_temp_in 21.5");
    f.run("overrides set cli_temp_in invalid");
    CHECK(f.said("invalid"));
    INFO("the operator saying `no` beats the force's own belief");
    CHECK_FALSE(f.var("cli_temp_in").valid());
    INFO("...but value() never substitutes: a page still shows what was forced");
    CHECK(f.var("cli_temp_in").as_f32() == doctest::Approx(21.5f));
}

TEST_CASE("shell overrides: clear releases the companion with the row") {
    Rig f;
    f.run("overrides set cli_temp_in 21.5");
    REQUIRE(f.run("overrides list").find("2 override(s)") != std::string::npos);

    f.run("overrides clear cli_temp_in");
    CHECK(f.said("override cleared"));
    INFO("a released reading must not go on being believed by what forced it");
    CHECK(f.run("overrides list").find("none active") != std::string::npos);
}

TEST_CASE("shell overrides: clear all releases the lot and reports the count") {
    Rig f;
    f.run("overrides set cli_temp_in 21.5");
    f.run("overrides set cli_temp_ext 4.0");
    CHECK(f.run("overrides clear all").find("cleared 4") != std::string::npos);
    CHECK(f.run("overrides list").find("none active") != std::string::npos);
    // ...and doing it twice is not an error, it is a no-op that says so.
    CHECK(f.run("overrides clear all").find("cleared 0") != std::string::npos);
}

TEST_CASE("shell overrides: clearing a row nothing forced is a no-op, not an error") {
    Rig f;
    // Idempotent on purpose: a release is what an operator reaches for when they
    // are not sure whether anything is forced, and it must not fail on `no`.
    f.run("overrides clear cli_temp_in");
    CHECK(f.said("override cleared"));
    CHECK(f.run("overrides list").find("none active") != std::string::npos);
    // The AUDIT line is the half that must stay honest: nothing was released,
    // so nothing is recorded. @see Bank::_say_forced
    CHECK(f.bank.unsaved_override_count() == 0);
}

TEST_CASE("shell overrides: an unknown name is refused by the noun that was asked") {
    Rig f;
    // The message names `overrides`, not `vars`: three nouns share the lookup,
    // and an operator has to be told which command failed.
    f.run("overrides set nosuchrow 1");
    CHECK(f.said("overrides: unknown variable"));
}

TEST_CASE("shell overrides: save persists the force, and survives a reload") {
    Rig f;
    RamStore store;
    f.persist.set_store(&store);
    f.run("overrides set cli_temp_in 21.5");
    f.run("overrides save cli_temp_in");
    // The companion goes with it: saving the reading and not its quality would
    // restore a forced value at the next boot that nothing believes.
    CHECK(store.stored("cli_temp_in", "ovr."));
    CHECK(store.stored("cli_temp_in_qlt", "ovr."));
    CHECK(f.bank.unsaved_override_count() == 0);

    // A release must forget the stored copy, or the override comes back at the
    // next boot after an operator deliberately dropped it.
    f.run("overrides clear cli_temp_in");
    CHECK_FALSE(store.stored("cli_temp_in", "ovr."));
    CHECK_FALSE(store.stored("cli_temp_in_qlt", "ovr."));
}

TEST_CASE("shell overrides: an output can be forced whatever control authority says") {
    Rig f;
    // Staging a hand-drive with the programs stopped is the whole point: with
    // control off, an override is the only way left to command an actuator.
    const std::string said = f.run("overrides set sys_control 0");
    CHECK(said.find("forced") != std::string::npos);
    CHECK(f.run("overrides set cli_windowRedatum 5.0").find("forced") != std::string::npos);
}

// vars

TEST_CASE("shell vars: get is the value alone, info is the whole row") {
    Rig f;
    const std::string got = f.run("vars get cli_temp_in");
    CHECK(got.find("degC") != std::string::npos);
    INFO("`get` is one line: no leader rows, no description");
    CHECK(got.find("Description") == std::string::npos);

    const std::string info = f.run("vars info cli_temp_in");
    for (const char* field : { "Kind", "Access", "Raw value", "Default",
                               "Source", "Description" }) {
        INFO(field);
        CHECK(info.find(field) != std::string::npos);
    }
}

TEST_CASE("shell vars: info shows the override beside the raw value, not instead of it") {
    Rig f;
    f.run("overrides set cli_temp_in 21.5");
    const std::string info = f.run("vars info cli_temp_in");
    // Both, because the question an operator has is what the force is HIDING.
    CHECK(info.find("Raw value") != std::string::npos);
    CHECK(info.find("14.0") != std::string::npos);
    CHECK(info.find("Override value") != std::string::npos);
    CHECK(info.find("21.5") != std::string::npos);
}

TEST_CASE("shell vars: list reaches rows the parameter block cannot") {
    Rig f;
    const std::string got = f.run("vars list cli_temp*");
    // `vars` is the unfiltered noun: readings, and the quality companions no
    // other listing shows.
    CHECK(got.find("cli_temp_in") != std::string::npos);
    CHECK(got.find("cli_temp_in_qlt") != std::string::npos);
    CHECK(got.find("variable(s)") != std::string::npos);
    INFO("params list must NOT show a reading");
    CHECK(f.run("params list cli_temp*").find("cli_temp_in ") == std::string::npos);
}

TEST_CASE("shell vars: an unrecognised verb prints the noun's usage, not an error") {
    Rig f;
    struct Case { const char* line; const char* verb; };
    static const Case CASES[] = {
        { "vars frobnicate",      "vars get"          },
        { "params frobnicate",    "params set"        },
        { "overrides frobnicate", "overrides clear"   },
        { "programs frobnicate",  "programs disable"  },
        { "alarms frobnicate",    "alarms ack"        },
    };
    for (const Case& c : CASES) {
        INFO(c.line);
        const std::string said = f.run(c.line);
        CHECK(said.find("usage:") != std::string::npos);
        INFO("the usage must list the verbs, or it is only half an answer");
        CHECK(said.find(c.verb) != std::string::npos);
    }
}

// programs

#if BOTANIK_FEATURE_PLC

TEST_CASE("shell programs: list names every program in the scan, with its state") {
    Rig f;
    const std::string got = f.run("programs list");
    CHECK(got.find("28 program(s)") != std::string::npos);
    CHECK(got.find("irrigation") != std::string::npos);
    CHECK(got.find("HEALTHY") != std::string::npos);
    CHECK(got.find("running") != std::string::npos);
    // The glob filters on the INSTANCE name, which is what `disable` takes.
    CHECK(f.run("programs list irr*").find("6 program(s)") != std::string::npos);
}

TEST_CASE("shell programs: disable takes a law out of the scan and list says so") {
    Rig f;
    f.run("programs disable irrigation");
    CHECK(f.said("1 program(s) DISABLED"));
    CHECK(f.run("programs list irrigation").find("DISABLED") != std::string::npos);
    CHECK(f.run("programs status").find("1 DISABLED") != std::string::npos);

    f.run("programs enable irrigation");
    CHECK(f.said("1 program(s) enabled"));
    CHECK(f.run("programs list irrigation").find("running") != std::string::npos);
}

TEST_CASE("shell control: stop takes every law out of the scan, start puts them back") {
    Rig f;
    CHECK(f.run("control status").find("28 of 28 running") != std::string::npos);

    f.run("control stop");
    CHECK(f.said("control STOPPED"));
    CHECK_FALSE(f.var("sys_control").as_bool());
    CHECK(f.run("control status").find("0 of 28 running") != std::string::npos);

    f.run("control start");
    CHECK(f.said("control started"));
    CHECK(f.var("sys_control").as_bool());
    CHECK(f.run("control status").find("28 of 28 running") != std::string::npos);
}

TEST_CASE("shell control: the two gates are independent, and status says which holds") {
    Rig f;
    // The operator's half alone is not enough: a unit outside RUN drives
    // nothing whatever sys_control says, and an operator who has just started
    // control needs telling that. The firmware hands the plant the run state;
    // the rig does not, and a plant without one reads every beat as RUN.
    f.plant.set_run_state(&f.sched.run_state);
    f.sched.run_state = Scheduler::RunState::START;
    const std::string got = f.run("control start");
    CHECK(got.find("control started") != std::string::npos);
    CHECK(got.find("not in RUN") != std::string::npos);
    CHECK(got.find("0 of 28 running") != std::string::npos);
}

TEST_CASE("shell programs: the gate is the operator's, and it takes a glob") {
    Rig f;
    f.run("programs disable irr*");
    CHECK(f.said("6 program(s) DISABLED"));
    CHECK(f.run("programs status").find("6 DISABLED") != std::string::npos);
    f.run("programs enable all");
    CHECK(f.run("programs status").find("0 DISABLED") != std::string::npos);
}

TEST_CASE("shell programs: enabling under an inhibited sys_control says it changes nothing") {
    Rig f;
    // The operator's gate is one of two and the stricter wins; an enabled
    // program that still will not run is exactly the confusing case.
    f.var("sys_control").override_set(Vars::Scalar::B(false));
    REQUIRE_FALSE(f.plant.control_allowed());
    f.run("programs enable irrigation");
    CHECK(f.said("sys_control is off"));
    CHECK(f.said("none of them will run"));
}

TEST_CASE("shell programs: a bare gate verb refuses rather than acting on everything") {
    Rig f;
    // `programs disable` with no argument must not mean `all`: that is the one
    // typo that would stop the whole plant.
    f.run("programs disable");
    CHECK(f.said("name it, or `all`"));
    CHECK(f.run("programs status").find("0 DISABLED") != std::string::npos);

    f.run("programs disable nosuchprog");
    CHECK(f.said("no such program"));
    CHECK(f.run("programs status").find("0 DISABLED") != std::string::npos);
}

TEST_CASE("shell programs: status is a tally plus the ones worth naming") {
    Rig f;
    const std::string clean = f.run("programs status");
    CHECK(clean.find("programs: 28") != std::string::npos);
    CHECK(clean.find("28 running") != std::string::npos);
    CHECK(clean.find("sys_control on") != std::string::npos);
    INFO("a clean plant names nobody: the tally is the whole answer");
    CHECK(clean.find("DISABLED  ") == std::string::npos);

    f.run("programs disable irrigation");
    INFO("...and a disabled one is named, since that is what an operator needs");
    CHECK(f.run("programs status").find("DISABLED  irrigation") != std::string::npos);
}

#endif // BOTANIK_FEATURE_PLC

// alarms

/** @brief Raise @p name's boolean and step the model past its ON dwell. */
static void raise(Rig& f, const char* name, uint64_t secs = 30) {
    f.bank.var(f.bank.find(name)).set(true);
    for (uint64_t t = 0; t <= secs * 1000; t += 1000) f.alarms.step(t);
}

TEST_CASE("shell alarms: list shows the active conditions and nothing else") {
    Rig f;
    CHECK(f.run("alarms list").find("no active alarms") != std::string::npos);

    raise(f, "alarm_ac_grid");
    const std::string got = f.run("alarms list");
    CHECK(got.find("AC_GRID") != std::string::npos);
    CHECK(got.find("alarm_ac_grid") != std::string::npos);
    INFO("a clear condition must not appear in a list of active ones");
    CHECK(got.find("DC_PS1") == std::string::npos);
    // The glob filters on the CODE, which is what the alarm bar shows.
    CHECK(f.run("alarms list AC_*").find("AC_GRID") != std::string::npos);
    CHECK(f.run("alarms list WTR_*").find("no active alarms") != std::string::npos);
}

TEST_CASE("shell alarms: get answers to the code and to the variable name alike") {
    Rig f;
    // The alarm bar shows the code, `vars get` takes the name; an operator
    // reading one screen and typing into another must not have to translate.
    for (const char* key : { "AC_GRID", "alarm_ac_grid" }) {
        INFO(key);
        const std::string got = f.run((std::string("alarms get ") + key).c_str());
        CHECK(got.find("AC_GRID") != std::string::npos);
        CHECK(got.find("alarm_ac_grid") != std::string::npos);
        INFO("the dwell policy, since an alarm that should be up may be in its on-delay");
        CHECK(got.find("Dwell") != std::string::npos);
        INFO("and the remediation, which is the half a code cannot carry");
        CHECK(got.find("Remediation") != std::string::npos);
    }
    CHECK(f.run("alarms get NOPE").find("no alarm 'NOPE'") != std::string::npos);
}

TEST_CASE("shell alarms: get says whether it is up, and for how long") {
    Rig f;
    CHECK(f.run("alarms get AC_GRID").find("clear") != std::string::npos);
    raise(f, "alarm_ac_grid");
    const std::string up = f.run("alarms get AC_GRID");
    CHECK(up.find("ACTIVE") != std::string::npos);
    CHECK(up.find("Active for") != std::string::npos);
}

TEST_CASE("shell alarms: ack marks the condition, and only an active one") {
    Rig f;
    // Acknowledging something that is not up would be an operator dismissing a
    // warning that has not been given.
    CHECK(f.run("alarms ack AC_GRID").find("no active alarm") != std::string::npos);

    raise(f, "alarm_ac_grid");
    CHECK(f.run("alarms ack AC_GRID").find("acknowledged AC_GRID") != std::string::npos);
    const int idx = f.alarms.index_of("AC_GRID");
    REQUIRE(idx >= 0);
    CHECK(f.alarms.condition((uint8_t)idx).user_ack);
    INFO("the ack shows on the listing, or an operator acks twice");
    CHECK(f.run("alarms list").find("[ack]") != std::string::npos);
}

TEST_CASE("shell alarms: ack all takes every active condition and reports the count") {
    Rig f;
    CHECK(f.run("alarms ack all").find("acknowledged 0") != std::string::npos);
    raise(f, "alarm_ac_grid");
    raise(f, "alarm_dc_ps1");
    CHECK(f.run("alarms ack all").find("acknowledged 2") != std::string::npos);
}

TEST_CASE("shell alarms: an ack belongs to one episode") {
    Rig f;
    raise(f, "alarm_ac_grid");
    f.run("alarms ack AC_GRID");
    // Down, then up again: a fresh episode is not covered by the last ack.
    f.bank.var(f.bank.find("alarm_ac_grid")).set(false);
    for (uint64_t t = 31000; t <= 60000; t += 1000) f.alarms.step(t);
    raise(f, "alarm_ac_grid", 90);
    CHECK(f.run("alarms list").find("[ack]") == std::string::npos);
}

#endif // BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_VARS

}}  // namespace tests::shell_vars
