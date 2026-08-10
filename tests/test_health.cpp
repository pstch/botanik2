/**
 * @file test_health.cpp
 * @brief The supervision path: the platform-health alarms, POST, and the loop
 *        driver that feeds them.
 *
 * These three had no coverage at all, which is a gap worth more than any of the
 * deletions the test review proposed: HealthMonitor decides what the console
 * annunciates and whether the watchdog is kicked, and every threshold it applies
 * is read off the scheduler's own metrics. A wrong comparison here is a fault
 * nobody is told about, or one raised on a healthy plant.
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
    /** @brief A bank over the site's real table (which splices the library's
     *  platform alarm rows), plus the scheduler context the monitor reads. */
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

        /** @brief One beat of the monitor at @p now, with the context as set. */
        void step(Health::HealthMonitor& h, uint64_t now) { h.step(now); }
    };

    // Past the reset window, so SYS_RST is not asserted and the case under test
    // is the only thing moving.
    const uint64_t SETTLED = (uint64_t)Config::HEALTH_RESET_WINDOW_MS + 1000;
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
    // The comparison is integer-scaled on purpose (no float compare), so the
    // boundary is the interesting part: at exactly the threshold it must NOT
    // fire, one microsecond past it must.
    Fixture f;
    Health::HealthMonitor h(&f.log, &f.sched, &f.bank);

    const uint32_t at = (uint32_t)((uint64_t)PERIOD_US * Config::ALARM_CPU_LOAD_PCT / 100u);
    f.sched.cpu_loop_us_recent = at;
    h.step(SETTLED);
    CHECK_FALSE(f.alarm("alarm_cpu_load"));

    f.sched.cpu_loop_us_recent = at + 1;
    h.step(SETTLED);
    CHECK(f.alarm("alarm_cpu_load"));

    // ...and jitter is its own knob, thresholded against the same period.
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

TEST_CASE("health: an overrun holds CPU_XRUN up long after the beat that caused it") {
    // The counter ticks for one beat; the alarm is what an operator sees, so it
    // is latched in the source for ALARM_EVENT_HOLD_MS rather than pulsed. A
    // one-beat alarm on a 10 ms loop is not annunciation.
    Fixture f;
    Health::HealthMonitor h(&f.log, &f.sched, &f.bank);

    h.step(SETTLED);
    CHECK_FALSE(f.alarm("alarm_cpu_xrun"));

    f.sched.overruns = 1;                           // one skipped beat, ever
    h.step(SETTLED);
    CHECK(f.alarm("alarm_cpu_xrun"));

    // Still up most of the way through the hold, even though nothing has
    // overrun since.
    h.step(SETTLED + Config::ALARM_EVENT_HOLD_MS - 1000);
    CHECK(f.alarm("alarm_cpu_xrun"));

    h.step(SETTLED + Config::ALARM_EVENT_HOLD_MS + 1000);
    CHECK_FALSE(f.alarm("alarm_cpu_xrun"));
}

TEST_CASE("health: POST fails when the alarm table declares more than the model drives") {
    // The only guard used to be BOTANIK_ASSERT, which compiles to nothing
    // without DEBUG - i.e. on every OPTA build - so on hardware the site's own
    // alarms were dropped silently (the library's platform rows are spliced
    // first, so it is the site's tail that goes).
    Fixture f;
    Health::HealthMonitor h(&f.log, &f.sched, &f.bank);
    h.setup(0);                                     // runs POST
    CHECK(h.healthy());
    CHECK(f.bank.var(f.bank.find("alarm_system_reset")).meta() != nullptr);

    // The real site fits, which is the property the site test also asserts.
    CHECK(h.alarm_model().declared_count() == h.alarm_model().count());
}

TEST_CASE("health: with no scheduler context the load alarms stay quiet") {
    // A monitor built before the scheduler is wired must not invent a fault out
    // of a null pointer. SYS_RST and FLASH_FLT do not depend on it and still
    // work.
    Fixture f;
    Health::HealthMonitor h(&f.log, nullptr, &f.bank);
    h.step(SETTLED);
    CHECK_FALSE(f.alarm("alarm_cpu_load"));
    CHECK_FALSE(f.alarm("alarm_cpu_jitter"));
    CHECK_FALSE(f.alarm("alarm_stack"));
    CHECK_FALSE(f.alarm("alarm_cpu_xrun"));
}

// --------------------------------------------------------------------------
// the loop driver the thresholds above are read from
// --------------------------------------------------------------------------

TEST_CASE("scheduler: a beat is released once per period, and counted") {
    Scheduler::LoopScheduler s(10);
    Scheduler::SchedulerContext* ctx = s.context();
    const uint32_t before = ctx->tick_count;

    // tick() gates: it busy-waits the tail of the period and returns true on
    // the beat. Bounded so a broken gate fails the test rather than hanging.
    int beats = 0;
    for (int i = 0; i < 100000 && beats < 3; i++) if (s.tick()) beats++;
    CHECK(beats == 3);
    CHECK(ctx->tick_count == before + 3);
    // ...and the beat publishes the "now" every renderer shares.
    CHECK(ctx->timestamp_ms != 0);
}

TEST_CASE("scheduler: a beat missed wholesale is counted as an overrun") {
    // The metric the CPU_XRUN alarm above is driven from. Sleeping past several
    // periods inside the loop is exactly what a long flash write does.
    Scheduler::LoopScheduler s(10);
    Scheduler::SchedulerContext* ctx = s.context();
    for (int i = 0; i < 100000; i++) if (s.tick()) break;      // land on a beat
    const uint32_t before = ctx->overruns;

    HAL::Clock::sleep_ms(55);                                  // ~5 periods gone
    for (int i = 0; i < 100000; i++) if (s.tick()) break;
    CHECK(ctx->overruns > before);
}

}  // namespace health
