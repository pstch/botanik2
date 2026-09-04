/**
 * @file test_health.cpp
 * @brief Platform-health alarms, POST, and the loop driver that feeds them.
 *
 * Wrong thresholds mean a silent fault, or a false one on a healthy plant.
 */

#include <string>

#include "doctest.h"

#include "libbotanik/health/health.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/vars/model.h"
#include "recording_writer.h"
#include "sites/gh_mlb41/site.h"
#include "site_bank.h"

namespace health {

using namespace Botanik;

namespace {
    /** @brief Bank over the site's real table, plus the scheduler context the monitor reads. */
    struct Fixture {
        Vars::Slot*                vals;
        Vars::Bank                  bank;
        Logs::Logger                log;
        RecordingWriter             writer;
        Scheduler::SchedulerContext sched;

        Fixture() : vals(nullptr) {
            TestSite::build(bank);
            log.add_writer(&writer);
        }

        bool alarm(const char* name) { return bank.var(bank.find(name)).as_bool(); }

        void step(Health::HealthMonitor& h, uint64_t now) { h.step(now); }
    };

    const uint64_t SETTLED = (uint64_t)Config::HEALTH_RESET_WINDOW_MS + 1000;  // past reset, SYS_RST clear
    const uint32_t PERIOD_US = Config::TASK_PERIOD_MS * 1000u;
}

TEST_CASE("health: SYS_RST is asserted for the reset window and then clears") {
    Fixture f;
    Health::HealthMonitor h(Botanik::SetupContext(nullptr, &f.log), &f.sched, &f.bank);

    h.step(1000);                                   // early in the window
    CHECK(f.alarm("alarm_system_reset"));
    h.step(SETTLED);                                // past it
    CHECK_FALSE(f.alarm("alarm_system_reset"));
}

TEST_CASE("health: CPU load and jitter are judged against the beat, not a constant") {
    // Integer-scaled (no float): at threshold must NOT fire, one us past must.
    Fixture f;
    Health::HealthMonitor h(Botanik::SetupContext(nullptr, &f.log), &f.sched, &f.bank);

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
    Health::HealthMonitor h(Botanik::SetupContext(nullptr, &f.log), &f.sched, &f.bank);

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
    Health::HealthMonitor h(Botanik::SetupContext(nullptr, &f.log), &f.sched, &f.bank);

    // Sit exactly at the factory threshold: quiet by the "at does not fire" rule.
    f.sched.cpu_loop_us_recent =
        (uint32_t)((uint64_t)PERIOD_US * Config::ALARM_CPU_LOAD_PCT / 100u);
    h.step(SETTLED);
    REQUIRE_FALSE(f.alarm("alarm_cpu_load"));

    SUBCASE("tightening it raises the alarm on an unchanged load") {
        REQUIRE(f.bank.var(f.bank.find("sys_cpu_load_pct")).param_set("10")
                != Vars::SET_INVALID);
        h.step(SETTLED);
        CHECK(f.alarm("alarm_cpu_load"));

        INFO("...and relaxing it again clears it, with nothing to re-initialise");
        REQUIRE(f.bank.var(f.bank.find("sys_cpu_load_pct")).param_set("99")
                != Vars::SET_INVALID);
        h.step(SETTLED);
        CHECK_FALSE(f.alarm("alarm_cpu_load"));
    }

    SUBCASE("each threshold is its own knob") {
        // Load and jitter agree at the factory value but threshold different
        // quantities, so moving one must not move the other.
        f.sched.cpu_jitter_us_recent =
            (uint32_t)((uint64_t)PERIOD_US * Config::ALARM_CPU_JITTER_PCT / 100u);
        REQUIRE(f.bank.var(f.bank.find("sys_cpu_load_pct")).param_set("10")
                != Vars::SET_INVALID);
        h.step(SETTLED);
        CHECK(f.alarm("alarm_cpu_load"));
        CHECK_FALSE(f.alarm("alarm_cpu_jitter"));
    }

    SUBCASE("the stack threshold moves the same way") {
        f.sched.stack_usage = 0.5f;
        h.step(SETTLED);
        REQUIRE_FALSE(f.alarm("alarm_stack"));    // under the factory 80%
        REQUIRE(f.bank.var(f.bank.find("sys_stack_pct")).param_set("40")
                != Vars::SET_INVALID);
        h.step(SETTLED);
        CHECK(f.alarm("alarm_stack"));
    }

    SUBCASE("an override on the threshold row reaches it like any other reader") {
        f.bank.var(f.bank.find("sys_cpu_load_pct")).override_set(Vars::Scalar::U(10));
        h.step(SETTLED);
        CHECK(f.alarm("alarm_cpu_load"));
    }
}

TEST_CASE("health: an overrun pulses CPU_XRUN, and the row's own hold_s latches it") {
    // Overruns are a COUNTER, so health writes a one-beat pulse on each tick and
    // the 300 s latch is the CPU_XRUN row's hold_s, applied by the model like
    // any other row. Both halves are asserted here: the pulse alone would be a
    // blip nobody sees, and the hold alone would never be armed.
    static const uint64_t HOLD_MS = 300u * 1000u;   // the CPU_XRUN row's hold_s
    Fixture f;
    Health::HealthMonitor h(Botanik::SetupContext(nullptr, &f.log), &f.sched, &f.bank);
    Alarms::AlarmController& m = h.alarm_model();
    h.setup(0);          // binds the alarm source, then sets the model up
    const int xr = m.index_of("CPU_XRUN");
    REQUIRE(xr >= 0);

    h.step(SETTLED);
    m.step(SETTLED);
    CHECK_FALSE(m.condition((uint8_t)xr).active);

    f.sched.overruns = 1;                           // one skipped beat, ever
    h.step(SETTLED);
    m.step(SETTLED);
    CHECK(f.alarm("alarm_cpu_xrun"));               // the raw pulse
    CHECK(m.condition((uint8_t)xr).active);

    // The pulse is over on the very next beat; the model keeps it up regardless.
    h.step(SETTLED + 1000);
    m.step(SETTLED + 1000);
    CHECK_FALSE(f.alarm("alarm_cpu_xrun"));
    CHECK(m.condition((uint8_t)xr).active);

    m.step(SETTLED + HOLD_MS - 1000);               // still inside the hold
    CHECK(m.condition((uint8_t)xr).active);

    m.step(SETTLED + HOLD_MS + 2000);               // past it
    CHECK_FALSE(m.condition((uint8_t)xr).active);
}

TEST_CASE("health: an ending latches with no bank to save into") {
    // The ending is the LIFECYCLE's, not the table's. Latching it inside the
    // bank-dependent half meant `halt` and `reboot` were accepted, latched in
    // _stop, and never happened: _stopped stayed false, so halted() was false
    // for ever and the watchdog was never withheld. Driven with a null bank,
    // which is the reachable shape of that (and the whole shape of it in a
    // build with no variable model).
    Fixture f;
    Botanik::Tasks::Registry reg;
    Health::HealthMonitor h(Botanik::SetupContext(reg, &f.log), &f.sched, nullptr);
    h.register_tasks();
    reg.setup(0);

    uint64_t t = 0;
    SUBCASE("halt stops the cycle and keeps kicking") {
        h.request_stop(Health::HealthMonitor::Stop::HALT);
        CHECK_FALSE(h.stopped());                   // not until the beat
        for (int i = 0; i < 4; i++) reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t += 100);
        CHECK(h.stopped());
        CHECK(h.halted());
        INFO("HALT keeps kicking: the loop is alive, doing nothing");
        CHECK(h.watchdog_ok());
    }
    SUBCASE("reboot withholds the kick") {
        h.request_stop(Health::HealthMonitor::Stop::REBOOT);
        for (int i = 0; i < 4; i++) reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t += 100);
        CHECK(h.stopped());
        CHECK_FALSE(h.halted());
        INFO("the withheld kick IS the reboot");
        CHECK_FALSE(h.watchdog_ok());
    }
}

