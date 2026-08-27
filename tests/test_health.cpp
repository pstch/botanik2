/**
 * @file test_health.cpp
 * @brief Platform-health alarms, POST, and the loop driver that feeds them.
 *
 * Wrong thresholds mean a silent fault, or a false one on a healthy plant.
 */

#include <string>
#include "libbotanik/plc/authority.h"

#include "doctest.h"

#include "libbotanik/health/health.h"
#include "libbotanik/plc/program.h"
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
        Botanik::Plc::ControlAuthority auth{bank};
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
    h.set_authority(&f.auth);

    h.step(1000);                                   // early in the window
    CHECK(f.alarm("alarm_system_reset"));
    h.step(SETTLED);                                // past it
    CHECK_FALSE(f.alarm("alarm_system_reset"));
}

TEST_CASE("health: CPU load and jitter are judged against the beat, not a constant") {
    // Integer-scaled (no float): at threshold must NOT fire, one us past must.
    Fixture f;
    Health::HealthMonitor h(Botanik::SetupContext(nullptr, &f.log), &f.sched, &f.bank);
    h.set_authority(&f.auth);

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
    h.set_authority(&f.auth);

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
    h.set_authority(&f.auth);

    // Sit exactly at the factory threshold: quiet by the "at does not fire" rule.
    f.sched.cpu_loop_us_recent =
        (uint32_t)((uint64_t)PERIOD_US * Config::ALARM_CPU_LOAD_PCT / 100u);
    h.step(SETTLED);
    REQUIRE_FALSE(f.alarm("alarm_cpu_load"));

    SUBCASE("tightening it raises the alarm on an unchanged load") {
        REQUIRE(f.bank.param_set(f.bank.var(f.bank.find("sys_cpu_load_pct")), "10")
                != Vars::SET_INVALID);
        h.step(SETTLED);
        CHECK(f.alarm("alarm_cpu_load"));

        INFO("...and relaxing it again clears it, with nothing to re-initialise");
        REQUIRE(f.bank.param_set(f.bank.var(f.bank.find("sys_cpu_load_pct")), "99")
                != Vars::SET_INVALID);
        h.step(SETTLED);
        CHECK_FALSE(f.alarm("alarm_cpu_load"));
    }

    SUBCASE("each threshold is its own knob") {
        // Load and jitter agree at the factory value but threshold different
        // quantities, so moving one must not move the other.
        f.sched.cpu_jitter_us_recent =
            (uint32_t)((uint64_t)PERIOD_US * Config::ALARM_CPU_JITTER_PCT / 100u);
        REQUIRE(f.bank.param_set(f.bank.var(f.bank.find("sys_cpu_load_pct")), "10")
                != Vars::SET_INVALID);
        h.step(SETTLED);
        CHECK(f.alarm("alarm_cpu_load"));
        CHECK_FALSE(f.alarm("alarm_cpu_jitter"));
    }

    SUBCASE("the stack threshold moves the same way") {
        f.sched.stack_usage = 0.5f;
        h.step(SETTLED);
        REQUIRE_FALSE(f.alarm("alarm_stack"));    // under the factory 80%
        REQUIRE(f.bank.param_set(f.bank.var(f.bank.find("sys_stack_pct")), "40")
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

TEST_CASE("health: an overrun pulses CPU_XRUN, and the row's own hold_s latches it") {
    // Overruns are a COUNTER, so health writes a one-beat pulse on each tick and
    // the 300 s latch is the CPU_XRUN row's hold_s, applied by the model like
    // any other row. Both halves are asserted here: the pulse alone would be a
    // blip nobody sees, and the hold alone would never be armed.
    static const uint64_t HOLD_MS = 300u * 1000u;   // the CPU_XRUN row's hold_s
    Fixture f;
    Health::HealthMonitor h(Botanik::SetupContext(nullptr, &f.log), &f.sched, &f.bank);
    h.set_authority(&f.auth);
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

TEST_CASE("health: POST fails when the alarm table declares more than the model drives") {
    // BOTANIK_ASSERT compiles to nothing without DEBUG (every OPTA build); POST must catch this too.
    Fixture f;
    Health::HealthMonitor h(Botanik::SetupContext(nullptr, &f.log), &f.sched, &f.bank);
    h.set_authority(&f.auth);
    h.setup(0);                                     // runs POST
    CHECK(h.healthy());
    CHECK(f.bank.var(f.bank.find("alarm_system_reset")).meta() != nullptr);
    CHECK(h.alarm_model().declared_count() == h.alarm_model().count());
}

TEST_CASE("health: with no scheduler context the load alarms stay quiet") {
    // Must not invent a fault from a null scheduler pointer; SYS_RST/FLASH_FLT still work.
    Fixture f;
    Health::HealthMonitor h(Botanik::SetupContext(nullptr, &f.log), nullptr, &f.bank);
    h.set_authority(&f.auth);
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
    h.set_authority(&f.auth);
    h.register_tasks();
    reg.setup(0);

    // Enough beats to cross the whole table, so the sweep is seeded and the
    // first pass has said nothing.
    uint64_t t = SETTLED;
    for (uint16_t i = 0; i < 40; i++) reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t += 100);

    const Vars::VarRef r = f.bank.find("cli_temp_hi1");
    REQUIRE(r.index != Vars::VAR_NONE);
    f.bank.var(r).publish(Vars::from_f32(Vars::T_F32, 21.0f), t);
    for (uint16_t i = 0; i < 40; i++) reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t += 100);
    f.writer.all_msgs.clear();

    f.bank.var(r).invalidate(Vars::Q_NO_DATA);
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
    h.set_authority(&f.auth);
    h.register_tasks();
    reg.setup(0);

    const Vars::VarRef r = f.bank.find("cli_temp_hi1");
    REQUIRE(r.index != Vars::VAR_NONE);

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
    f.bank.var(r).invalidate(Vars::Q_NO_DATA);
    for (uint16_t i = 0; i < 40; i++) reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t += 100);
    CHECK(f.writer.saw("cli_temp_hi1: invalid"));
}

namespace {
    /** @brief A program that takes @c travel activations to reach its safe
     *  state, so a case can watch a supervisor wait for it. */
    class SlowToPark : public Botanik::Plc::Program {
    public:
        uint16_t travel;
        uint16_t parked_for = 0;

        SlowToPark(const Botanik::SetupContext& ctx, Vars::Bank& io,
                   uint16_t t, const char* name = "slow")
            : Program(ctx, name, Botanik::Tasks::GROUP_FAST, io), travel(t) {}

    protected:
        void run(Botanik::ProgramContext&) override {
            if (!inhibit_requested()) { parked_for = 0; return; }
            if (parked_for < travel) { parked_for++; parking(); }
        }
    };

    /** @brief Beat the registry the way the firmware does. */
    void beat(Botanik::Tasks::Registry& reg, Health::HealthMonitor& h,
              uint64_t& t, uint16_t n) {
        for (uint16_t i = 0; i < n; i++) {
            t += Config::TASK_PERIOD_MS;
            reg.run_phase(Botanik::Tasks::PHASE_SERVICE, t);
            reg.run_phase(Botanik::Tasks::PHASE_CONTROL, t);
            h.step(t);
        }
    }
}

TEST_CASE("stop: a reboot parks the plant BEFORE it stops kicking") {
    Fixture f;
    Botanik::Tasks::Registry reg;
    Health::HealthMonitor h(Botanik::SetupContext(reg, &f.log), &f.sched, &f.bank);
    h.set_authority(&f.auth);
    SlowToPark slow(Botanik::SetupContext(reg, &f.log), f.bank, 50);
    slow.set_slot(0);
    slow.register_tasks();
    h.register_tasks();
    reg.setup(0);
    f.auth.set_health_hold(false);

    uint64_t t = SETTLED;
    beat(reg, h, t, 5);
    REQUIRE(h.watchdog_ok());

    h.request_reboot();
    beat(reg, h, t, 10);
    INFO("still travelling: withholding the kick now would reset the unit "
         "mid-stroke, which is the one thing the parking exists to avoid");
    CHECK(h.watchdog_ok());
    CHECK_FALSE(h.stopped());

    beat(reg, h, t, 60);
    CHECK(h.stopped());
    CHECK_FALSE(h.watchdog_ok());
    INFO("and the field is at its declared safe state before the reset window "
         "opens, not after it");
    CHECK(f.auth.health_hold());
}

TEST_CASE("stop: a program that never parks does not hold the unit for ever") {
    Fixture f;
    Botanik::Tasks::Registry reg;
    Health::HealthMonitor h(Botanik::SetupContext(reg, &f.log), &f.sched, &f.bank);
    h.set_authority(&f.auth);
    // Longer than the deadline: this one is never going to arrive.
    SlowToPark stuck(Botanik::SetupContext(reg, &f.log), f.bank, 0xFFFF);
    stuck.set_slot(0);
    stuck.register_tasks();
    h.register_tasks();
    reg.setup(0);
    f.auth.set_health_hold(false);

    uint64_t t = SETTLED;
    beat(reg, h, t, 5);
    h.request_reboot();

    const uint16_t deadline_beats =
        (uint16_t)(Config::STOP_PARK_MS / Config::TASK_PERIOD_MS);
    beat(reg, h, t, (uint16_t)(deadline_beats - 20));
    CHECK_FALSE(h.stopped());

    beat(reg, h, t, 40);
    INFO("an operator who typed `reboot` is owed one");
    CHECK(h.stopped());
    CHECK_FALSE(h.watchdog_ok());
    CHECK(f.writer.saw("never parked"));
}

TEST_CASE("stop: a halt keeps kicking, because the loop IS alive") {
    Fixture f;
    Botanik::Tasks::Registry reg;
    Health::HealthMonitor h(Botanik::SetupContext(reg, &f.log), &f.sched, &f.bank);
    h.set_authority(&f.auth);
    SlowToPark slow(Botanik::SetupContext(reg, &f.log), f.bank, 10);
    slow.set_slot(0);
    slow.register_tasks();
    h.register_tasks();
    reg.setup(0);
    f.auth.set_health_hold(false);

    uint64_t t = SETTLED;
    beat(reg, h, t, 5);
    h.request_stop(Health::HealthMonitor::Stop::HALT);
    beat(reg, h, t, 40);

    CHECK(h.stopped());
    CHECK(h.halted());
    INFO("the watchdog says the loop is alive, and it is - what stops is the "
         "beat, which the firmware skips while this holds");
    CHECK(h.watchdog_ok());
    CHECK(f.auth.health_hold());
}

TEST_CASE("health: a program on its way to safe reads PARKING, not HEALTHY") {
    Fixture f;
    Botanik::Tasks::Registry reg;
    Health::HealthMonitor h(Botanik::SetupContext(reg, &f.log), &f.sched, &f.bank);
    h.set_authority(&f.auth);
    SlowToPark slow(Botanik::SetupContext(reg, &f.log), f.bank, 10);
    slow.set_slot(0);
    slow.register_tasks();
    h.register_tasks();
    reg.setup(0);
    f.auth.set_health_hold(false);

    uint64_t t = SETTLED;
    beat(reg, h, t, 5);
    REQUIRE(slow.health() == Plc::Health::HEALTHY);

    reg.request_safe(true);
    beat(reg, h, t, 3);
    INFO("the actuator has not arrived, so the program has not either - and "
         "reading HEALTHY here would say the plant was still in hand");
    CHECK(slow.health() == Plc::Health::PARKING);
    CHECK_FALSE(slow.is_parked());
    CHECK(reg.unsafe_count() == 1);

    beat(reg, h, t, 20);
    CHECK(slow.health() == Plc::Health::INHIBITED);
    CHECK(slow.is_parked());

    INFO("both are NOTICE: on the OPTA the minimum level is INFO, so a plant "
         "going to its safe state at DEBUG leaves no record at all");
    CHECK(f.writer.saw("slow: PARKING"));
    CHECK(f.writer.saw("slow: INHIBITED"));
    CHECK(f.writer.last_level == Logs::LOG_NOTICE);

    INFO("...and released, it says so on the way back up");
    reg.request_safe(false);
    beat(reg, h, t, 3);
    CHECK(slow.health() == Plc::Health::HEALTHY);
    CHECK(f.writer.saw("slow: HEALTHY"));
}

namespace {
    /** @brief A program naming a row no table declares, so setup FAULTS it. */
    class Unwireable : public Botanik::Plc::Program {
    public:
        uint16_t ran = 0, observed = 0;

        Unwireable(const Botanik::SetupContext& ctx, Vars::Bank& io)
            : Program(ctx, "unwireable", Botanik::Tasks::GROUP_FAST, io) {}

    protected:
        void bind(Vars::Bank& io) override {
            (void)io.resolve_in<float>("no_such_row_anywhere");
        }
        void run(Botanik::ProgramContext&) override     { ran++; }
        void observe(Botanik::ProgramContext&) override { observed++; }
    };
}

namespace {
    /** @brief Binds one name, however the case under test asks for it. */
    class OneBinding : public Botanik::Plc::Program {
    public:
        enum How { IN, OUT, INOUT_ } how;
        const char* row;

        OneBinding(const Botanik::SetupContext& ctx, Vars::Bank& io,
                   const char* label, How h, const char* name)
            : Program(ctx, label, Botanik::Tasks::GROUP_FAST, io), how(h), row(name) {}

    protected:
        void bind(Vars::Bank& io) override {
            if (how == IN)         (void)_bind_input<float>(io, row);
            else if (how == OUT)   (void)_bind_output<float>(io, row);
            else                   (void)_bind_inout<float>(io, row);
        }
        void run(Botanik::ProgramContext&) override {}
    };

    /** @brief Set the two up, register both, and run setup once. */
    void wire_two(Botanik::Tasks::Registry& reg, Botanik::Plc::Program& a,
                  Botanik::Plc::Program& b) {
        a.set_slot(0); b.set_slot(1);
        a.register_tasks(); b.register_tasks();
        reg.setup(0);
    }
}

TEST_CASE("bind: a name no table declares is refused, and says which") {
    Fixture f;
    Botanik::Tasks::Registry reg;
    OneBinding p(Botanik::SetupContext(reg, &f.log), f.bank, "ghost",
                 OneBinding::IN, "no_such_row_anywhere");
    p.set_slot(0);
    p.register_tasks();
    reg.setup(0);

    CHECK(p.is_invalid());
    CHECK(p.health() == Plc::Health::INVALID);
    CHECK(f.writer.saw("ghost: cannot bind 'no_such_row_anywhere'"));
    CHECK(f.writer.saw("no such row"));
}

TEST_CASE("bind: a row the reference cannot carry is refused, not read sideways") {
    Fixture f;
    Botanik::Tasks::Registry reg;
    // A real row, and a boolean: a float reference over it would read the wrong
    // arm of the union rather than fail.
    REQUIRE(f.bank.find("cli_rainLockout").index != Vars::VAR_NONE);
    OneBinding p(Botanik::SetupContext(reg, &f.log), f.bank, "mistyped",
                 OneBinding::IN, "cli_rainLockout");
    p.set_slot(0);
    p.register_tasks();
    reg.setup(0);

    CHECK(p.is_invalid());
    CHECK(f.writer.saw("mistyped: cannot bind 'cli_rainLockout'"));
    CHECK(f.writer.saw("wrong type"));
}

TEST_CASE("bind: an output another program already drives is refused, and names it") {
    Fixture f;
    Botanik::Tasks::Registry reg;
    const char* row = "cli_dTStrat_degC";
    REQUIRE(f.bank.find(row).index != Vars::VAR_NONE);
    OneBinding first(Botanik::SetupContext(reg, &f.log), f.bank, "first",
                     OneBinding::OUT, row);
    OneBinding second(Botanik::SetupContext(reg, &f.log), f.bank, "second",
                      OneBinding::OUT, row);
    wire_two(reg, first, second);

    INFO("first claim wins: the loser is the one that cannot be wired");
    CHECK_FALSE(first.is_invalid());
    CHECK(second.is_invalid());
    CHECK(f.writer.saw("second: cannot bind 'cli_dTStrat_degC'"));
    CHECK(f.writer.saw("already driven, by 'first'"));
}

TEST_CASE("bind: VAR_IN_OUT is shared by declaration, so two of them is not a fault") {
    Fixture f;
    Botanik::Tasks::Registry reg;
    const char* row = "cli_dTStrat_degC";
    OneBinding a(Botanik::SetupContext(reg, &f.log), f.bank, "a",
                 OneBinding::INOUT_, row);
    OneBinding b(Botanik::SetupContext(reg, &f.log), f.bank, "b",
                 OneBinding::INOUT_, row);
    wire_two(reg, a, b);

    CHECK_FALSE(a.is_invalid());
    CHECK_FALSE(b.is_invalid());
}

TEST_CASE("bind: the rows a program reads are recorded, outputs are not") {
    Fixture f;
    Botanik::Tasks::Registry reg;
    OneBinding p(Botanik::SetupContext(reg, &f.log), f.bank, "reader",
                 OneBinding::IN, "cli_temp_hi");
    p.set_slot(0);
    p.register_tasks();
    reg.setup(0);

    CHECK_FALSE(p.is_invalid());
    REQUIRE(p.input_count() == 1);
    CHECK(p.inputs()[0].index == f.bank.find("cli_temp_hi").index);
    CHECK_FALSE(p.input_overflow());
}

TEST_CASE("health: a program setup could not wire is INVALID, and never runs") {
    Fixture f;
    Botanik::Tasks::Registry reg;
    Health::HealthMonitor h(Botanik::SetupContext(reg, &f.log), &f.sched, &f.bank);
    h.set_authority(&f.auth);
    Unwireable bad(Botanik::SetupContext(reg, &f.log), f.bank);
    bad.set_slot(0);
    bad.register_tasks();
    h.register_tasks();
    reg.setup(0);
    f.auth.set_health_hold(false);

    CHECK(bad.health() == Plc::Health::INVALID);
    CHECK(bad.is_invalid());

    uint64_t t = SETTLED;
    beat(reg, h, t, 20);
    INFO("its refs are inert sinks, so an activation would publish zeros that "
         "read exactly like readings");
    CHECK(bad.ran == 0);
    CHECK(bad.observed == 0);
    CHECK(bad.health() == Plc::Health::INVALID);

    INFO("and it counts as parked: one that will never run can never arrive, "
         "and waiting on it would hold the plant out of its reboot for ever");
    CHECK(bad.is_parked());
    CHECK(reg.all_safe());
}

TEST_CASE("health: INVALID is said even though bring-up is quiet") {
    // The quiet window exists so the first verdict of every program is adopted
    // in silence. INVALID is only ever reached in setup, so obeying it would
    // mean an unwireable program was never reported at all.
    Fixture f;
    Botanik::Tasks::Registry reg;
    Health::HealthMonitor h(Botanik::SetupContext(reg, &f.log), &f.sched, &f.bank);
    h.set_authority(&f.auth);
    Unwireable bad(Botanik::SetupContext(reg, &f.log), f.bank);
    bad.set_slot(0);
    bad.register_tasks();
    h.register_tasks();
    reg.setup(0);
    f.auth.set_health_hold(false);

    uint64_t t = 0;
    beat(reg, h, t, 20);
    REQUIRE(t < Config::HEALTH_QUIET_MS);
    CHECK(f.writer.saw("unwireable: INVALID"));
    CHECK(f.writer.last_level == Logs::LOG_CRITICAL);
}

}  // namespace health
