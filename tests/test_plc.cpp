/**
 * @file test_plc.cpp
 * @brief The PLC vocabulary: stateless functions, the block families, PID and actuator.
 *
 * Each section below is the former test_<name>.cpp, kept in its own
 * namespace so their fixtures and helpers cannot collide.
 */

#include "libbotanik/plc/functions/functions.h" // the stateless POU vocabulary
#include "doctest.h"
#include <string>   // std::string
#include "libbotanik/plc/blocks/blocks.h"
#include "libbotanik/utils/filters.h"   // Utils::ExpMovingAverage / WindowedMax
#include "libbotanik/plc/blocks/pid.h"  // Plc::Pid / PidTuning
#include "libbotanik/plc/blocks/actuator.h" // Plc::Actuator / ActuatorConfig

// --------------------------------------------------------------------------
// functions
// --------------------------------------------------------------------------

namespace functions {

using namespace Botanik::Plc;

TEST_CASE("limit holds a value inside its bounds") {
    CHECK(limit(5.0f, 0.0f, 10.0f) == doctest::Approx(5.0f));
    CHECK(limit(-1.0f, 0.0f, 10.0f) == doctest::Approx(0.0f));
    CHECK(limit(99.0f, 0.0f, 10.0f) == doctest::Approx(10.0f));
    // Constant-folded: the whole reason these are constexpr rather than a .cpp.
    static_assert(limit(99.0f, 0.0f, 10.0f) == 10.0f, "limit is constexpr");
}

TEST_CASE("in_range is inclusive at both ends") {
    CHECK(in_range(230.0f, 200.0f, 260.0f));
    CHECK(in_range(200.0f, 200.0f, 260.0f));
    CHECK(in_range(260.0f, 200.0f, 260.0f));
    CHECK_FALSE(in_range(199.9f, 200.0f, 260.0f));
}

TEST_CASE("deadzone reports the excursion, and is continuous at the edge") {
    CHECK(deadzone(0.3f, 0.5f) == doctest::Approx(0.0f));
    CHECK(deadzone(-0.3f, 0.5f) == doctest::Approx(0.0f));
    // No step out of the band: leaving it by a hair gives a hair, not `band`.
    CHECK(deadzone(0.5001f, 0.5f) == doctest::Approx(0.0001f));
    CHECK(deadzone(2.0f, 0.5f) == doctest::Approx(1.5f));
    CHECK(deadzone(-2.0f, 0.5f) == doctest::Approx(-1.5f));
}

TEST_CASE("scale rescales linearly, and a dead span reads as the low end") {
    CHECK(scale(0.5f, 0.0f, 1.0f, 20.0f, 80.0f) == doctest::Approx(50.0f));
    CHECK(scale(0.0f, 0.0f, 1.0f, 20.0f, 80.0f) == doctest::Approx(20.0f));
    CHECK(scale(2.0f, 0.0f, 1.0f, 20.0f, 80.0f) == doctest::Approx(140.0f));  // no clamp
    CHECK(scale(1.0f, 0.0f, 1.0f, 80.0f, 20.0f) == doctest::Approx(20.0f));   // inverted
    // A misconfigured span must give a dull answer, not an infinity that then
    // propagates into an output.
    CHECK(scale(7.0f, 3.0f, 3.0f, 20.0f, 80.0f) == doctest::Approx(20.0f));
}

TEST_CASE("lerp and sel") {
    CHECK(lerp(10.0f, 20.0f, 0.25f) == doctest::Approx(12.5f));
    CHECK(sel(false, 18.0f, 21.0f) == doctest::Approx(18.0f));
    CHECK(sel(true, 18.0f, 21.0f) == doctest::Approx(21.0f));
}

TEST_CASE("seconds converts the scan's dt") {
    CHECK(seconds(1000) == doctest::Approx(1.0f));
    CHECK(seconds(250) == doctest::Approx(0.25f));
    CHECK(seconds(0) == doctest::Approx(0.0f));
}

TEST_CASE("accumulate saturates at the limit rather than wrapping") {
    uint32_t e = 0;
    accumulate(e, 100, 40);
    CHECK(e == 40);
    accumulate(e, 100, 40);
    CHECK(e == 80);
    // A hold far longer than the limit stays elapsed.
    accumulate(e, 100, 0xFFFFFFFFu);
    CHECK(e == 100);
    accumulate(e, 100, 50);
    CHECK(e == 100);
}

TEST_CASE("countdown stops at zero rather than wrapping") {
    uint32_t r = 100;
    countdown(r, 40);
    CHECK(r == 60);
    countdown(r, 1000);
    CHECK(r == 0);
    countdown(r, 10);
    CHECK(r == 0);
}

}  // namespace functions

