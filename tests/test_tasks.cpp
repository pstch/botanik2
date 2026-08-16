/**
 * @file test_tasks.cpp
 * @brief The task registry and its timing primitive.
 *
 * Two sections, each in its own namespace to avoid fixture collisions.
 */

#include <cstdint>
#include <string>
#include <vector>
#include "libbotanik/core/component.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/plc/program.h"
#include "doctest.h"
#include "libbotanik/tasks/task_timer.h"
#include "libbotanik/utils/assert.h"

// tasks

namespace tasks {

using namespace Botanik::Tasks;

namespace {
    std::vector<std::string> g_log;

    /** @brief The smallest possible component: its only work is to say it ran.
     *  Everything in the table is a Component now, so even a test that only
     *  cares about ORDER needs one. */
    struct Noted : public Botanik::Component {
        explicit Noted(const char* n) : Component(Botanik::SetupContext(), n) {}
        void ran() { g_log.push_back(name()); }
    };
    void note(Botanik::Component* c, const Tick&) { static_cast<Noted*>(c)->ran(); }

    Noted A("a"), B("b"), C("c");   // distinct owners: the name identifies the task
}

TEST_CASE("tasks: a phase runs only its own members, in registration order") {
    g_log.clear();
    Registry r;
    r.add(A, &note, "in", GROUP_FAST, PHASE_INPUT);
    r.add(B, &note, "out", GROUP_FAST, PHASE_OUTPUT);
    r.add(C, &note, "in2", GROUP_FAST, PHASE_INPUT);

    r.run_phase(PHASE_INPUT, 0);
    REQUIRE(g_log.size() == 2);
    CHECK(g_log[0] == "a");
    CHECK(g_log[1] == "c");

    r.run_phase(PHASE_OUTPUT, 0);
    REQUIRE(g_log.size() == 3);
    CHECK(g_log[2] == "b");
}

TEST_CASE("tasks: a group's period is what decides due-ness") {
    g_log.clear();
    Registry r;
    r.add(A, &note, "fast", GROUP_FAST, PHASE_SERVICE);
    r.add(B, &note, "slow", GROUP_SLOW, PHASE_SERVICE);
    r.add(C, &note, "bkg", GROUP_BACKGROUND, PHASE_SERVICE);

    for (uint64_t t = 0; t <= 1000; t += 10) r.run_phase(PHASE_SERVICE, t);

    size_t fast = 0, slow = 0, bkg = 0;
    for (size_t i = 0; i < g_log.size(); i++) {
        if (g_log[i] == "a") fast++;
        if (g_log[i] == "b") slow++;
        if (g_log[i] == "c") bkg++;
    }
    CHECK(fast == 101);            // every beat over 0..1000
    CHECK(slow >= 10);             // ~100 ms
    CHECK(slow <= 11);
    CHECK(bkg  >= 2);              // ~500 ms
    CHECK(bkg  <= 3);
}

TEST_CASE("tasks: each run is costed, and the mean is not the peak") {
    g_log.clear();
    Registry r;
    r.add(A, &note, "x", GROUP_FAST, PHASE_SERVICE);

    for (uint64_t t = 0; t < 50; t += 10) r.run_phase(PHASE_SERVICE, t);

    REQUIRE(r.count() == 1);
    const Task& t = r.task(0);
    CHECK(t.cost.runs == 5);
    CHECK(t.cost.us_max >= t.cost.us_mean());
    CHECK(std::string(t.name) == "x");
    CHECK(std::string(group_name(t.group)) == "fast");
    CHECK(std::string(phase_name(t.phase)) == "service");
}

TEST_CASE("tasks: dropped activations are counted, not caught up") {
    g_log.clear();
    Registry r;
    r.add(A, &note, "x", GROUP_FAST, PHASE_SERVICE);

    const uint64_t p = group_period_ms(GROUP_FAST);
    r.run_phase(PHASE_SERVICE, 0);              // first activation
    REQUIRE(r.count() == 1);
    CHECK(r.task(0).cost.missed == 0);

    r.run_phase(PHASE_SERVICE, 5 * p);   // runs ONCE; the four in between just count as missed
    CHECK(r.task(0).cost.runs   == 2);
    CHECK(r.task(0).cost.missed == 4);

    r.run_phase(PHASE_SERVICE, 6 * p);   // on time again: count holds, doesn't decay
    CHECK(r.task(0).cost.runs   == 3);
    CHECK(r.task(0).cost.missed == 4);
}

TEST_CASE("tasks: a full table refuses and counts, it does not drop silently") {
    // Both halves of the contract: it asserts (so a sim run cannot walk past a
    // lost registration) AND it refuses gracefully, so the field gets a
    // degraded unit rather than a dead one. This drives it on purpose.
    Registry r;
    for (uint16_t i = 0; i < Botanik::Config::MAX_TASKS; i++)
        CHECK(r.add(A, &note, "t", GROUP_FAST, PHASE_SERVICE));

    CHECK(r.count() == Botanik::Config::MAX_TASKS);
    CHECK_FALSE(r.add(A, &note, "one too many", GROUP_FAST, PHASE_SERVICE));
    CHECK(r.overflow() == 1);
}

TEST_CASE("tasks: a null dispatch is refused rather than called") {
    Registry r;
    CHECK_FALSE(r.add(A, nullptr, "nop", GROUP_FAST, PHASE_SERVICE));
    CHECK(r.count() == 0);
    r.run_phase(PHASE_SERVICE, 0);   // must not fault
}

// Lazy init on first step() would put the parameter load's 423 storage reads inside the first beat.
TEST_CASE("tasks: setup() runs before any beat, exactly once") {
    struct Spy : public Botanik::Component {
        uint32_t setups = 0, steps = 0;
        Spy() : Component(Botanik::SetupContext(), "spy") {}
        void on_setup(uint64_t) override { setups++; }
        void bump() { steps++; }
    };
    struct H {
        static void run(Botanik::Component* c, const Tick&) {
            static_cast<Spy*>(c)->bump();
        }
    };
    Spy a, b;
    Registry r;
    r.add(a, &H::run, "a", GROUP_FAST, PHASE_INPUT);
    r.add(b, &H::run, "b", GROUP_SLOW, PHASE_PRESENT);

    r.setup(0);
    CHECK(a.setups == 1);
    CHECK(b.setups == 1);
    CHECK(a.steps == 0);          // init, not a beat
    CHECK(b.steps == 0);

    r.run_phase(PHASE_INPUT, 0);   // and the setup pass does not run twice
    r.run_phase(PHASE_PRESENT, 0);
    CHECK(a.setups == 1);
    CHECK(b.setups == 1);
    CHECK(a.steps == 1);
    CHECK(b.steps == 1);
}

// Fallback the network stack's children rely on: nested components have no registration to set them up.
TEST_CASE("tasks: a component the registry never set up still self-inits") {
    struct Spy : public Botanik::Component {
        uint32_t setups = 0;
        Spy() : Component(Botanik::SetupContext(), "spy") {}
    protected:
        void on_setup(uint64_t) override { setups++; }
    };
    Spy s;
    CHECK_FALSE(s.started());
    s.step(0);
    CHECK(s.setups == 1);
    CHECK(s.started());
    s.step(10);
    CHECK(s.setups == 1);
}

TEST_CASE("tasks: setup() is not run(), and a component with nothing to do is fine") {
    g_log.clear();
    Registry r;
    r.add(A, &note, "plain", GROUP_FAST, PHASE_SERVICE);
    r.setup(0);
    CHECK(g_log.empty());          // the setup pass dispatches no handler
    r.run_phase(PHASE_SERVICE, 0);
    CHECK(g_log.size() == 1);
}

}  // namespace tasks