TEST_CASE("health: the first ending asked for is the one that happens") {
    Fixture f;
    Botanik::Tasks::Registry reg;
    Health::HealthMonitor h(Botanik::SetupContext(reg, &f.log), &f.sched, &f.bank);
    h.register_tasks();
    reg.setup(0);

    // request_stop latches: a halt that arrives after a reboot was asked for
    // must not turn it into a halt, or the unit sits there instead of coming
    // back.
    h.request_reboot();
    h.request_stop(Health::HealthMonitor::Stop::HALT);
    uint64_t t = 0;
    for (int i = 0; i < 4; i++) reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t += 100);
    CHECK(h.reboot_requested());
    CHECK_FALSE(h.halted());
}

TEST_CASE("health: POST fails when the alarm table declares more than the model drives") {
    // BOTANIK_ASSERT compiles to nothing without DEBUG (every OPTA build); POST must catch this too.
    Fixture f;
    Health::HealthMonitor h(Botanik::SetupContext(nullptr, &f.log), &f.sched, &f.bank);
    h.setup(0);                                     // runs POST
    CHECK(h.healthy());
    CHECK(!f.bank.var(f.bank.find("alarm_system_reset")).null());
    CHECK(h.alarm_model().declared_count() == h.alarm_model().count());
}

TEST_CASE("health: with no scheduler context the load alarms stay quiet") {
    // Must not invent a fault from a null scheduler pointer; SYS_RST/FLASH_FLT still work.
    Fixture f;
    Health::HealthMonitor h(Botanik::SetupContext(nullptr, &f.log), nullptr, &f.bank);
    h.step(SETTLED);
    CHECK_FALSE(f.alarm("alarm_cpu_load"));
    CHECK_FALSE(f.alarm("alarm_cpu_jitter"));
    CHECK_FALSE(f.alarm("alarm_stack"));
    CHECK_FALSE(f.alarm("alarm_cpu_xrun"));
}

