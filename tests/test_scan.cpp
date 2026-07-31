/**
 * @file test_scan.cpp
 * @brief The scan driver (scan/scan.h): multi-rate dispatch, the no-catch-up
 *        rule, and that dt_ms reports the real elapsed time.
 */
#include <cstddef>
#include <cstdint>
#include <vector>

#include "doctest.h"

#include "libbotanik/scan/scan.h"

using Botanik::Vars::Bank;

using namespace Botanik::Scan;

namespace {
    // Programs are plain functions, so the recording state is a file static.
    struct Rec { uint32_t dt; uint16_t missed; uint64_t now; };
    std::vector<Rec> g_fast, g_slow;

    void fast(ScanContext& c) { g_fast.push_back({c.dt_ms, c.missed, c.now_ms}); }
    void slow(ScanContext& c) { g_slow.push_back({c.dt_ms, c.missed, c.now_ms}); }

    const Task TASKS[] = {
        { "fast", 100,  &fast },
        { "slow", 1000, &slow },
    };

    void reset() { g_fast.clear(); g_slow.clear(); }
}

TEST_CASE("scan: each task runs at its own rate") {
    reset();
    TaskStats stats[2];
    Bank io;
    Driver d(TASKS, 2, stats, &io);
    for (uint64_t t = 0; t <= 2000; t += 10) d.step(t);

    // ~20 fast runs per 2 s, ~2 slow. Exact counts depend on the first-run
    // seeding, so assert the ratio holds rather than pinning both.
    CHECK(g_fast.size() >= 19);
    CHECK(g_fast.size() <= 21);
    CHECK(g_slow.size() >= 2);
    CHECK(g_slow.size() <= 3);
    CHECK(stats[0].runs == g_fast.size());
}

TEST_CASE("scan: dt_ms is the nominal period when on time") {
    reset();
    TaskStats stats[2];
    Bank io;
    Driver d(TASKS, 2, stats, &io);
    for (uint64_t t = 0; t <= 1000; t += 10) d.step(t);
    REQUIRE(g_fast.size() >= 3);
    for (size_t i = 1; i < g_fast.size(); i++) CHECK(g_fast[i].dt == 100);
}

TEST_CASE("scan: a late task runs once and reports the real elapsed time") {
    reset();
    TaskStats stats[2];
    Bank io;
    Driver d(TASKS, 2, stats, &io);
    d.step(0);        // first run seeds
    g_fast.clear();

    // A 950 ms stall: nine 100 ms activations were missed.
    d.step(1000);

    REQUIRE(g_fast.size() == 1);            // ran ONCE, no catch-up burst
    CHECK(g_fast[0].dt == 1000);            // and dt is the real gap, not 100
    CHECK(g_fast[0].missed == 9);
    CHECK(stats[0].missed_total == 9);
}

TEST_CASE("scan: period 0 runs every beat") {
    reset();
    static std::vector<uint64_t> beats;
    beats.clear();
    struct H { static void run(ScanContext& c) { beats.push_back(c.now_ms); } };
    const Task every[] = { { "every", 0, &H::run } };
    TaskStats stats[1];
    Bank io;
    Driver d(every, 1, stats, &io);
    for (uint64_t t = 0; t < 50; t += 10) d.step(t);
    CHECK(beats.size() == 5);
}

TEST_CASE("scan: a null table or null run pointer is inert") {
    TaskStats stats[1];
    Bank io;
    Driver none(nullptr, 0, nullptr, &io);
    none.step(0);                       // must not fault
    const Task holey[] = { { "nop", 0, nullptr } };
    Driver d(holey, 1, stats, &io);
    d.step(0);
    CHECK(stats[0].runs == 0);
}