// task_timer

namespace task_timer {

using Botanik::Tasks::TaskTimer;

TEST_CASE("task timer: the first call is always due, at the nominal period") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;

    CHECK(t.runs() == 0);
    CHECK(t.due(0, 100, dt, missed));   // due at t=0: dt is the nominal period, not "since boot"
    CHECK(dt == 100);
    CHECK(missed == 0);
}

TEST_CASE("task timer: due() is a pure query, ran() is what commits") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;

    CHECK(t.due(0, 100, dt, missed));   // asking repeatedly must not consume the activation
    CHECK(t.due(0, 100, dt, missed));
    CHECK(t.runs() == 0);

    t.ran(0);
    CHECK(t.runs() == 1);
    CHECK(t.last_run_ms() == 0);
    CHECK_FALSE(t.due(50, 100, dt, missed));   // now it holds off
}

TEST_CASE("task timer: not due until the period has fully elapsed") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;
    t.due(0, 100, dt, missed); t.ran(0);

    CHECK_FALSE(t.due(99, 100, dt, missed));   // one ms short
    REQUIRE(t.due(100, 100, dt, missed));      // exactly on the boundary
    CHECK(dt == 100);
    CHECK(missed == 0);
}

TEST_CASE("task timer: period 0 is due on every beat") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;

    for (uint64_t now = 0; now < 50; now += 10) {
        REQUIRE(t.due(now, 0, dt, missed));
        t.ran(now);
    }
    CHECK(t.runs() == 5);
    CHECK(dt == 10);        // real elapsed, even with no declared period
    CHECK(missed == 0);
}

