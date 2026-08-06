/**
 * @file test_task_timer.cpp
 * @brief TaskTimer (scheduler/task_timer.h): the due/late/dropped rule on its
 *        own, including the edges the scan driver only reaches indirectly.
 */
#include <cstdint>                    // uint16_t/uint32_t/uint64_t - period, offset, last-run stamp

#include "libbotanik/scheduler/task_timer.h" // Scheduler::TaskTimer - under test

#include "doctest.h"                  // the test framework (vendored single header); last so it sees our types

using Botanik::Scheduler::TaskTimer;

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
