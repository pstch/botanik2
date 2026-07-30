/**
 * @file test_blocks.cpp
 * @brief Function blocks (scan/blocks.h). Every timed block is driven by an
 *        explicit dt, so these need no clock and no sleeping.
 */
#include "doctest.h"

#include "libbotanik/scan/blocks.h"

using namespace Botanik::Scan;

TEST_CASE("Ton: delays the rise, drops immediately") {
    Ton t;
    CHECK_FALSE(t(true, 100, 40));    // 40 of 100
    CHECK_FALSE(t(true, 100, 40));    // 80
    CHECK(t(true, 100, 40));          // 120 -> elapsed
    CHECK(t(true, 100, 40));          // stays up
    CHECK_FALSE(t(false, 100, 40));   // drops at once
    CHECK_FALSE(t(true, 100, 40));    // and restarts from zero
}

TEST_CASE("Ton: a late scan does not lose the time it was late by") {
    Ton a, b;
    // One 100 ms step must be worth the same as ten 10 ms steps.
    for (int i = 0; i < 10; i++) a(true, 100, 10);
    CHECK(a.q());
    CHECK(b(true, 100, 100));
}

TEST_CASE("Tof: follows up, holds the fall") {
    Tof t;
    CHECK(t(true, 100, 10));
    CHECK(t(false, 100, 40));         // holding
    CHECK(t(false, 100, 40));         // 80, still holding
    CHECK_FALSE(t(false, 100, 40));   // 120 -> released
}

TEST_CASE("Tp: fixed-width pulse, immune to the input dropping") {
    Tp t;
    CHECK(t(true, 100, 10));          // rising edge starts it
    CHECK(t(false, 100, 40));         // input gone, pulse continues
    CHECK(t(false, 100, 40));
    CHECK_FALSE(t(false, 100, 40));   // width reached
    // Re-arms only on a fresh rising edge.
    CHECK_FALSE(t(false, 100, 10));
    CHECK(t(true, 100, 10));
}

TEST_CASE("RTrig / FTrig: one scan per edge") {
    RTrig r; FTrig f;
    CHECK(r(true));  CHECK_FALSE(r(true));   // only the transition
    // Both assume the input starts low, so neither fires on the first scan
    // merely because of where the input sits.
    CHECK_FALSE(f(false));
    f(true);
    CHECK(f(false)); CHECK_FALSE(f(false));
}

TEST_CASE("Sr is set-dominant, Rs is reset-dominant") {
    Sr sr; Rs rs;
    CHECK(sr(true, true));         // set wins
    CHECK_FALSE(rs(true, true));   // reset wins
    CHECK(sr(false, false));       // both latch
    CHECK_FALSE(rs(false, false));
    CHECK_FALSE(sr(false, true));
}

TEST_CASE("Hysteresis: holds between the thresholds") {
    Hysteresis h;
    CHECK_FALSE(h(20.0f, 20.0f, 25.0f));   // at low -> false
    CHECK_FALSE(h(24.0f, 20.0f, 25.0f));   // between -> holds false
    CHECK(h(25.0f, 20.0f, 25.0f));         // at high -> true
    CHECK(h(21.0f, 20.0f, 25.0f));         // between -> holds true
    CHECK_FALSE(h(19.0f, 20.0f, 25.0f));
}

TEST_CASE("Ramp: rate-limits toward the target, seeds on first call") {
    Ramp r;
    CHECK(r(50.0f, 10.0f, 1000) == doctest::Approx(50.0f));   // seed: no ramp
    CHECK(r(100.0f, 10.0f, 1000) == doctest::Approx(60.0f));  // 10/s for 1 s
    CHECK(r(100.0f, 10.0f, 500)  == doctest::Approx(65.0f));
    CHECK(r(0.0f,   10.0f, 1000) == doctest::Approx(55.0f));  // symmetric down
    r.preset(0.0f);
    CHECK(r.value() == doctest::Approx(0.0f));
}

TEST_CASE("Deadband: reports only moves larger than the band") {
    Deadband d;
    CHECK(d(10.0f, 0.5f) == doctest::Approx(10.0f));   // seed
    CHECK(d(10.3f, 0.5f) == doctest::Approx(10.0f));   // inside the band
    CHECK(d(10.6f, 0.5f) == doctest::Approx(10.6f));   // outside -> reported
    CHECK(d(10.2f, 0.5f) == doctest::Approx(10.6f));
}
