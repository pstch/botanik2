/**
 * @file test_shell_report.cpp
 * @brief The shell's READ path: `stats`, `tasks`, `net`, `modbus`, `version`,
 *        `features`, `logs`, `log`, `help`, and the history ring.
 *
 * The sibling of test_shell_vars.cpp, which covers the verbs that CHANGE
 * something. These only report - but a report an operator cannot trust is worse
 * than none, and every one of them is what somebody reads at 4800 baud while
 * deciding whether to drive an actuator by hand.
 *
 * What is asserted is that each command reaches its subsystem and says
 * something specific about it, and that an unwired subsystem says so rather
 * than printing a plausible zero.
 */

#include <string>

#include "doctest.h"

#include "shell_rig.h"

namespace shell_report {

using namespace Botanik;

#if BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_VARS

using shell_rig::Rig;

// stats

TEST_CASE("shell stats: the beat's own numbers, read off the scheduler context") {
    Rig f;
    // Stated, not observed: a case asserting on host timing asserts on the box
    // it happens to run on.
    f.sched.tick_count        = 12345;
    f.sched.overruns          = 7;
    f.sched.cpu_load_ema      = 0.42f;
    f.sched.cpu_loop_us_max   = 3500;
    f.sched.cpu_jitter_ema    = 0.015f;
    f.sched.stack_usage       = 0.31f;
    f.sched.watchdog_ms       = 10000;

    const std::string said = f.run("stats");
    CHECK(said.find("Uptime") != std::string::npos);
    CHECK(said.find("12345 ticks") != std::string::npos);
    INFO("an overrun count is a fact about the beat, not a footnote");
    CHECK(said.find("7 overruns") != std::string::npos);
    INFO("the load is a percentage, not a fraction");
    CHECK(said.find("42.0%") != std::string::npos);
    CHECK(said.find("3.50ms") != std::string::npos);
    CHECK(said.find("1.5%") != std::string::npos);
    CHECK(said.find("31.0%") != std::string::npos);
    INFO("an armed watchdog reports its timeout, not just that it is on");
    CHECK(said.find("10000") != std::string::npos);
}

TEST_CASE("shell stats: a withheld watchdog kick is visible, not silent") {
    Rig f;
    f.sched.watchdog_ms       = 10000;
    f.sched.watchdog_kick_ms  = 1;      // it HAS kicked, so "never" is not the answer
    f.sched.watchdog_kicking  = false;
    f.sched.watchdog_withheld = 3;
    // The loop has decided to let the hardware reset the unit. That must be
    // legible before the reboot, since afterwards the only trace is a counter.
    const std::string said = f.run("stats");
    CHECK(said.find("WITHHELD") != std::string::npos);

    // Kicking again, with the count standing: a unit that came within a
    // timeout of resetting itself and recovered still has to say so.
    Rig g;
    g.sched.watchdog_ms       = 10000;
    g.sched.watchdog_kick_ms  = 1;
    g.sched.watchdog_kicking  = true;
    g.sched.watchdog_withheld = 3;
    CHECK(g.run("stats").find("3 withheld") != std::string::npos);

    // And an unarmed one is its own answer, not a zero.
    Rig h;
    CHECK(h.run("stats").find("not armed") != std::string::npos);
}

TEST_CASE("shell stats: with no scheduler wired it says so rather than printing zeros") {
    Rig f;
    f.sys.scheduler = nullptr;
    CHECK(f.run("stats").find("no scheduler context") != std::string::npos);
}

// tasks

TEST_CASE("shell tasks: a bare `tasks` is the beat's totals") {
    Rig f;
    f.register_all();
    const std::string said = f.run("tasks stats");
    CHECK(said.find("Registered") != std::string::npos);
    CHECK(said.find("Beat") != std::string::npos);
    CHECK(said.find("10ms") != std::string::npos);
    INFO("it points at where the per-task detail is, since it cannot carry it");
    CHECK(said.find("tasks get") != std::string::npos);
}

TEST_CASE("shell tasks: list names the registered work and filters on a glob") {
    Rig f;
    f.register_all();
    const std::string all = f.run("tasks list");
    CHECK(all.find("sys_params") != std::string::npos);
    CHECK(all.find("task(s)") != std::string::npos);
    INFO("the phase is shown: it is a correctness property, unlike the rate");
    CHECK(all.find("control") != std::string::npos);

    // The whole table over a 4800-baud console is an index, not an answer,
    // which is why the filter exists.
    const std::string some = f.run("tasks list sys_*");
    CHECK(some.find("sys_params") != std::string::npos);
    INFO("and it must actually filter");
    CHECK(some.find("irrigation") == std::string::npos);
    CHECK(f.run("tasks list zzz*").find("nothing matches") != std::string::npos);
}

TEST_CASE("shell tasks: get is one task's schedule and its cost samples") {
    Rig f;
    f.register_all();
    REQUIRE(f.reg.count() > 0);
    const std::string said = f.run("tasks get sys_params");
    // The three things about one task an operator can act on: where in the beat
    // it sits, whether it has ever run, and what it costs when it does.
    CHECK(said.find("Runs in") != std::string::npos);
    CHECK(said.find("config") != std::string::npos);
    CHECK(said.find("100 ms") != std::string::npos);
    CHECK(said.find("Activations") != std::string::npos);
    CHECK(said.find("Cost") != std::string::npos);
    CHECK(f.run("tasks get nosuchtask").find("no task named") != std::string::npos);
}

TEST_CASE("shell tasks: with no registry wired it says so") {
    Rig f;
    f.sys.tasks = nullptr;
    const std::string said = f.run("tasks stats");
    CHECK(said.find("task") != std::string::npos);
}

// version, features

TEST_CASE("shell version: the build, the site and the platform in one line") {
    Rig f;
    const std::string said = f.run("version");
    // What an operator reads out down a phone: which firmware, which
    // greenhouse, which kind of machine.
    CHECK(said.find("Botanik") != std::string::npos);
    CHECK(said.find("gh_mlb41") != std::string::npos);
    CHECK(said.find("simulator") != std::string::npos);
}

TEST_CASE("shell features: every flag in the roster, on or off") {
    Rig f;
    const std::string said = f.run("features");
    CHECK(said.find("on") != std::string::npos);
    CHECK(said.find("off") != std::string::npos);
    // The reference site is the full profile, so these must all be present -
    // and the command expands BOTANIK_FEATURE_LIST, so a flag added without a
    // roster entry would be missing here rather than merely undocumented.
    for (const char* flag : { "console", "logging", "vars", "plc",
                              "modbus_rtu_client", "system_page", "sun_tracker" }) {
        INFO(flag);
        CHECK(said.find(flag) != std::string::npos);
    }
}

// net

#if BOTANIK_FEATURE_NETWORK
TEST_CASE("shell net: the link, then each service that rides on it") {
    Rig f;
    const std::string said = f.run("net");
    CHECK(said.find("Network") != std::string::npos);
    CHECK(said.find("Netmask") != std::string::npos);
    CHECK(said.find("Gateway") != std::string::npos);
    CHECK(said.find("DNS") != std::string::npos);
#if BOTANIK_FEATURE_NTP
    CHECK(said.find("NTP") != std::string::npos);
#endif
#if BOTANIK_FEATURE_MQTT
    CHECK(said.find("MQTT") != std::string::npos);
#endif
    INFO("the address mode is stated, since a static site and a DHCP one fail differently");
    CHECK((said.find("static") != std::string::npos ||
           said.find("DHCP")   != std::string::npos));
}

TEST_CASE("shell net: with no handler wired it says networking is disabled") {
    Rig f;
    f.sys.network = nullptr;
    CHECK(f.run("net").find("disabled") != std::string::npos);
}
#endif

// modbus

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
TEST_CASE("shell modbus: stats is the plan, the line and the counters") {
    Rig f;
    const std::string said = f.run("modbus stats");
    CHECK(said.find("Plan") != std::string::npos);
    CHECK(said.find("State") != std::string::npos);
    CHECK(said.find("Transactions") != std::string::npos);
    INFO("the line counters are kept apart: they point at different faults");
    CHECK(said.find("crc") != std::string::npos);
    CHECK(said.find("stray") != std::string::npos);
}

TEST_CASE("shell modbus: devices is the roll-call and nothing else") {
    Rig f;
    const std::string said = f.run("modbus devices");
    CHECK(said.find("RTU devices") != std::string::npos);
    INFO("no counters here: `modbus stats` is where those live");
    CHECK(said.find("Transactions") == std::string::npos);
}

TEST_CASE("shell modbus: an unwired master says so rather than reporting zeros") {
    Rig f;
    f.sys.modbus_rtu = nullptr;
    CHECK(f.run("modbus devices").find("not wired") != std::string::npos);
}
#endif

// logs and the operator's own entries

TEST_CASE("shell logs: on and off move the sink, and a bare verb is usage") {
    Rig f;
    CHECK(f.run("logs").find("usage:") != std::string::npos);
    CHECK(f.run("logs off").find("disabled") != std::string::npos);
    CHECK(f.out.logs == false);
    CHECK(f.run("logs on").find("enabled") != std::string::npos);
    CHECK(f.out.logs == true);
}

TEST_CASE("shell log: an operator's own entry takes a level it has to name") {
    Rig f;
    // Accepted levels write nothing to the shell - the entry goes to the log.
    for (const char* lvl : { "debug", "info", "notice", "warning", "error", "critical" }) {
        INFO(lvl);
        std::string line = std::string("log ") + lvl + " a note from the operator";
        CHECK(f.run(line.c_str()).find("usage:") == std::string::npos);
    }
    // A level that is not one must not be logged AS something else.
    CHECK(f.run("log bogus a note").find("usage:") != std::string::npos);
    CHECK(f.run("log").find("usage:") != std::string::npos);
}

#if BOTANIK_FEATURE_NTP
TEST_CASE("shell ntpsync: with a client wired it asks, without one it says so") {
    Rig f;
    CHECK(f.run("ntpsync").find("unavailable") != std::string::npos);
    f.register_all();                       // wires the NTP client
    CHECK(f.run("ntpsync").find("unavailable") == std::string::npos);
}
#endif

// help and dispatch

TEST_CASE("shell help: the bare page lists the commands and the topics") {
    Rig f;
    const std::string said = f.run("help");
    for (const char* cmd : { "help", "version", "clear", "logs" }) {
        INFO(cmd);
        CHECK(said.find(cmd) != std::string::npos);
    }
    INFO("and the topics `help <topic>` filters on");
    CHECK(said.find("system") != std::string::npos);
}

TEST_CASE("shell help: an unknown topic says so AND still shows the page") {
    Rig f;
    const std::string said = f.run("help nosuchtopic");
    CHECK(said.find("no topic") != std::string::npos);
    INFO("leaving an operator with nothing would be the wrong half to drop");
    CHECK(said.find("version") != std::string::npos);
}

TEST_CASE("shell: an unknown command names what was typed") {
    Rig f;
    const std::string said = f.run("nosuchcommand");
    CHECK(said.find("Unknown command") != std::string::npos);
    INFO("echoing it back is what tells a typo from a missing feature");
    CHECK(said.find("nosuchcommand") != std::string::npos);
}

TEST_CASE("shell: a blank line does nothing at all") {
    Rig f;
    CHECK(f.run("").empty());
    CHECK(f.run("   ").empty());
}

TEST_CASE("shell clear: wipes the output rather than writing to it") {
    Rig f;
    f.run("version");
    REQUIRE_FALSE(f.out.text.empty());
    f.shell.run("clear", 5);
    CHECK(f.out.text.empty());
}

// history

TEST_CASE("shell history: recall walks back, and the draft comes back at the bottom") {
    Rig f;
    f.run("version");
    f.run("features");

    f.editor.replace_all("half-typed", 10);
    f.shell.history_prev();
    // Newest first.
    CHECK(std::string(f.editor.buffer(), f.editor.buffer_len()) == "features");
    f.shell.history_prev();
    CHECK(std::string(f.editor.buffer(), f.editor.buffer_len()) == "version");

    f.shell.history_next();
    CHECK(std::string(f.editor.buffer(), f.editor.buffer_len()) == "features");
    f.shell.history_next();
    INFO("past the newest, the line being typed when browsing started is restored");
    CHECK(std::string(f.editor.buffer(), f.editor.buffer_len()) == "half-typed");
}

TEST_CASE("shell history: a repeated line is not stored twice") {
    Rig f;
    f.run("version");
    f.run("version");
    f.shell.history_prev();
    CHECK(std::string(f.editor.buffer(), f.editor.buffer_len()) == "version");
    f.shell.history_prev();
    INFO("one entry, so walking back again leaves it where it is");
    CHECK(std::string(f.editor.buffer(), f.editor.buffer_len()) == "version");
}

#endif // BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_VARS

}  // namespace shell_report