TEST_CASE("task timer: a late run reports the real gap and the dropped count") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;
    t.due(0, 100, dt, missed); t.ran(0);

    REQUIRE(t.due(1000, 100, dt, missed));   // 900 ms stall on a 100 ms task: nine went past
    CHECK(dt == 1000);      // the REAL gap, not the nominal 100
    CHECK(missed == 9);
}

TEST_CASE("task timer: one period late is late, not missed") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;
    t.due(0, 100, dt, missed); t.ran(0);

    REQUIRE(t.due(199, 100, dt, missed));   // one activation, served late; nothing skipped
    CHECK(dt == 199);
    CHECK(missed == 0);
}

TEST_CASE("task timer: the missed count saturates instead of wrapping") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;
    t.due(0, 1, dt, missed); t.ran(0);

    REQUIRE(t.due(100000000ull, 1, dt, missed));   // 100 M activations, far past what uint16_t holds
    CHECK(missed == 0xFFFF);
}

TEST_CASE("task timer: an offset delays the first interval and only that one") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;
    t.set_offset(30);
    REQUIRE(t.due(0, 100, dt, missed));   // first activation is never delayed
    t.ran(0);

    CHECK_FALSE(t.due(100, 100, dt, missed));   // held off by the offset
    REQUIRE(t.due(130, 100, dt, missed));
    t.ran(130);

    CHECK_FALSE(t.due(229, 100, dt, missed));   // offset spent: rate is nominal from here, not 130
    CHECK(t.due(230, 100, dt, missed));
}

// What the offset exists for: same period, different offsets spreads work over beats.
TEST_CASE("task timer: offset timers of one period never share a beat again") {
    TaskTimer a, b;
    uint32_t dt = 0; uint16_t missed = 0;
    a.set_offset(0);
    b.set_offset(40);

    int shared = 0, a_runs = 0, b_runs = 0;
    for (uint64_t now = 0; now <= 2000; now += 10) {
        bool ra = a.due(now, 100, dt, missed); if (ra) a.ran(now);
        bool rb = b.due(now, 100, dt, missed); if (rb) b.ran(now);
        if (ra && rb && now > 0) shared++;
        if (ra) a_runs++;
        if (rb) b_runs++;
    }

    CHECK(shared == 0);            // never coincide after boot
    CHECK(a_runs == 21);           // 0, 100 .. 2000; same rate, not same count as b
    CHECK(b_runs == 20);           // 0, 140 .. 1940
}

TEST_CASE("task timer: dt saturates instead of truncating past 32 bits") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;
    t.due(0, 1000, dt, missed); t.ran(0);

    // Beyond a 32-bit ms span (~49 days); truncating would silently hand a fraction of the real gap.
    REQUIRE(t.due(0x1FFFFFFFFull, 1000, dt, missed));
    CHECK(dt == 0xFFFFFFFFu);
}

}  // namespace task_timer

