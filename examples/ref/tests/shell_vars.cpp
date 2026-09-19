/**
 * @file shell_vars.cpp
 * @brief The shell's write path: `params`, `overrides`, `programs`, `alarms`.
 *
 * These are the commands that change what the plant does, and every one of them
 * runs against the site's real table rather than a hand-built one - a band a
 * `params set` is judged by, a `_qlt` companion an override drags with it, the
 * scan roster a `programs stop` takes a law out of. `test_shell.cpp` wires no
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

#if BOTANIK_FEATURE_SHELL_PAGE

using shell_rig::Rig;

using test_store::RamStore;

// params

TEST_CASE("shell params: set applies live, and says the edit is not durable yet") {
    Rig f;
    // 3.0 is the table's default; the reply has to carry the NEW value, or an
    // operator cannot tell an accepted edit from a refused one.
    CHECK(f.run("params set vent_redatum 7.5").find("7.5") != std::string::npos);
    CHECK(f.said("unsaved"));
    INFO("the reply must name the command that makes it durable");
    CHECK(f.said("params save vent_redatum"));
    // ...and it actually moved.
    CHECK(f.var("vent_redatum").as_f32() == doctest::Approx(7.5f));
    CHECK(f.bank.unsaved_count() == 1);
}

TEST_CASE("shell params: a value outside the row's band is refused, and nothing moves") {
    Rig f;
    const float before = f.var("vent_redatum").as_f32();
    // The band is 0.0 .. 50.0. Both ends, and a word that is not a number.
    static const char* const bads[] = { "999", "-1", "banana" };
    for (const char* bad : bads) {
        INFO(bad);
        std::string line = std::string("params set vent_redatum ") + bad;
        CHECK(f.run(line.c_str()).find("invalid value") != std::string::npos);
        INFO("a refusal must name the row, or a script cannot tell which set failed");
        CHECK(f.said("vent_redatum"));
    }
    CHECK(f.var("vent_redatum").as_f32() == doctest::Approx(before));
    INFO("a refused edit owes storage nothing");
    CHECK(f.bank.unsaved_count() == 0);
}

TEST_CASE("shell params: a non-parameter is refused, and points at the verb that would work") {
    Rig f;
    // air_temp_in is a reading. Refusing without saying `overrides set` is the
    // difference between a wall and a signpost.
    f.run("params set air_temp_in 5");
    CHECK(f.said("not a parameter"));
    CHECK(f.said("overrides set air_temp_in"));
    CHECK(f.var("air_temp_in").as_f32() != doctest::Approx(5.0f));
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

    f.run("params set vent_redatum 7.5");
    f.run("params save vent_redatum");
    CHECK(store.stored("vent_redatum"));
    CHECK(f.bank.unsaved_count() == 0);

    // ...and a storage that refuses must not report success.
    store.refuse = true;
    f.run("params set vent_redatum 8.5");
    f.run("params save vent_redatum");
    INFO("a refused write reported as saved is how a setting is silently lost");
    CHECK(f.said("NOT saved"));
}

TEST_CASE("shell params: with no store at all, save refuses rather than pretending") {
    Rig f;                                  // no store bound
    f.run("params set vent_redatum 7.5");
    f.run("params save vent_redatum");
    CHECK(f.said("NOT saved"));
    CHECK(f.said("storage refused"));
}

TEST_CASE("shell params: clear drops the unsaved edit, reset drops the stored copy") {
    Rig f;
    RamStore store;
    f.persist.set_store(&store);
    const float factory = f.var("vent_redatum").as_f32();

    f.run("params set vent_redatum 7.5");
    f.run("params clear vent_redatum");
    INFO("clear puts the row back to what a reboot would give it");
    CHECK(f.var("vent_redatum").as_f32() == doctest::Approx(factory));
    CHECK(f.bank.unsaved_count() == 0);

    f.run("params set vent_redatum 9.0");
    f.run("params save vent_redatum");
    REQUIRE(store.stored("vent_redatum"));
    f.run("params reset vent_redatum");
    INFO("reset is the destructive sibling: the stored copy goes too");
    CHECK(f.var("vent_redatum").as_f32() == doctest::Approx(factory));
    CHECK_FALSE(store.stored("vent_redatum"));
}

TEST_CASE("shell params: reset refuses a RETAINED row and says why") {
    Rig f;
    // tank_lastFill is when tray 1 was last watered. Its "default" is not a
    // factory setting to go back to - it is a fact that has not happened yet.
    f.run("params reset tank_lastFill");
    CHECK(f.said("retained"));
    CHECK(f.said("leaves it alone"));
    INFO("the refusal must say which verb DOES move it");
    CHECK(f.said("params set"));
}

// overrides

TEST_CASE("shell overrides: a force takes the row AND its quality companion") {
    Rig f;
    f.run("overrides set air_temp_in 21.5");
    CHECK(f.said("forced to 21.5"));
    CHECK(f.var("air_temp_in").as_f32() == doctest::Approx(21.5f));

    // Two rows, not one: forcing a reading without forcing its quality would
    // leave a law substituting the declared value over the force.
    const std::string list = f.run("overrides list");
    CHECK(list.find("air_temp_in") != std::string::npos);
    CHECK(list.find("air_temp_in_qlt") != std::string::npos);
    CHECK(list.find("2 override(s) active") != std::string::npos);
    INFO("a forced value is BELIEVED, or forcing a dead probe could not work");
    CHECK(f.var("air_temp_in").valid());
}

TEST_CASE("shell overrides: `invalid` forces the companion, and the reading survives it") {
    Rig f;
    f.run("overrides set air_temp_in 21.5");
    f.run("overrides set air_temp_in invalid");
    CHECK(f.said("invalid"));
    INFO("the operator saying `no` beats the force's own belief");
    CHECK_FALSE(f.var("air_temp_in").valid());
    INFO("...but value() never substitutes: a page still shows what was forced");
    CHECK(f.var("air_temp_in").as_f32() == doctest::Approx(21.5f));
}

TEST_CASE("shell overrides: clear releases the companion with the row") {
    Rig f;
    f.run("overrides set air_temp_in 21.5");
    REQUIRE(f.run("overrides list").find("2 override(s)") != std::string::npos);

    f.run("overrides clear air_temp_in");
    CHECK(f.said("override cleared"));
    INFO("a released reading must not go on being believed by what forced it");
    CHECK(f.run("overrides list").find("none active") != std::string::npos);
}

TEST_CASE("shell overrides: clearing a row nothing forced is a no-op, not an error") {
    Rig f;
    // Idempotent on purpose: a release is what an operator reaches for when they
    // are not sure whether anything is forced, and it must not fail on `no`.
    f.run("overrides clear air_temp_in");
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
    f.run("overrides set air_temp_in 21.5");
    f.run("overrides save air_temp_in");
    // The companion goes with it: saving the reading and not its quality would
    // restore a forced value at the next boot that nothing believes.
    CHECK(store.stored("air_temp_in", "ovr."));
    CHECK(store.stored("air_temp_in_qlt", "ovr."));
    CHECK(f.bank.unsaved_override_count() == 0);

    // A release must forget the stored copy, or the override comes back at the
    // next boot after an operator deliberately dropped it.
    f.run("overrides clear air_temp_in");
    CHECK_FALSE(store.stored("air_temp_in", "ovr."));
    CHECK_FALSE(store.stored("air_temp_in_qlt", "ovr."));
}

TEST_CASE("shell overrides: an output can be forced whatever control authority says") {
    Rig f;
    // Staging a hand-drive with the programs stopped is the whole point: with
    // control off, an override is the only way left to command an actuator.
    const std::string said = f.run("overrides set sys_control 0");
    CHECK(said.find("forced") != std::string::npos);
    CHECK(f.run("overrides set vent_redatum 5.0").find("forced") != std::string::npos);
}

// vars

TEST_CASE("shell vars: get is the value alone, info is the whole row") {
    Rig f;
    const std::string got = f.run("vars get air_temp_in");
    CHECK(got.find("degC") != std::string::npos);
    INFO("`get` is one line: no leader rows, no description");
    CHECK(got.find("Description") == std::string::npos);

    const std::string info = f.run("vars info air_temp_in");
    static const char* const fields[] = { "Kind", "Access", "Raw value", "Default", "Source", "Description" };
    for (const char* field : fields) {
        INFO(field);
        CHECK(info.find(field) != std::string::npos);
    }
}

TEST_CASE("shell vars: list reaches rows the parameter block cannot") {
    Rig f;
    const std::string got = f.run("vars list air_temp*");
    // `vars` is the unfiltered noun: readings, and the quality companions no
    // other listing shows.
    CHECK(got.find("air_temp_in") != std::string::npos);
    CHECK(got.find("air_temp_in_qlt") != std::string::npos);
    CHECK(got.find("variable(s)") != std::string::npos);
    INFO("params list must NOT show a reading");
    CHECK(f.run("params list air_temp*").find("air_temp_in ") == std::string::npos);
}

TEST_CASE("shell vars: an unrecognised verb prints the noun's usage, not an error") {
    Rig f;
    struct Case { const char* line; const char* verb; };
    static const Case CASES[] = {
        { "vars frobnicate",      "vars get"          },
        { "params frobnicate",    "params set"        },
        { "overrides frobnicate", "overrides clear"   },
        { "programs frobnicate",  "programs stop"  },
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

TEST_CASE("shell control: start/stop stay in RAM, enable/disable reach the store") {
    Rig f;
    RamStore store;
    f.persist.set_store(&store);

    f.run("control stop");
    CHECK(f.said("this run only"));
    CHECK_FALSE(store.stored("sys_control"));

    f.run("control disable");
    CHECK(f.said("saved"));
    CHECK(store.stored("sys_control"));
    CHECK_FALSE(f.var("sys_control").as_bool());

    f.run("control enable");
    CHECK(f.said("saved"));
    CHECK(f.var("sys_control").as_bool());
}

TEST_CASE("shell control: a save storage refuses is said so, not reported as durable") {
    Rig f;
    RamStore store;
    store.refuse = true;
    f.persist.set_store(&store);

    f.run("control disable");
    CHECK(f.said("REFUSED"));
    // The RAM half still took: the plant is stopped either way, and an
    // operator told otherwise would think the laws were still driving.
    CHECK_FALSE(f.var("sys_control").as_bool());
}

TEST_CASE("shell programs: a bare gate verb refuses rather than acting on everything") {
    Rig f;
    // `programs stop` with no argument must not mean `all`: that is the one
    // typo that would stop the whole plant.
    f.run("programs stop");
    CHECK(f.said("name it, or `all`"));
    CHECK(f.run("programs status").find("0 STOPPED") != std::string::npos);

    f.run("programs stop nosuchprog");
    CHECK(f.said("no such program"));
    CHECK(f.run("programs status").find("0 STOPPED") != std::string::npos);
}

#endif // BOTANIK_FEATURE_PLC

// alarms

/** @brief Raise @p name's boolean and step the model past its ON dwell. */
static void raise(Rig& f, const char* name, uint64_t secs = 30) {
    f.bank.var(f.bank.find(name)).set(true);
    for (uint64_t t = 0; t <= secs * 1000; t += 1000) f.alarms.step(t);
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

#endif // BOTANIK_FEATURE_SHELL_PAGE

}}  // namespace tests::shell_vars