// --------------------------------------------------------------------------
// blocks
// --------------------------------------------------------------------------

namespace blocks {

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

}  // namespace blocks

// --------------------------------------------------------------------------
// filters
// --------------------------------------------------------------------------

namespace filters {

using Botanik::Utils::ema_alpha;
using Botanik::Utils::ExpMovingAverage;
using Botanik::Utils::WindowedMax;
using doctest::Approx;

// ema_alpha
// ----------------------------------------------------------------------------

TEST_CASE("ema_alpha: alpha is the sample-period / tau ratio") {
    CHECK(ema_alpha(100, 10) == Approx(0.1f));
    CHECK(ema_alpha(20,  10) == Approx(0.5f));
    CHECK(ema_alpha(40,  20) == Approx(0.5f));
}

TEST_CASE("ema_alpha: tau at or below one sample period collapses to 1.0") {
    CHECK(ema_alpha(10, 10) == Approx(1.0f)); // equal: can't resolve faster
    CHECK(ema_alpha(5,  10) == Approx(1.0f)); // below: clamps, no overshoot
    CHECK(ema_alpha(0,  10) == Approx(1.0f));
}

TEST_CASE("ema_alpha: default period is the tick period") {
    // Config::TASK_PERIOD_MS == 10, so these mirror the explicit-period cases.
    CHECK(ema_alpha(20)  == Approx(0.5f));
    CHECK(ema_alpha(100) == Approx(0.1f));
    CHECK(ema_alpha(10)  == Approx(1.0f));
}

// ExpMovingAverage
// ----------------------------------------------------------------------------

TEST_CASE("ExpMovingAverage: alpha 1.0 tracks the input exactly") {
    ExpMovingAverage ema;
    ema.configure(1.0f, 1.0f);
    ema.push(42.0f);
    CHECK(ema.value() == Approx(42.0f));
    ema.push(-7.0f);
    CHECK(ema.value() == Approx(-7.0f));
}

TEST_CASE("ExpMovingAverage: symmetric blend converges toward the input") {
    ExpMovingAverage ema;
    ema.configure(0.5f, 0.5f);
    ema.push(10.0f);              // 0.5*10 + 0.5*0
    CHECK(ema.value() == Approx(5.0f));
    ema.push(10.0f);             // 0.5*10 + 0.5*5
    CHECK(ema.value() == Approx(7.5f));
}

TEST_CASE("ExpMovingAverage: asymmetric attack/decay rises fast, holds on falls") {
    ExpMovingAverage ema;
    ema.configure(1.0f, 0.0f);   // snap up instantly, never decay
    ema.push(10.0f);
    CHECK(ema.value() == Approx(10.0f)); // rose: attack 1.0
    ema.push(5.0f);
    CHECK(ema.value() == Approx(10.0f)); // fell: decay 0.0 holds the peak
    ema.push(20.0f);
    CHECK(ema.value() == Approx(20.0f)); // rose again: attack 1.0
}

// WindowedMax
// ----------------------------------------------------------------------------

TEST_CASE("WindowedMax: reports the max of the samples in the window") {
    WindowedMax<4> w;
    w.configure(8);
    CHECK(w.value() == 0);       // empty
    w.push(3);
    w.push(7);
    w.push(2);
    CHECK(w.value() == 7);
}

TEST_CASE("WindowedMax: a peak ages out as the window fills past it") {
    WindowedMax<3> w;
    w.configure(3);              // window == K, so one sample per bucket
    w.push(9);
    w.push(1);
    w.push(1);
    CHECK(w.value() == 9);       // peak still inside the window
    w.push(1);                   // recycles the bucket holding 9
    CHECK(w.value() == 1);       // peak has aged out
}

TEST_CASE("WindowedMax: a weighted push ages the window by that many samples") {
    WindowedMax<3> w;
    w.configure(3);              // window == K, one sample per bucket
    w.push(9);
    CHECK(w.value() == 9);
    w.push(1, 3);                // weight 3: ages by a full window, so the peak drops out
    CHECK(w.value() == 1);
    // A weight beyond the whole window is clamped - no runaway rotation, peak stays gone.
    w.push(2, 100);
    CHECK(w.value() == 2);
}

TEST_CASE("WindowedMax: a later, higher peak supersedes immediately") {
    WindowedMax<3> w;
    w.configure(3);
    w.push(5);
    CHECK(w.value() == 5);
    w.push(12);
    CHECK(w.value() == 12);
}

TEST_CASE("WindowedMax: reset clears the window") {
    WindowedMax<4> w;
    w.configure(8);
    w.push(99);
    w.reset();
    CHECK(w.value() == 0);
}

TEST_CASE("WindowedMax: a window shorter than K still works (span floored to 1)") {
    WindowedMax<4> w;
    w.configure(2);              // window < K: each bucket spans one sample
    w.push(4);
    w.push(8);
    CHECK(w.value() == 8);
    // Push enough lower samples to rotate through all K buckets; the peak ages out.
    w.push(1);
    w.push(1);
    w.push(1);
    w.push(1);
    CHECK(w.value() == 1);
}

}  // namespace filters

