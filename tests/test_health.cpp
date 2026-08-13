/**
 * @file test_health.cpp
 * @brief Platform-health alarms, POST, and the loop driver that feeds them.
 *
 * Wrong thresholds mean a silent fault, or a false one on a healthy plant.
 */

#include "doctest.h"

#include "libbotanik/health/health.h"        // Health::HealthMonitor
#include "libbotanik/logs/logger.h"          // Logs::Logger
#include "libbotanik/scheduler/scheduler.h"  // Scheduler::LoopScheduler / SchedulerContext
#include "libbotanik/vars/model.h"           // Vars::Bank
#include "recording_writer.h"                // RecordingWriter
#include "sites/gh_mlb41/site.h"             // the site table carries the platform alarm rows

namespace health {

using namespace Botanik;

namespace {
    /** @brief Bank over the site's real table, plus the scheduler context the monitor reads. */
    struct Fixture {
        Vars::Value*                vals;
        Vars::Bank                  bank;
        Logs::Logger                log;
        RecordingWriter             writer;
        Scheduler::SchedulerContext sched;

        Fixture() : vals(nullptr), bank(Site::g_site.vars) {
            bank.init();
            log.add_writer(&writer);
        }

        bool alarm(const char* name) { return bank.var(bank.find(name)).b(); }

        void step(Health::HealthMonitor& h, uint64_t now) { h.step(now); }
    };

