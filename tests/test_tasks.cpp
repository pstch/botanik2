/**
 * @file test_tasks.cpp
 * @brief The task registry (scheduler/tasks.h): phase-major dispatch, group
 *        rates, the derived offsets, and the per-task cost numbers.
 */
#include <cstdint>
#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik/core/component.h"
#include "libbotanik/scheduler/tasks.h"

using namespace Botanik::Scheduler;

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