// --------------------------------------------------------------------------
// pid
// --------------------------------------------------------------------------

namespace pid {

// Compiles to an empty TU when the blocks are off - the namespace
// itself is gated, so this must sit ABOVE the using-directive.
#if BOTANIK_FEATURE_PLC_BLK_BASE

using namespace Botanik::Plc;


namespace {

/** @brief A first-order plant: the reading chases the output with a lag, which
 *  is close enough to a heater in a room to exercise a loop. */
struct Plant {
    float pv;
    float gain;      ///< reading units per output unit at equilibrium
    float tau_s;

    void step(float out, uint32_t dt_ms) {
        const float dt = (float)dt_ms / 1000.0f;
        pv += (gain * out - pv) * (dt / (tau_s + dt));
    }
};

}  // namespace

TEST_CASE("Pid: pure P is proportional to the error, and clamps") {
    Pid p;
    const PidTuning t(2.0f);                       // kp 2, output 0..100
    CHECK(p(25.0f, 20.0f, t, 1000) == doctest::Approx(10.0f));   // 5 err * 2
    CHECK(p(25.0f, 25.0f, t, 1000) == doctest::Approx(0.0f));
    CHECK(p(25.0f, 0.0f, t, 1000) == doctest::Approx(50.0f));
    CHECK(p(99.0f, 0.0f, t, 1000) == doctest::Approx(100.0f));   // clamped
    CHECK(p.saturated());
    CHECK(p(0.0f, 25.0f, t, 1000) == doctest::Approx(0.0f));     // no negative output
}

TEST_CASE("Pid: reverse action inverts the error") {
    Pid direct, reverse;
    const PidTuning heat(2.0f);
    const PidTuning cool(2.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, true);
    // Too warm: a heating loop asks for nothing, a cooling loop for output.
    CHECK(direct(20.0f, 25.0f, heat, 1000) == doctest::Approx(0.0f));
    CHECK(reverse(20.0f, 25.0f, cool, 1000) == doctest::Approx(10.0f));
    CHECK(reverse.error() == doctest::Approx(5.0f));   // sign-corrected
}

TEST_CASE("Pid: the integral removes the offset a P-only loop leaves") {
    Plant plant = { 0.0f, 0.5f, 20.0f };
    Pid p;
    const PidTuning t(2.0f, 0.5f);     // PI
    for (int i = 0; i < 2000; i++) {
        plant.step(p(25.0f, plant.pv, t, 100), 100);
    }
    CHECK(plant.pv == doctest::Approx(25.0f).epsilon(0.01));
    CHECK(p.integral() > 0.0f);        // it is the integral holding it there
}

TEST_CASE("Pid: derivative is on the reading, so a setpoint step gives no kick") {
    Pid with_d;
    const PidTuning t(1.0f, 0.0f, 10.0f);
    with_d(20.0f, 20.0f, t, 1000);                     // settled, no error
    // Setpoint jumps 5 in one scan. On the error the derivative would be
    // 5/1s * kd = 50; on the reading it is zero, so only P responds.
    CHECK(with_d(25.0f, 20.0f, t, 1000) == doctest::Approx(5.0f));
}

TEST_CASE("Pid: derivative opposes a moving reading") {
    Pid p;
    const PidTuning t(0.0f, 0.0f, 10.0f, -100.0f, 100.0f);   // D only, signed output
    p(20.0f, 20.0f, t, 1000);
    // Reading climbing at 1/s: the D term pulls the output down by kd.
    CHECK(p(20.0f, 21.0f, t, 1000) == doctest::Approx(-10.0f));
    CHECK(p(20.0f, 20.0f, t, 1000) == doctest::Approx(10.0f));   // and up when it falls
}

TEST_CASE("Pid: the derivative filter trades response for noise rejection") {
    Pid raw, filtered;
    const PidTuning t_raw(0.0f, 0.0f, 1.0f, -100.0f, 100.0f);
    const PidTuning t_flt(0.0f, 0.0f, 1.0f, -100.0f, 100.0f, 5.0f);
    raw(0.0f, 0.0f, t_raw, 1000);
    filtered(0.0f, 0.0f, t_flt, 1000);
    // One noisy sample: unfiltered it lands whole, filtered it is a fraction.
    const float r = raw(0.0f, 1.0f, t_raw, 1000);
    const float f = filtered(0.0f, 1.0f, t_flt, 1000);
    CHECK(r == doctest::Approx(-1.0f));
    CHECK(f > r);
    CHECK(f < 0.0f);
}

TEST_CASE("Pid: anti-windup - a saturated loop comes off the limit at once") {
    Pid guarded;
    const PidTuning t(0.1f, 5.0f);
    // An hour of unreachable setpoint against a stuck reading. Unbounded, the
    // integral would reach 5 * 100 * 3600 = 1.8 million.
    for (int i = 0; i < 3600; i++) guarded(100.0f, 0.0f, t, 1000);
    CHECK(guarded.value() == doctest::Approx(100.0f));
    CHECK(guarded.saturated());
    CHECK(guarded.integral() <= 100.0f);     // never grew past the output span

    // Setpoint reached and overshot: the output backs off on the next scan
    // rather than spending an hour of accumulated excess first.
    const float out = guarded(100.0f, 130.0f, t, 1000);
    CHECK(out == doctest::Approx(0.0f));
    CHECK(guarded.integral() < 100.0f);
}

TEST_CASE("Pid: preset hands a running output back to the loop") {
    Pid p;
    const PidTuning t(1.0f, 0.1f);
    p.preset(40.0f);
    CHECK(p.value() == doctest::Approx(40.0f));
    CHECK(p.integral() == doctest::Approx(40.0f));
    // At the setpoint, the loop carries on from where manual left it.
    CHECK(p(25.0f, 25.0f, t, 1000) == doctest::Approx(40.0f));
}

TEST_CASE("Pid: reset clears the loop's memory") {
    Pid p;
    const PidTuning t(1.0f, 1.0f);
    for (int i = 0; i < 10; i++) p(30.0f, 20.0f, t, 1000);
    CHECK(p.integral() > 0.0f);
    p.reset();
    CHECK(p.integral() == doctest::Approx(0.0f));
    CHECK(p.value() == doctest::Approx(0.0f));
}

TEST_CASE("Pid: a zero dt scan is harmless") {
    Pid p;
    const PidTuning t(1.0f, 1.0f, 1.0f);
    const float before = p(30.0f, 20.0f, t, 1000);
    const float again  = p(30.0f, 20.0f, t, 0);   // no integration, no divide
    CHECK(again == doctest::Approx(before));
}

#endif

}  // namespace pid

