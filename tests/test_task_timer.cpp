/**
 * @file test_task_timer.cpp
 * @brief TaskTimer (scheduler/task_timer.h): the due/late/dropped rule on its
 *        own, including the edges the scan driver only reaches indirectly.
 */
#include <cstdint>

#include "doctest.h"

#include "libbotanik/scheduler/task_timer.h"

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

TEST_CASE("task timer: dt saturates instead of truncating past 32 bits") {
    TaskTimer t;
    uint32_t dt = 0; uint16_t missed = 0;
    t.due(0, 1000, dt, missed); t.ran(0);

    // Beyond a 32-bit ms span (~49 days). Truncating would report a small dt and
    // silently hand a timer a fraction of the time that actually passed.
    REQUIRE(t.due(0x1FFFFFFFFull, 1000, dt, missed));
    CHECK(dt == 0xFFFFFFFFu);
}
