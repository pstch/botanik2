/**
 * @file test_tasks.cpp
 * @brief The task registry and its timing primitive.
 *
 * Each section below is the former test_<name>.cpp, kept in its own
 * namespace so their fixtures and helpers cannot collide.
 */

#include <cstdint>                      // uint8_t/uint32_t/uint64_t
#include <string>                       // std::string
#include <vector>                       // std::vector
#include "libbotanik/core/component.h"  // Component
#include "libbotanik/tasks/tasks.h" // Tasks::Registry
#include "doctest.h"
#include "libbotanik/tasks/task_timer.h"    // Tasks::TaskTimer

// --------------------------------------------------------------------------
// tasks
// --------------------------------------------------------------------------

namespace tasks {

using namespace Botanik::Tasks;

namespace {
    std::vector<std::string> g_log;

    void note(void* self, uint64_t) { g_log.push_back((const char*)self); }

    // Distinct storage per task, since `self` is what identifies it here.
    char A[] = "a", B[] = "b", C[] = "c";
}

TEST_CASE("tasks: a phase runs only its own members, in registration order") {
    g_log.clear();
    Registry r;
    r.add("in",  GROUP_FAST, PHASE_INPUT,   &note, A);
    r.add("out", GROUP_FAST, PHASE_OUTPUT,  &note, B);
    r.add("in2", GROUP_FAST, PHASE_INPUT,   &note, C);

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
    r.add("fast", GROUP_FAST,       PHASE_SERVICE, &note, A);
    r.add("slow", GROUP_SLOW,       PHASE_SERVICE, &note, B);
    r.add("bkg",  GROUP_BACKGROUND, PHASE_SERVICE, &note, C);

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

TEST_CASE("tasks: group-mates are offset off each other's beats") {
    // The property the derived offset exists for: two tasks at one rate must not
    // pile onto the same beat, or the rate cuts the mean work and leaves the
    // peak - which is what decides the shortest beat the loop can hold.
    Registry r;
    r.add("s1", GROUP_SLOW, PHASE_SERVICE, &note, A);
    r.add("s2", GROUP_SLOW, PHASE_SERVICE, &note, B);

    size_t shared = 0;
    for (uint64_t t = 0; t <= 1000; t += 10) {
        g_log.clear();
        r.run_phase(PHASE_SERVICE, t);
        if (g_log.size() > 1) shared++;
    }
    CHECK(shared == 1);            // the first beat only, which is unavoidable
}

TEST_CASE("tasks: each run is costed, and the mean is not the peak") {
    g_log.clear();
    Registry r;
    r.add("x", GROUP_FAST, PHASE_SERVICE, &note, A);

    for (uint64_t t = 0; t < 50; t += 10) r.run_phase(PHASE_SERVICE, t);

    REQUIRE(r.count() == 1);
    const Task& t = r.task(0);
    CHECK(t.cost.runs == 5);
    CHECK(t.cost.us_max >= t.cost.us_mean());
    CHECK(std::string(t.name) == "x");
    CHECK(std::string(group_name(t.group)) == "fast");
    CHECK(std::string(phase_name(t.phase)) == "service");
}

TEST_CASE("tasks: a full table refuses and counts, it does not drop silently") {
    Registry r;
    for (uint16_t i = 0; i < Botanik::Config::MAX_TASKS; i++)
        CHECK(r.add("t", GROUP_FAST, PHASE_SERVICE, &note, A));

    CHECK(r.count() == Botanik::Config::MAX_TASKS);
    CHECK_FALSE(r.add("one too many", GROUP_FAST, PHASE_SERVICE, &note, A));
    CHECK(r.overflow() == 1);
}

TEST_CASE("tasks: a null dispatch is refused rather than called") {
    Registry r;
    CHECK_FALSE(r.add("nop", GROUP_FAST, PHASE_SERVICE, nullptr, nullptr));
    CHECK(r.count() == 0);
    r.run_phase(PHASE_SERVICE, 0);   // must not fault
}

TEST_CASE("tasks: setup() runs before any beat, exactly once") {
    // Setup used to happen lazily on a component's first step(), which put the
    // parameter load's 423 storage reads inside the first beat.
    struct Spy : public Botanik::Component {
        uint32_t setups = 0, steps = 0;
        Spy() : Component(nullptr, "spy") {}
    protected:
        void on_setup(uint64_t) override { setups++; }
        void on_step(uint64_t) override { steps++; }
    };
    Spy a, b;
    Registry r;
    r.add_component("a", GROUP_FAST, PHASE_INPUT,  a);
    r.add_component("b", GROUP_SLOW, PHASE_PRESENT, b);

    r.setup(0);
    CHECK(a.setups == 1);
    CHECK(b.setups == 1);
    CHECK(a.steps == 0);          // init, not a beat
    CHECK(b.steps == 0);

    // The lazy path in step() must not run it a second time.
    r.run_phase(PHASE_INPUT, 0);
    r.run_phase(PHASE_PRESENT, 0);
    CHECK(a.setups == 1);
    CHECK(b.setups == 1);
    CHECK(a.steps == 1);
    CHECK(b.steps == 1);
}

TEST_CASE("tasks: a component the registry never set up still self-inits") {
    // The fallback the network stack's children rely on: nested components have
    // no registration to be set up from, so step() still does it.
    struct Spy : public Botanik::Component {
        uint32_t setups = 0;
        Spy() : Component(nullptr, "spy") {}
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

TEST_CASE("tasks: setup() skips a task with no setup, and does not fault") {
    g_log.clear();
    Registry r;
    r.add("plain", GROUP_FAST, PHASE_SERVICE, &note, A);   // no setup fn
    r.setup(0);
    CHECK(g_log.empty());          // setup is not run(); nothing was dispatched
    r.run_phase(PHASE_SERVICE, 0);
    CHECK(g_log.size() == 1);
}

TEST_CASE("tasks: a Component registers and is stepped through the registry") {
    struct Spy : public Botanik::Component {
        uint32_t steps = 0;
        Spy() : Component(nullptr, "spy") {}
    protected:
        void on_step(uint64_t) override { steps++; }
    };
    Spy s;
    Registry r;
    r.add_component("spy", GROUP_FAST, PHASE_SERVICE, s);

    for (uint64_t t = 0; t < 30; t += 10) r.run_phase(PHASE_SERVICE, t);
    CHECK(s.steps == 3);
}

}  // namespace tasks

// --------------------------------------------------------------------------
// task_timer
// --------------------------------------------------------------------------

namespace task_timer {

using Botanik::Tasks::TaskTimer;

TEST_CASE("task timer: the first call is always due, at the nominal period") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;

    CHECK(t.runs() == 0);
    // Due at t=0 even though nothing has elapsed: dt is the nominal period, or
    // a timer handed "everything since boot" would fire immediately.
    CHECK(t.due(0, 100, dt, missed));
    CHECK(dt == 100);
    CHECK(missed == 0);
}

TEST_CASE("task timer: due() is a pure query, ran() is what commits") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;

    // Asking repeatedly must not consume the activation - the scan cycle asks
    // and then declines to run while control is withheld.
    CHECK(t.due(0, 100, dt, missed));
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

    // A 900 ms stall on a 100 ms task: nine activations went past.
    REQUIRE(t.due(1000, 100, dt, missed));
    CHECK(dt == 1000);      // the REAL gap, not the nominal 100
    CHECK(missed == 9);
}

TEST_CASE("task timer: one period late is late, not missed") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;
    t.due(0, 100, dt, missed); t.ran(0);

    // 199 ms is one activation, served late; nothing was skipped.
    REQUIRE(t.due(199, 100, dt, missed));
    CHECK(dt == 199);
    CHECK(missed == 0);
}

TEST_CASE("task timer: the missed count saturates instead of wrapping") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;
    t.due(0, 1, dt, missed); t.ran(0);

    // 100 M activations of a 1 ms task: far past what uint16_t holds.
    REQUIRE(t.due(100000000ull, 1, dt, missed));
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

    // Offset spent: the rate is nominal from here on, not 130.
    CHECK_FALSE(t.due(229, 100, dt, missed));
    CHECK(t.due(230, 100, dt, missed));
}

TEST_CASE("task timer: offset timers of one period never share a beat again") {
    // The property the offset exists for: same period, different offsets, so
    // the periodic work spreads over beats instead of piling onto one.
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
    // Same rate, not the same count: b's activations sit 40 ms later, so its
    // last one falls past the window edge rather than being lost.
    CHECK(a_runs == 21);           // 0, 100 .. 2000
    CHECK(b_runs == 20);           // 0, 140 .. 1940
}

TEST_CASE("task timer: dt saturates instead of truncating past 32 bits") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;
    t.due(0, 1000, dt, missed); t.ran(0);

    // Beyond a 32-bit ms span (~49 days). Truncating would report a small dt and
    // silently hand a timer a fraction of the time that actually passed.
    REQUIRE(t.due(0x1FFFFFFFFull, 1000, dt, missed));
    CHECK(dt == 0xFFFFFFFFu);
}

}  // namespace task_timer