// the loop driver the thresholds above are read from

TEST_CASE("scheduler: a beat is released once per period, and counted") {
    Tasks::Registry reg;                 // borrowed: the driver no longer owns it
    Scheduler::LoopScheduler s(reg, 10);
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
    Tasks::Registry reg;                 // borrowed: the driver no longer owns it
    Scheduler::LoopScheduler s(reg, 10);
    Scheduler::SchedulerContext* ctx = s.context();
    for (int i = 0; i < 100000; i++) if (s.tick()) break;      // land on a beat
    const uint32_t before = ctx->overruns;

    HAL::Clock::sleep_ms(55);                                  // ~5 periods gone
    for (int i = 0; i < 100000; i++) if (s.tick()) break;
    CHECK(ctx->overruns > before);
}


TEST_CASE("quality: a row losing its validity is announced once, not per beat") {
    // A row going invalid is how a dead instrument announces itself, and only
    // the rows carrying a timeout alarm say so otherwise.
    Fixture f;
    Botanik::Tasks::Registry reg;
    Health::HealthMonitor h(Botanik::SetupContext(reg, &f.log), &f.sched, &f.bank);
    h.register_tasks();
    reg.setup(0);

    // Enough beats to cross the whole table, so the sweep is seeded and the
    // first pass has said nothing.
    uint64_t t = SETTLED;
    for (uint16_t i = 0; i < 40; i++) reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t += 100);

    const uint16_t r = f.bank.find("cli_temp_hi1");
    REQUIRE(r != Vars::VAR_NONE);
    f.bank.var(r).publish(Vars::from_f32(Vars::T_F32, 21.0f), t);
    for (uint16_t i = 0; i < 40; i++) reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t += 100);
    f.writer.all_msgs.clear();

    f.bank.var(r).set_quality(Vars::Q_NONE);
    for (uint16_t i = 0; i < 40; i++) reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t += 100);

    int said = 0;
    for (size_t at = f.writer.all_msgs.find("cli_temp_hi1");
         at != std::string::npos;
         at = f.writer.all_msgs.find("cli_temp_hi1", at + 1)) said++;
    INFO("the CHANGE is the event; a row that is invalid for an hour is one line");
    CHECK(said == 1);
    CHECK(f.writer.saw("cli_temp_hi1: invalid"));
    INFO("and it is a WARNING: something the plant is controlled on has stopped "
         "being measured, and an operator should not need debug on to see it");
    CHECK(f.writer.last_level == Logs::LOG_WARNING);

    INFO("...where coming back is the record closing, at DEBUG");
    f.writer.all_msgs.clear();
    f.bank.var(r).publish(Vars::from_f32(Vars::T_F32, 21.0f), t);
    for (uint16_t i = 0; i < 40; i++) reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t += 100);
    CHECK(f.writer.saw("cli_temp_hi1: valid"));
    CHECK(f.writer.last_level == Logs::LOG_DEBUG);
}

TEST_CASE("quality: bring-up is adopted in silence, not announced row by row") {
    // Every field row goes valid as its device first answers, and every program
    // leaves INHIBITED on its first activation. Announcing all of it is several
    // hundred lines in one breath, which the rate limiter then sheds along with
    // whatever else was being said.
    Fixture f;
    Botanik::Tasks::Registry reg;
    Health::HealthMonitor h(Botanik::SetupContext(reg, &f.log), &f.sched, &f.bank);
    h.register_tasks();
    reg.setup(0);

    const uint16_t r = f.bank.find("cli_temp_hi1");
    REQUIRE(r != Vars::VAR_NONE);

    // Inside the quiet window: the table is swept, the row goes valid, and
    // nothing is said about it.
    uint64_t t = 0;
    for (uint16_t i = 0; i < 40; i++) reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t += 100);
    f.bank.var(r).publish(Vars::from_f32(Vars::T_F32, 21.0f), t);
    for (uint16_t i = 0; i < 40; i++) reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t += 100);
    REQUIRE(t < Config::HEALTH_QUIET_MS);
    CHECK_FALSE(f.writer.saw("cli_temp_hi1"));

    // ...and the row was ADOPTED rather than merely skipped, so the next change
    // is the one that speaks.
    t = (uint64_t)Config::HEALTH_QUIET_MS + 1000;
    f.bank.var(r).set_quality(Vars::Q_NONE);
    for (uint16_t i = 0; i < 40; i++) reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t += 100);
    CHECK(f.writer.saw("cli_temp_hi1: invalid"));
}



}  // namespace health