// --------------------------------------------------------------------------
// actuator
// --------------------------------------------------------------------------

namespace actuator {

// Compiles to an empty TU when the blocks are off - the namespace
// itself is gated, so this must sit ABOVE the using-directive.
#if BOTANIK_FEATURE_PLC_BLK_BASE

using namespace Botanik::Plc;


namespace {

// 10 s open, 8 s close, 2% deadband, 1 s seating overrun, 250 ms minimum run,
// 500 ms reversal dead time.
const ActuatorConfig CFG(10000, 8000, 0.02f, 1000, 250, 500);

/** @brief Run the actuator for @p ms at a fixed target, 100 ms at a time.
 *  @return the number of scans the open coil was energised for. */
int drive(Actuator& a, float target, uint32_t ms) {
    int open_scans = 0;
    for (uint32_t t = 0; t < ms; t += 100) {
        a.step(target, CFG, 100);
        if (a.open_cmd()) open_scans++;
        // Extra parentheses: doctest decomposes the expression, and cannot
        // take one apart across a &&.
        CHECK_FALSE((a.open_cmd() && a.close_cmd()));   // never both coils
    }
    return open_scans;
}

}  // namespace

TEST_CASE("Actuator: starts UNKNOWN and datums against the closed stop") {
    Actuator a;
    CHECK(a.state() == Actuator::ST_UNKNOWN);
    CHECK_FALSE(a.known());

    // A mid target from UNKNOWN closes fully first: the position estimate is
    // worth nothing until something has reached a mechanical stop.
    a.step(0.5f, CFG, 100);
    CHECK(a.state() == Actuator::ST_CLOSING);
    CHECK(a.close_cmd());

    drive(a, 0.5f, 16000);         // 8 s travel + 1 s seating, then the move
    CHECK(a.known());
    CHECK(a.position() == doctest::Approx(0.5f).epsilon(0.05));
    CHECK(a.state() == Actuator::ST_OPEN);      // off the seat, so open
}

TEST_CASE("Actuator: a full-open target datums against the open stop instead") {
    Actuator a;
    a.step(1.0f, CFG, 100);
    CHECK(a.state() == Actuator::ST_OPENING);   // no pointless close first
    drive(a, 1.0f, 12000);
    CHECK(a.state() == Actuator::ST_OPEN);
    CHECK(a.position() == doctest::Approx(1.0f));
}

TEST_CASE("Actuator: partial openings are timed against the travel time") {
    Actuator a;
    a.preset(0.0f);                             // datumed closed already
    CHECK(a.state() == Actuator::ST_CLOSED);

    // 30% of a 10 s open travel is 3 s of coil.
    const int scans = drive(a, 0.3f, 6000);
    CHECK(scans == 30);
    CHECK(a.position() == doctest::Approx(0.3f));
    CHECK(a.state() == Actuator::ST_OPEN);
    CHECK_FALSE(a.moving());
}

TEST_CASE("Actuator: CLOSED means on the seat, and only a full close gives it") {
    Actuator a;
    a.preset(1.0f);
    drive(a, 0.5f, 6000);
    CHECK(a.state() == Actuator::ST_OPEN);      // half open is open
    drive(a, 0.0f, 10000);
    CHECK(a.state() == Actuator::ST_CLOSED);
    CHECK(a.position() == doctest::Approx(0.0f));
}

TEST_CASE("Actuator: a stroke to an end overruns, so the estimate re-datums") {
    Actuator a;
    a.preset(0.0f);
    // Walk it half open and back a few times. Without the seating overrun the
    // estimate would drift; with it, every full close puts it back on zero.
    for (int i = 0; i < 3; i++) {
        drive(a, 0.5f, 8000);
        drive(a, 0.0f, 8000);
        CHECK(a.position() == doctest::Approx(0.0f));
        CHECK(a.state() == Actuator::ST_CLOSED);
    }
}

TEST_CASE("Actuator: no twitching inside the deadband or under the minimum run") {
    Actuator a;
    a.preset(0.5f);
    drive(a, 0.51f, 2000);                      // inside the 2% deadband
    CHECK(a.position() == doctest::Approx(0.5f));
    // Outside the deadband, but 2.3% of a 10 s travel is a 230 ms run - under
    // min_run_ms, so still not worth energising the motor.
    drive(a, 0.523f, 2000);
    CHECK(a.position() == doctest::Approx(0.5f));
    drive(a, 0.6f, 3000);                       // 1 s of travel, worth doing
    CHECK(a.position() == doctest::Approx(0.6f));
}

TEST_CASE("Actuator: reversing mid-stroke passes through the dead time") {
    Actuator a;
    a.preset(0.0f);
    for (int i = 0; i < 10; i++) a.step(1.0f, CFG, 100);   // ~9% open
    CHECK(a.open_cmd());

    // Demand flips to closed: the open coil drops in the same scan, and the
    // close coil stays off for the reversal dead time.
    a.step(0.0f, CFG, 100);
    CHECK_FALSE(a.open_cmd());
    CHECK_FALSE(a.close_cmd());
    a.step(0.0f, CFG, 100);
    CHECK_FALSE(a.close_cmd());                 // 100 of 500 ms
    a.step(0.0f, CFG, 400);
    CHECK(a.close_cmd());                       // dead time served
}

TEST_CASE("Actuator: a committed seating stroke is not cut short") {
    Actuator a;
    a.preset(0.1f);
    // Target 0 from 10% open: the estimate says 800 ms of travel, but the
    // stroke is armed for that plus the seating overrun, and nothing cuts it
    // short at the estimated arrival - that overrun IS the datum.
    for (int i = 0; i < 12; i++) a.step(0.0f, CFG, 100);
    CHECK(a.position() == doctest::Approx(0.0f));   // estimate says arrived
    CHECK(a.close_cmd());                           // ...and it is still driving
    CHECK(a.state() == Actuator::ST_CLOSING);
    drive(a, 0.0f, 1500);
    CHECK(a.state() == Actuator::ST_CLOSED);
}

TEST_CASE("Actuator: stop drops both coils, home forgets the position") {
    Actuator a;
    a.preset(0.0f);
    a.step(1.0f, CFG, 100);
    CHECK(a.moving());
    a.stop();
    CHECK_FALSE(a.moving());
    CHECK(a.known());                           // the estimate still stands

    a.home();
    CHECK_FALSE(a.known());
    CHECK(a.state() == Actuator::ST_UNKNOWN);
    // ...and the next command datums again rather than trusting the estimate.
    // (The stop left a dead time behind, which this scan serves out.)
    a.step(0.5f, CFG, 500);
    CHECK(a.state() == Actuator::ST_CLOSING);
}

TEST_CASE("Actuator: a datum stroke cut short leaves the position unknown") {
    Actuator a;
    a.step(0.5f, CFG, 100);
    CHECK(a.state() == Actuator::ST_CLOSING);
    a.stop();
    CHECK(a.state() == Actuator::ST_UNKNOWN);
    CHECK_FALSE(a.known());
}

#endif

}  // namespace actuator