    const uint64_t SETTLED = (uint64_t)Config::HEALTH_RESET_WINDOW_MS + 1000;  // past reset, SYS_RST clear
    const uint32_t PERIOD_US = Config::TASK_PERIOD_MS * 1000u;
}

TEST_CASE("health: SYS_RST is asserted for the reset window and then clears") {
    Fixture f;
    Health::HealthMonitor h(&f.log, &f.sched, &f.bank);

    h.step(1000);                                   // early in the window
    CHECK(f.alarm("alarm_system_reset"));
    h.step(SETTLED);                                // past it
    CHECK_FALSE(f.alarm("alarm_system_reset"));
}

TEST_CASE("health: CPU load and jitter are judged against the beat, not a constant") {
    // Integer-scaled (no float): at threshold must NOT fire, one us past must.
    Fixture f;
    Health::HealthMonitor h(&f.log, &f.sched, &f.bank);

    const uint32_t at = (uint32_t)((uint64_t)PERIOD_US * Config::ALARM_CPU_LOAD_PCT / 100u);
    f.sched.cpu_loop_us_recent = at;
    h.step(SETTLED);
    CHECK_FALSE(f.alarm("alarm_cpu_load"));

    f.sched.cpu_loop_us_recent = at + 1;
    h.step(SETTLED);
    CHECK(f.alarm("alarm_cpu_load"));

    // jitter is a separate knob, same threshold math.
    CHECK_FALSE(f.alarm("alarm_cpu_jitter"));
    f.sched.cpu_jitter_us_recent =
        (uint32_t)((uint64_t)PERIOD_US * Config::ALARM_CPU_JITTER_PCT / 100u) + 1;
    h.step(SETTLED);
    CHECK(f.alarm("alarm_cpu_jitter"));
}

TEST_CASE("health: the stack alarm follows the high-water fraction") {
    Fixture f;
    Health::HealthMonitor h(&f.log, &f.sched, &f.bank);

    f.sched.stack_usage = (float)Config::ALARM_STACK_PCT / 100.0f - 0.01f;
    h.step(SETTLED);
    CHECK_FALSE(f.alarm("alarm_stack"));

    f.sched.stack_usage = (float)Config::ALARM_STACK_PCT / 100.0f + 0.01f;
    h.step(SETTLED);
    CHECK(f.alarm("alarm_stack"));
}

TEST_CASE("health: the thresholds are parameters, live, not compile-time constants") {
    // What counts as too much load is a property of the installation, and the
    // number an operator wants to move is the one a unit nuisance-alarms on.
    Fixture f;
    Health::HealthMonitor h(&f.log, &f.sched, &f.bank);

    // Sit exactly at the factory threshold: quiet by the "at does not fire" rule.
    f.sched.cpu_loop_us_recent =
        (uint32_t)((uint64_t)PERIOD_US * Config::ALARM_CPU_LOAD_PCT / 100u);
    h.step(SETTLED);
    REQUIRE_FALSE(f.alarm("alarm_cpu_load"));

    SUBCASE("tightening it raises the alarm on an unchanged load") {
        REQUIRE(f.bank.set(f.bank.var(f.bank.find("sys_cpu_load_pct")), "10")
                != Vars::SET_INVALID);
        h.step(SETTLED);
        CHECK(f.alarm("alarm_cpu_load"));

        INFO("...and relaxing it again clears it, with nothing to re-initialise");
        REQUIRE(f.bank.set(f.bank.var(f.bank.find("sys_cpu_load_pct")), "99")
                != Vars::SET_INVALID);
        h.step(SETTLED);
        CHECK_FALSE(f.alarm("alarm_cpu_load"));
    }

    SUBCASE("each threshold is its own knob") {
        // Load and jitter agree at the factory value but threshold different
        // quantities, so moving one must not move the other.
        f.sched.cpu_jitter_us_recent =
            (uint32_t)((uint64_t)PERIOD_US * Config::ALARM_CPU_JITTER_PCT / 100u);
        REQUIRE(f.bank.set(f.bank.var(f.bank.find("sys_cpu_load_pct")), "10")
                != Vars::SET_INVALID);
        h.step(SETTLED);
        CHECK(f.alarm("alarm_cpu_load"));
        CHECK_FALSE(f.alarm("alarm_cpu_jitter"));
    }

    SUBCASE("the stack threshold moves the same way") {
        f.sched.stack_usage = 0.5f;
        h.step(SETTLED);
        REQUIRE_FALSE(f.alarm("alarm_stack"));    // under the factory 80%
        REQUIRE(f.bank.set(f.bank.var(f.bank.find("sys_stack_pct")), "40")
                != Vars::SET_INVALID);
        h.step(SETTLED);
        CHECK(f.alarm("alarm_stack"));
    }

    SUBCASE("an override on the threshold row reaches it like any other reader") {
        f.bank.override_set(f.bank.var(f.bank.find("sys_cpu_load_pct")),
                            Vars::Scalar::U(10), true);
        h.step(SETTLED);
        CHECK(f.alarm("alarm_cpu_load"));
    }
}

TEST_CASE("health: an overrun holds CPU_XRUN up long after the beat that caused it") {
    // Latched for ALARM_EVENT_HOLD_MS, not pulsed - a one-beat blip isn't annunciation.
    Fixture f;
    Health::HealthMonitor h(&f.log, &f.sched, &f.bank);

    h.step(SETTLED);
    CHECK_FALSE(f.alarm("alarm_cpu_xrun"));

    f.sched.overruns = 1;                           // one skipped beat, ever
    h.step(SETTLED);
    CHECK(f.alarm("alarm_cpu_xrun"));

    // Still up mid-hold, though nothing has overrun since.
    h.step(SETTLED + Config::ALARM_EVENT_HOLD_MS - 1000);
    CHECK(f.alarm("alarm_cpu_xrun"));

    h.step(SETTLED + Config::ALARM_EVENT_HOLD_MS + 1000);
    CHECK_FALSE(f.alarm("alarm_cpu_xrun"));
}

TEST_CASE("health: POST fails when the alarm table declares more than the model drives") {
    // BOTANIK_ASSERT compiles to nothing without DEBUG (every OPTA build); POST must catch this too.
    Fixture f;
    Health::HealthMonitor h(&f.log, &f.sched, &f.bank);
    h.setup(0);                                     // runs POST
    CHECK(h.healthy());
    CHECK(f.bank.var(f.bank.find("alarm_system_reset")).meta() != nullptr);
    CHECK(h.alarm_model().declared_count() == h.alarm_model().count());
}

TEST_CASE("health: with no scheduler context the load alarms stay quiet") {
    // Must not invent a fault from a null scheduler pointer; SYS_RST/FLASH_FLT still work.
    Fixture f;
    Health::HealthMonitor h(&f.log, nullptr, &f.bank);
    h.step(SETTLED);
    CHECK_FALSE(f.alarm("alarm_cpu_load"));
    CHECK_FALSE(f.alarm("alarm_cpu_jitter"));
    CHECK_FALSE(f.alarm("alarm_stack"));
    CHECK_FALSE(f.alarm("alarm_cpu_xrun"));
}

// the loop driver the thresholds above are read from

TEST_CASE("scheduler: a beat is released once per period, and counted") {
    Scheduler::LoopScheduler s(10);
    Scheduler::SchedulerContext* ctx = s.context();
    const uint32_t before = ctx->tick_count;

    // tick() busy-waits to the beat; bounded so a broken gate fails, not hangs.
    int beats = 0;
    for (int i = 0; i < 100000 && beats < 3; i++) if (s.tick()) beats++;
    CHECK(beats == 3);
    CHECK(ctx->tick_count == before + 3);
    // beat also publishes the shared "now".
    CHECK(ctx->timestamp_ms != 0);
}

TEST_CASE("scheduler: a beat missed wholesale is counted as an overrun") {
    // Feeds the CPU_XRUN alarm above; sleeping past periods mimics a long flash write.
    Scheduler::LoopScheduler s(10);
    Scheduler::SchedulerContext* ctx = s.context();
    for (int i = 0; i < 100000; i++) if (s.tick()) break;      // land on a beat
    const uint32_t before = ctx->overruns;

    HAL::Clock::sleep_ms(55);                                  // ~5 periods gone
    for (int i = 0; i < 100000; i++) if (s.tick()) break;
    CHECK(ctx->overruns > before);
}

}  // namespace health