// The registered form: a component supplies its own name, rate and phase, and a
// file-local handler that casts the owner back. Both forms coexist until the
// last legacy registration is gone (design/scan-cycle-rework.md, step 1).
namespace registered {

using namespace Botanik;
using namespace Botanik::Tasks;

namespace {
    /** @brief Derives Plc::Program rather than Component, because add_program()
     *  takes one: registering a plain component as a program is now a compile
     *  error, which is the whole point of the signature. A Program IS a
     *  Component, so the ordinary add() cases below are unaffected. */
    struct Spy : public Botanik::Plc::Program {
        uint32_t setups = 0, inputs = 0, presents = 0, programs = 0;
        uint32_t last_dt = 0;
        uint16_t last_missed = 0;
        Spy() : Program(Botanik::SetupContext(), "spy", GROUP_SLOW, _bank) {}
        Botanik::Vars::Bank _bank;   // a program needs a field; this one is inert
    protected:
        // Never reached: these cases dispatch through the spy_* handlers below,
        // and a Program with no bank returns before run() anyway.
        void run(Botanik::Plc::Context&) override {}
    };

    void spy_setup  (Component* c, const Tick&)   { static_cast<Spy*>(c)->setups++; }
    void spy_input  (Component* c, const Tick& t) {
        Spy* s = static_cast<Spy*>(c);
        s->inputs++; s->last_dt = t.dt_ms; s->last_missed = t.missed;
    }
    void spy_present(Component* c, const Tick&)   { static_cast<Spy*>(c)->presents++; }
    void spy_program(Component* c, const Tick& t) {
        Spy* s = static_cast<Spy*>(c);
        s->programs++; s->last_dt = t.dt_ms; s->last_missed = t.missed;
    }
}

TEST_CASE("registry: a component's own registrations run in their phases") {
    Spy s;
    Registry r;
    REQUIRE(r.add(s, &spy_input,   "spy_in",  GROUP_FAST, PHASE_INPUT));
    REQUIRE(r.add(s, &spy_present, "spy_out", GROUP_FAST, PHASE_PRESENT));
    CHECK(r.overflow() == 0);

    r.run_phase(PHASE_INPUT, 0);
    CHECK(s.inputs == 1);
    CHECK(s.presents == 0);          // its own phase, and no other
    r.run_phase(PHASE_PRESENT, 0);
    CHECK(s.presents == 1);

    // dt is the task's, so a handler that integrates never assumes its period.
    r.run_phase(PHASE_INPUT, 50);
    CHECK(s.inputs == 2);
    CHECK(s.last_dt == 50);
}

TEST_CASE("registry: a setup handler runs once, and never in the beat") {
    Spy s;
    Registry r;
    REQUIRE(r.add_setup(s, &spy_setup, "spy"));
    REQUIRE(r.add(s, &spy_input, "spy_in", GROUP_FAST, PHASE_INPUT));

    for (uint8_t p = 0; p < PHASE_COUNT; p++) r.run_phase((Phase)p, 0);
    CHECK(s.setups == 0);            // not dispatched by any phase

    r.setup(0);
    CHECK(s.setups == 1);
}

TEST_CASE("registry: a program takes no phase - it is PHASE_CONTROL or nothing") {
    Spy s;
    Registry r;
    REQUIRE(r.add_program(s, &spy_program, "prog", GROUP_SLOW));
    r.set_programs_enabled(true, 0);

    r.run_phase(PHASE_INPUT, 0);
    CHECK(s.programs == 0);
    r.run_phase(PHASE_CONTROL, 0);
    CHECK(s.programs == 1);
    CHECK(r.task(0).program != nullptr);
}

TEST_CASE("registry: a program does not run until control authority is granted") {
    Spy s;
    Registry r;
    REQUIRE(r.add_program(s, &spy_program, "prog", GROUP_SLOW));
    REQUIRE(r.add(s, &spy_input, "spy_in", GROUP_FAST, PHASE_INPUT));

    INFO("the gate starts SHUT: control authority starts withheld, so nothing "
         "can drive the field on the way through bring-up");
    CHECK_FALSE(r.programs_enabled());
    r.run_phase(PHASE_CONTROL, 0);
    CHECK(s.programs == 0);

    // ...and it gates programs ONLY. A withdrawal writes safe states into the
    // image, and the phases around CONTROL still have to carry them outward.
    r.run_phase(PHASE_INPUT, 0);
    CHECK(s.inputs == 1);

    r.set_programs_enabled(true, 0);
    r.run_phase(PHASE_CONTROL, 0);
    CHECK(s.programs == 1);
}

TEST_CASE("registry: control returning reseeds a program's timer, not the beat's") {
    Spy s;
    Registry r;
    REQUIRE(r.add_program(s, &spy_program, "prog", GROUP_SLOW));
    REQUIRE(r.add(s, &spy_input, "spy_in", GROUP_SLOW, PHASE_INPUT));
    r.set_programs_enabled(true, 0);
    r.run_phase(PHASE_CONTROL, 0);
    r.run_phase(PHASE_INPUT, 0);
    REQUIRE(s.programs == 1);
    REQUIRE(s.inputs == 1);

    // A long outage, then control back. The reseed is what stops the first
    // activation reporting the whole outage as its dt.
    r.set_programs_enabled(false, 10);
    r.set_programs_enabled(true, 60000);

    r.run_phase(PHASE_CONTROL, 60000);
    CHECK(s.programs == 1);              // the reseed beat: not due yet
    r.run_phase(PHASE_CONTROL, 60100);
    CHECK(s.programs == 2);
    CHECK(s.last_dt == 100);             // its nominal period, not 60 s

    INFO("an ordinary task's timer is untouched by a control edge");
    r.run_phase(PHASE_INPUT, 60000);
    CHECK(s.inputs == 2);
    CHECK(s.last_dt == 60000);
}

TEST_CASE("registry: a program's own gate is independent of control authority") {
    Spy s;
    Registry r;
    REQUIRE(r.add_program(s, &spy_program, "prog", GROUP_SLOW));
    r.set_programs_enabled(true, 0);
    r.run_phase(PHASE_CONTROL, 0);
    REQUIRE(s.programs == 1);

    const int16_t i = r.find_program("prog");
    REQUIRE(i == 0);
    CHECK(r.program_enabled(0));
    CHECK_FALSE(r.program_unsaved(0));       // nothing edited yet

    // Taken out by hand, with control authority still granted.
    REQUIRE(r.set_program_enabled(0, false, 100));
    CHECK_FALSE(r.program_enabled(0));
    CHECK(r.program_unsaved(0));             // live, not yet durable
    r.run_phase(PHASE_CONTROL, 200);
    CHECK(s.programs == 1);                  // did not run

    INFO("the stricter of the two gates wins, so an authority edge does not "
         "put a program the operator disabled back into the scan");
    r.set_programs_enabled(false, 300);
    r.set_programs_enabled(true, 400);
    r.run_phase(PHASE_CONTROL, 500);
    CHECK(s.programs == 1);

    // ...and back in by hand, which reseeds like the authority edge does.
    REQUIRE(r.set_program_enabled(0, true, 500));
    r.run_phase(PHASE_CONTROL, 500);
    CHECK(s.programs == 1);                  // the reseed beat: not due yet
    r.run_phase(PHASE_CONTROL, 600);
    CHECK(s.programs == 2);
    CHECK(s.last_dt == 100);                 // nominal period, not the outage
}

TEST_CASE("registry: find_program answers on the registered name, or -1") {
    Spy a, b;
    Registry r;
    REQUIRE(r.add_program(a, &spy_program, "irrigate", GROUP_SLOW));
    REQUIRE(r.add_program(b, &spy_program, "vent",     GROUP_SLOW));

    CHECK(r.find_program("irrigate") == 0);
    CHECK(r.find_program("vent")     == 1);
    CHECK(r.find_program("nope")     < 0);
    // A length-bounded word, as the shell passes it: a prefix is not a match.
    CHECK(r.find_program("vented", 4) == 1);
    CHECK(r.find_program("ven",    3) < 0);
}

TEST_CASE("registry: setting a gate to what it already is is not a new edit") {
    Spy s;
    Registry r;
    REQUIRE(r.add_program(s, &spy_program, "prog", GROUP_SLOW));
    REQUIRE(r.set_program_enabled(0, true, 0));   // already enabled
    CHECK_FALSE(r.program_unsaved(0));
    CHECK_FALSE(r.set_program_enabled(9, false, 0));  // past the last program
}

}  // namespace registered
