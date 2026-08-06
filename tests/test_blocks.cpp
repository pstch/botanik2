/**
 * @file test_blocks.cpp
 * @brief Function blocks (scan/blocks.h). Every timed block is driven by an
 *        explicit dt, so these need no clock and no sleeping.
 */
#include <string>                     // std::string - doctest's failure messages

#include "libbotanik/plc/blocks/blocks.h" // the whole block vocabulary - under test

#include "doctest.h"                  // the test framework (vendored single header); last so it sees our types

// Compiles to an empty TU when the blocks are off - the namespace
// itself is gated, so this must sit ABOVE the using-directive.
#if BOTANIK_FEATURE_PLC_BLK_BASE && BOTANIK_FEATURE_PLC_BLK_FILTERS

using namespace Botanik::Plc;


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

TEST_CASE("Hysteresis3: three bands, each edge with its own hold") {
    Hysteresis3 h;
    // Cold call in the middle: the band the value is in, not a default.
    CHECK(h(21.0f, 18.0f, 26.0f, 1.0f) == Hysteresis3::BAND_MID);

    CHECK(h(26.0f, 18.0f, 26.0f, 1.0f) == Hysteresis3::BAND_HIGH);   // at high
    CHECK(h(25.5f, 18.0f, 26.0f, 1.0f) == Hysteresis3::BAND_HIGH);   // holds
    CHECK(h(25.0f, 18.0f, 26.0f, 1.0f) == Hysteresis3::BAND_MID);    // high - hyst
    CHECK(h.mid());

    CHECK(h(18.0f, 18.0f, 26.0f, 1.0f) == Hysteresis3::BAND_LOW);    // at low
    CHECK(h(18.5f, 18.0f, 26.0f, 1.0f) == Hysteresis3::BAND_LOW);    // holds
    CHECK(h(19.0f, 18.0f, 26.0f, 1.0f) == Hysteresis3::BAND_MID);    // low + hyst
    CHECK(h.low() == false);
}

TEST_CASE("Hysteresis3: seeds into the band it starts in, either end") {
    Hysteresis3 cold, hot;
    CHECK(cold(5.0f, 18.0f, 26.0f, 1.0f) == Hysteresis3::BAND_LOW);
    CHECK(hot(40.0f, 18.0f, 26.0f, 1.0f) == Hysteresis3::BAND_HIGH);
    // ...and a value parked between low and low+hyst reads MID rather than
    // sticking in LOW forever waiting for an edge that never comes.
    Hysteresis3 parked;
    CHECK(parked(18.5f, 18.0f, 26.0f, 1.0f) == Hysteresis3::BAND_MID);
}

TEST_CASE("Interlock: latches, and names the input that went first") {
    Interlock il("door", "pressure", "estop", "spare");
    CHECK_FALSE(il(true, true, true, true));      // starts un-permitted
    CHECK(il(true, true, true, true, true));      // one acknowledge arms it
    CHECK(il.first_out() == Interlock::NONE);

    CHECK_FALSE(il(true, false, true, true));     // pressure trips
    CHECK(il.first_out() == 1);
    CHECK(std::string(il.first_out_name()) == "pressure");

    // A second input falling does not steal the first-out.
    CHECK_FALSE(il(false, false, true, true));
    CHECK(std::string(il.first_out_name()) == "pressure");

    // Everything back, but the latch holds until acknowledged.
    CHECK_FALSE(il(true, true, true, true));
    CHECK(std::string(il.first_out_name()) == "pressure");
    CHECK(il(true, true, true, true, true));
    CHECK(il.first_out() == Interlock::NONE);
}

TEST_CASE("Interlock: the acknowledge is an edge, and needs the inputs back") {
    Interlock il;
    CHECK(il(true, true, true, true, true));
    CHECK_FALSE(il(true, true, false, true, true));   // trips despite reset held
    // Reset held true throughout: no rising edge, so no clear even once the
    // input recovers. A stuck reset button must not stand as permission.
    CHECK_FALSE(il(true, true, true, true, true));
    CHECK(il(true, true, true, true, false) == false);
    CHECK(il(true, true, true, true, true));          // a fresh edge clears it
}

TEST_CASE("Interlock: non-latching just follows the four inputs") {
    Interlock il(false);
    CHECK(il(true, true, true, true));
    CHECK_FALSE(il(true, true, true, false));
    CHECK(il.first_out() == 3);
    CHECK(il(true, true, true, true));                // recovers on its own
    CHECK(il.first_out() == Interlock::NONE);
}

TEST_CASE("Ewma: adopts the first sample, then lags by its time constant") {
    Ewma e;
    CHECK(e(20.0f, 10.0f, 1000) == doctest::Approx(20.0f));   // no warm-up ramp
    // One time constant of step: ~63% of the way there.
    float v = 20.0f;
    for (int i = 0; i < 10; i++) v = e(30.0f, 10.0f, 1000);
    CHECK(v == doctest::Approx(26.1f).epsilon(0.02));
    CHECK(e(30.0f, 0.0f, 1000) == doctest::Approx(30.0f));    // tau 0 = passthrough
}

TEST_CASE("Ewma: the same wall-clock response whatever the scan rate") {
    Ewma slow, fast;
    slow(0.0f, 10.0f, 1000);                                // seed both at zero
    fast(0.0f, 10.0f, 100);
    for (int i = 0; i < 10; i++) slow(1.0f, 10.0f, 1000);   // 1 Hz for 10 s
    for (int i = 0; i < 100; i++) fast(1.0f, 10.0f, 100);   // 10 Hz for 10 s
    CHECK(slow.value() == doctest::Approx(fast.value()).epsilon(0.05));
}

TEST_CASE("MedianOf3 drops a single spike outright") {
    MedianOf3 m;
    CHECK(m(10.0f) == doctest::Approx(10.0f));
    CHECK(m(10.0f) == doctest::Approx(10.0f));
    CHECK(m(10.0f) == doctest::Approx(10.0f));
    CHECK(m(900.0f) == doctest::Approx(10.0f));   // spike never reaches the output
    CHECK(m(10.0f) == doctest::Approx(10.0f));
    // A sustained move does get through, two samples late.
    m(20.0f);
    CHECK(m(20.0f) == doctest::Approx(20.0f));
}

TEST_CASE("MovingAverage: partial mean while filling, window mean once full") {
    MovingAverage<4> a;
    CHECK(a(4.0f) == doctest::Approx(4.0f));
    CHECK(a(8.0f) == doctest::Approx(6.0f));
    CHECK_FALSE(a.full());
    a(12.0f); a(16.0f);
    CHECK(a.full());
    CHECK(a.value() == doctest::Approx(10.0f));
    a(20.0f);                                     // 4 falls out of the window
    CHECK(a.value() == doctest::Approx(14.0f));
}

TEST_CASE("Integral: integrates a rate, and cuts off sensor noise") {
    Integral t;
    // 2 L/s for 3 s, in one late scan and two on-time ones.
    CHECK(t(2.0f, 1000) == doctest::Approx(2.0f));
    CHECK(t(2.0f, 2000) == doctest::Approx(6.0f));
    t.preset(0.0);
    // A dead pump reading 0.01 L/s totals nothing under a 0.05 cutoff...
    t(0.01f, 60000, 0.05f);
    CHECK(t.total() == doctest::Approx(0.0f));
    CHECK(t.rate() == doctest::Approx(0.0f));
    // ...but a real flow above it counts in full, not minus the cutoff.
    t(1.0f, 1000, 0.05f);
    CHECK(t.total() == doctest::Approx(1.0f));
}

TEST_CASE("Integral: still counts millilitres at ten thousand litres") {
    // The reason the accumulator is a double: at 1e4, a float's step is ~1e-3,
    // so a 0.5 mL increment would round away and the total would freeze.
    Integral t;
    t.preset(10000.0);
    for (int i = 0; i < 1000; i++) t(0.5f, 1000);   // 500 L, 0.5 L at a time
    CHECK(t.total64() == doctest::Approx(10500.0));
}

TEST_CASE("Derivative: differentiates over its window, holds between") {
    Derivative r;
    CHECK(r(100.0f, 2000, 1000) == doctest::Approx(0.0f));   // seeds, no rate yet
    CHECK(r(105.0f, 2000, 1000) == doctest::Approx(0.0f));   // window not closed
    CHECK(r(110.0f, 2000, 1000) == doctest::Approx(5.0f));   // 10 units / 2 s
    CHECK(r(110.0f, 2000, 1000) == doctest::Approx(5.0f));   // held between updates
    CHECK(r(110.0f, 2000, 1000) == doctest::Approx(0.0f));   // window closed flat
    // Signed: a falling quantity is a negative rate, not nothing.
    r(90.0f, 2000, 1000);
    CHECK(r(90.0f, 2000, 1000) == doctest::Approx(-10.0f));
}

TEST_CASE("Derivative: divides by the time really elapsed, not the window") {
    Derivative r;
    r(0.0f, 1000, 100);
    // One 4 s scan against a 1 s window: 40 units in 4 s is 10/s, not 40/s.
    CHECK(r(40.0f, 1000, 4000) == doctest::Approx(10.0f));
}

TEST_CASE("Integral and Derivative are inverses") {
    Integral vol;
    Derivative rate;
    for (int i = 0; i < 20; i++) {
        vol(3.0f, 500);                  // 3 units/s
        rate(vol.total(), 2000, 500);
    }
    CHECK(rate.value() == doctest::Approx(3.0f).epsilon(0.01));
}

#endif
