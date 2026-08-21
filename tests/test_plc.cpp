/**
 * @file test_plc.cpp
 * @brief The PLC vocabulary: stateless functions, the block families, PID and actuator.
 *
 * Each section is its own namespace, isolating fixtures and helpers.
 */

#include "libbotanik/plc/functions/functions.h"
#include "doctest.h"
#include <string>
#include <cmath>
#include "libbotanik/plc/blocks/blocks.h"
#include "libbotanik/utils/filters.h"
#include "libbotanik/plc/blocks/pid.h"
#include "libbotanik/plc/blocks/actuator.h"

// functions

namespace functions {

using namespace Botanik::Plc;

TEST_CASE("limit holds a value inside its bounds") {
    CHECK(limit(5.0f, 0.0f, 10.0f) == doctest::Approx(5.0f));
    CHECK(limit(-1.0f, 0.0f, 10.0f) == doctest::Approx(0.0f));
    CHECK(limit(99.0f, 0.0f, 10.0f) == doctest::Approx(10.0f));
    // Constant-folded: the whole reason these are constexpr rather than a .cpp.
    static_assert(limit(99.0f, 0.0f, 10.0f) == 10.0f, "limit is constexpr");
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
    // A misconfigured span must give a dull answer, not a propagating infinity.
    CHECK(scale(7.0f, 3.0f, 3.0f, 20.0f, 80.0f) == doctest::Approx(20.0f));
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

// blocks

namespace blocks {

// Empty TU when blocks are off (guard sits above the using-directive).
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
    // Both assume the input starts low; neither fires on the first scan alone.
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
    // A value parked between low and low+hyst reads MID, not stuck in LOW.
    Hysteresis3 parked;
    CHECK(parked(18.5f, 18.0f, 26.0f, 1.0f) == Hysteresis3::BAND_MID);
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
    // A double accumulator: at 1e4, a float's ~1e-3 step would round away a
    // 0.5 mL increment and freeze the total.
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

#endif

}  // namespace blocks

// filters

namespace filters {

using Botanik::Utils::ema_alpha;
using Botanik::Utils::ExpMovingAverage;
using Botanik::Utils::WindowedMax;
using doctest::Approx;

// ema_alpha

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

// ExpMovingAverage

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

// pid

namespace pid {

// Empty TU when blocks are off (guard sits above the using-directive).
#if BOTANIK_FEATURE_PLC_BLK_BASE

using namespace Botanik::Plc;


namespace {

/** @brief A first-order plant: reading chases output with a lag, close
 *  enough to a heater to exercise a loop. */
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

TEST_CASE("Tp: a pulse shorter than the scan still shows for one activation") {
    // IEC 61131-3 gives TP a minimum of one activation; judging expiry from pulse start would let width <= dt_ms go unseen.
    Tp a;
    CHECK(a(true, 50, 100) == true);         // width < dt: still one activation
    CHECK(a(true, 50, 100) == false);        // ...and only one
    Tp b;
    CHECK(b(true, 100, 100) == true);        // width == dt
    CHECK(b(true, 100, 100) == false);
    // The ordinary case is unchanged: a pulse wider than the scan spans scans.
    Tp c;
    CHECK(c(true, 250, 100) == true);
    CHECK(c(true, 250, 100) == true);
    CHECK(c(true, 250, 100) == true);
    CHECK(c(true, 250, 100) == false);
}

TEST_CASE("Ramp: a non-positive rate freezes rather than running away") {
    // The clamps assume a positive step; a negative one walks the output away unbounded (an operator-reachable Tunable).
    Ramp r;
    r(0.0f, 1.0f, 100);                      // seed at 0
    for (int i = 0; i < 20; i++) r(5.0f, -0.01f, 100);
    CHECK(r.value() == doctest::Approx(0.0f));   // frozen, not diverging
    CHECK(r.value() <= 5.0f);                    // never past the target
    Ramp z;
    z(0.0f, 1.0f, 100);
    for (int i = 0; i < 20; i++) z(5.0f, 0.0f, 100);
    CHECK(z.value() == doctest::Approx(0.0f));   // a zero rate is no motion
}

TEST_CASE("blocks: every block in the family can be reset") {
    // block.h asserts this at compile time; runtime: reset reads as cold, summed across blocks.
    Ramp ramp;   ramp(7.0f, 1.0f, 100);   ramp.reset();
    CHECK(ramp(3.0f, 1.0f, 100) == doctest::Approx(3.0f));   // re-seeds, no ramp from 7

    Deadband db;  db(10.0f, 1.0f);        db.reset();
    CHECK(db(2.0f, 1.0f) == doctest::Approx(2.0f));          // reports, not suppressed

    Actuator act; const ActuatorConfig cfg(10000, 8000, 0.02f, 1000, 250, 500);
    for (int i = 0; i < 10; i++) act.step(1.0f, cfg, 2000);   // datum stroke, seated
    REQUIRE(act.known());
    act.reset();
    CHECK_FALSE(act.known());                                 // datums again
    CHECK_FALSE(act.open_cmd());
    CHECK_FALSE(act.close_cmd());
}

TEST_CASE("Pid: a non-zero output floor does not become a phantom integral") {
    // limit() clamps a fresh integral UP to out_min, and anti-windup pins it there: authority never earned.
    Pid p;
    const PidTuning t(2.0f, 0.5f, 0.0f, 20.0f, 100.0f);   // floor at 20
    p(25.0f, 24.0f, t, 100);                              // one integrating scan
    CHECK(p.integral() < 1.0f);        // ~0.5, not 20
    CHECK(p.integral() > 0.0f);

    // The loop still settles with the integral it earned - the phantom grants the floor's worth up front, un-refundable near the floor.
    Plant plant = { 0.0f, 0.5f, 20.0f };                  // equilibrium: out 50 -> pv 25
    Pid q;
    for (int i = 0; i < 2000; i++) plant.step(q(25.0f, plant.pv, t, 100), 100);
    CHECK(plant.pv == doctest::Approx(25.0f).epsilon(0.01));
    CHECK(q.integral() == doctest::Approx(50.0f).epsilon(0.1));   // earned, not granted
}

TEST_CASE("Pid: ki back to 0 removes the integral rather than freezing it") {
    // Tunings are operator-editable; a frozen integral after I->0 would command the output forever.
    Plant plant = { 0.0f, 0.5f, 20.0f };
    Pid p;
    const PidTuning pi(2.0f, 0.5f);
    for (int i = 0; i < 2000; i++) plant.step(p(25.0f, plant.pv, pi, 100), 100);
    REQUIRE(p.integral() > 0.0f);          // wound up holding the setpoint

    const PidTuning p_only(2.0f);          // same loop, ki now 0
    const float out = p(25.0f, plant.pv, p_only, 100);
    CHECK(p.integral() == 0.0f);
    // Settled loop, P alone ~0: output collapses instead of holding the old integral.
    CHECK(out < 1.0f);
}

TEST_CASE("Pid: derivative is on the reading, so a setpoint step gives no kick") {
    Pid with_d;
    const PidTuning t(1.0f, 0.0f, 10.0f);
    with_d(20.0f, 20.0f, t, 1000);                     // settled, no error
    // Setpoint jumps 5 in one scan: on error kd*5/1s=50, on reading zero - only P responds.
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
    // An hour of unreachable setpoint on a stuck reading; unbounded, the
    // integral would reach 5*100*3600 = 1.8 million.
    for (int i = 0; i < 3600; i++) guarded(100.0f, 0.0f, t, 1000);
    CHECK(guarded.value() == doctest::Approx(100.0f));
    CHECK(guarded.saturated());
    CHECK(guarded.integral() <= 100.0f);     // never grew past the output span

    // Setpoint reached and overshot: output backs off next scan, not after an hour of excess.
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

TEST_CASE("Pid: a zero dt scan is harmless") {
    Pid p;
    const PidTuning t(1.0f, 1.0f, 1.0f);
    const float before = p(30.0f, 20.0f, t, 1000);
    const float again  = p(30.0f, 20.0f, t, 0);   // no integration, no divide
    CHECK(again == doctest::Approx(before));
}

#endif

}  // namespace pid

// actuator

namespace actuator {

// Empty TU when blocks are off (guard sits above the using-directive).
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
        // Extra parens: doctest can't decompose an expression across &&.
        CHECK_FALSE((a.open_cmd() && a.close_cmd()));   // never both coils
    }
    return open_scans;
}

}  // namespace

TEST_CASE("Actuator: starts UNKNOWN and datums against the closed stop") {
    Actuator a;
    CHECK(a.state() == Actuator::ST_UNKNOWN);
    CHECK_FALSE(a.known());

    // A mid target from UNKNOWN closes fully first: the estimate is worthless until a stop is reached.
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
    // Without the seating overrun the estimate would drift; every full close re-zeros it.
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
    // 2.3% of a 10 s travel is 230 ms - under min_run_ms, not worth energising.
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

    // Demand flips closed: open coil drops same scan, close coil waits out the dead time.
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
    // Target 0 from 10% open: armed for the 800 ms estimate plus the seating overrun - the overrun IS the datum.
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
    // The next command datums again rather than trusting the estimate.
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

namespace psychro {

using namespace Botanik::Plc;
// Psychrometrics: what "how wet is it" means to a control law.

TEST_CASE("psychro: saturated air has its dew point at its own temperature") {
    for (float t = -10.0f; t <= 40.0f; t += 10.0f) {
        CHECK(dew_point(t, 1.0f) == doctest::Approx(t).epsilon(0.001));
        CHECK(dew_point_margin(t, 1.0f) == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(vapour_pressure_deficit(t, 1.0f) == doctest::Approx(0.0f).epsilon(0.001));
    }
}

TEST_CASE("psychro: the dew point tracks published values") {
    // Textbook pairs, to a tenth of a degree.
    CHECK(dew_point(20.0f, 0.50f) == doctest::Approx(9.3f).epsilon(0.02));
    CHECK(dew_point(30.0f, 0.60f) == doctest::Approx(21.4f).epsilon(0.02));
    CHECK(dew_point(10.0f, 0.80f) == doctest::Approx(6.7f).epsilon(0.03));
}

TEST_CASE("psychro: drier air is a larger margin, and the margin never goes negative") {
    float last = -1.0f;
    for (float rh = 0.95f; rh >= 0.15f; rh -= 0.10f) {
        const float m = dew_point_margin(20.0f, rh);
        CHECK(m > last);
        CHECK(m >= 0.0f);
        last = m;
    }
}

TEST_CASE("psychro: a sensor reading zero humidity does not produce a NaN") {
    // ln(0) is undefined; RH_FLOOR is what keeps it out of every law downstream.
    const float m = dew_point_margin(20.0f, 0.0f);
    CHECK(std::isfinite(m));
    CHECK(m == doctest::Approx(dew_point_margin(20.0f, RH_FLOOR)));
    CHECK(std::isfinite(vapour_pressure_deficit(20.0f, 0.0f)));
}

TEST_CASE("psychro: the same margin is a bigger deficit when it is warm") {
    // The spec's worked figures: ~0.4 kPa at 10 degC against ~1.0 kPa at 30 degC,
    // for one 3 degC margin. This nonlinearity is why rot is set in degC and
    // irrigation in kPa.
    auto vpd_at = [](float t) {
        // the RH that puts the dew point exactly 3 degC below t. The margin
        // FALLS as humidity rises, so a margin still too wide means looking at
        // wetter air.
        float lo = 0.05f, hi = 1.0f;
        for (int i = 0; i < 60; i++) {
            const float mid = 0.5f * (lo + hi);
            (dew_point_margin(t, mid) > 3.0f ? lo : hi) = mid;
        }
        return vapour_pressure_deficit(t, 0.5f * (lo + hi));
    };
    CHECK(vpd_at(10.0f) == doctest::Approx(0.4f).epsilon(0.2));
    CHECK(vpd_at(30.0f) == doctest::Approx(1.0f).epsilon(0.2));
    CHECK(vpd_at(30.0f) > 2.0f * vpd_at(10.0f));
}

}  // namespace psychro

namespace ladders {

using namespace Botanik::Plc;

TEST_CASE("median3: the middle survives, a stuck probe never does") {
    CHECK(median3(1.0f, 2.0f, 3.0f) == 2.0f);
    CHECK(median3(3.0f, 1.0f, 2.0f) == 2.0f);
    CHECK(median3(2.0f, 3.0f, 1.0f) == 2.0f);
    INFO("a probe railed high or low is one of the two discarded");
    CHECK(median3(-40.0f, 20.1f, 20.3f) == 20.1f);
    CHECK(median3(999.0f, 20.1f, 20.3f) == 20.3f);
    CHECK(median3(5.0f, 5.0f, 5.0f) == 5.0f);
}

TEST_CASE("ladder: a quantity becomes a demand between 0 and 1") {
    CHECK(ladder(15.0f, 20.0f, 30.0f) == 0.0f);
    CHECK(ladder(25.0f, 20.0f, 30.0f) == doctest::Approx(0.5f));
    CHECK(ladder(35.0f, 20.0f, 30.0f) == 1.0f);
    INFO("an inverted span fails toward acting, not toward doing nothing");
    CHECK(ladder(50.0f, 30.0f, 20.0f) == 1.0f);
    CHECK(ladder(10.0f, 30.0f, 20.0f) == 0.0f);
}

TEST_CASE("signed_ladder: a dead band, then a branch each way") {
    // cooling: nothing between -0.5 and +0.5 degC of the target
    const float nf = -5.0f, ns = -0.5f, ps = 0.5f, pf = 5.0f;
    CHECK(signed_ladder(0.0f, nf, ns, ps, pf) == 0.0f);
    CHECK(signed_ladder(0.4f, nf, ns, ps, pf) == 0.0f);
    CHECK(signed_ladder(-0.4f, nf, ns, ps, pf) == 0.0f);

    CHECK(signed_ladder(2.75f, nf, ns, ps, pf) == doctest::Approx(0.5f));
    CHECK(signed_ladder(-2.75f, nf, ns, ps, pf) == doctest::Approx(-0.5f));
    CHECK(signed_ladder(9.0f, nf, ns, ps, pf) == 1.0f);
    CHECK(signed_ladder(-9.0f, nf, ns, ps, pf) == -1.0f);

    INFO("and it is continuous across each edge of the band");
    CHECK(signed_ladder(ps, nf, ns, ps, pf) == doctest::Approx(0.0f));
    CHECK(signed_ladder(ns, nf, ns, ps, pf) == doctest::Approx(0.0f));
}

}  // namespace ladders

namespace vote {

using namespace Botanik::Plc;

TEST_CASE("closest2_mean: the outlier goes, the agreeing pair is averaged") {
    CHECK(closest2_mean(20.0f, 20.2f, 30.0f) == doctest::Approx(20.1f));
    CHECK(closest2_mean(30.0f, 20.0f, 20.2f) == doctest::Approx(20.1f));
    CHECK(closest2_mean(20.2f, 30.0f, 20.0f) == doctest::Approx(20.1f));

    INFO("a probe railed either way is the one dropped");
    CHECK(closest2_mean(-40.0f, 20.1f, 20.3f) == doctest::Approx(20.2f));
    CHECK(closest2_mean(999.0f, 20.1f, 20.3f) == doctest::Approx(20.2f));

    INFO("three that agree average to the middle");
    CHECK(closest2_mean(5.0f, 5.0f, 5.0f) == doctest::Approx(5.0f));
    CHECK(closest2_mean(19.0f, 20.0f, 21.0f) == doctest::Approx(19.5f));
}

TEST_CASE("closest2_mean: it keeps the second probe's information") {
    // Against a plain median, which discards it. Two probes reading either side
    // of the truth average back onto it; the median lands on whichever one.
    const float truth = 20.0f;
    const float got = closest2_mean(truth - 0.2f, truth + 0.2f, 40.0f);
    CHECK(got == doctest::Approx(truth));
    CHECK(median3(truth - 0.2f, truth + 0.2f, 40.0f) == doctest::Approx(truth + 0.2f));
}

}  // namespace vote

namespace actuator_drift {

using namespace Botanik::Plc;

namespace {
/** @brief Hold @p target for a fixed span, long enough for any single stroke
 *  plus its seating overrun and the dead time after it.
 *
 *  Not "until it stops moving": moving() is false during the reversal dead time
 *  too, so that test returns before the stroke has even started. */
void settle_at(Actuator& act, float target, const ActuatorConfig& cfg,
               int steps = 400) {
    for (int i = 0; i < steps; i++) act.step(target, cfg, 50);
}
}  // namespace

TEST_CASE("Actuator: partial moves accumulate drift, and a seat clears it") {
    Actuator a;
    const ActuatorConfig cfg(10000, 10000, 0.02f, 1000, 250, 500, /*redatum*/ 3.0f);

    settle_at(a, 0.0f, cfg);                 // datum against the closed stop
    REQUIRE(a.state() == Actuator::ST_CLOSED);
    CHECK(a.drift() == doctest::Approx(0.0f).epsilon(0.05));

    // Partial moves only: never to either end, so nothing re-datums.
    settle_at(a, 0.5f, cfg);
    const float after_one = a.drift();
    CHECK(after_one > 0.4f);
    settle_at(a, 0.3f, cfg);
    CHECK(a.drift() > after_one);

    INFO("a stroke that reaches the stop makes the estimate a fact again");
    settle_at(a, 0.0f, cfg);
    REQUIRE(a.state() == Actuator::ST_CLOSED);
    CHECK(a.drift() == doctest::Approx(0.0f).epsilon(0.05));
}

TEST_CASE("Actuator: past the allowance it closes fully, then reopens to target") {
    Actuator a;
    const ActuatorConfig cfg(10000, 10000, 0.02f, 1000, 250, 500, /*redatum*/ 0.8f);

    settle_at(a, 0.0f, cfg);
    settle_at(a, 0.6f, cfg);                 // 0.6 of travel, under the allowance
    REQUIRE_FALSE(a.redatum_due(cfg));
    settle_at(a, 0.2f, cfg);                 // +0.4 -> past it
    REQUIRE(a.redatum_due(cfg));

    // The next partial move must detour to the seat first.
    a.step(0.7f, cfg, 50);
    CHECK(a.close_cmd());

    // Watch the whole detour rather than sampling it: the close, the seat, and
    // the reopen all fit inside one hold, so a check afterwards would only ever
    // see the destination.
    bool  seated = false;
    float drift_at_seat = 1.0f;
    for (int i = 0; i < 800; i++) {
        a.step(0.7f, cfg, 50);
        if (!seated && a.state() == Actuator::ST_CLOSED) {
            seated = true;
            drift_at_seat = a.drift();
        }
    }
    INFO("it went to the closed stop on the way, which is the point");
    CHECK(seated);
    CHECK(drift_at_seat == doctest::Approx(0.0f).epsilon(0.05));

    INFO("...and then out to where it was actually asked for");
    CHECK(a.position() == doctest::Approx(0.7f).epsilon(0.1));
    CHECK(a.state() == Actuator::ST_OPEN);
}

TEST_CASE("Actuator: a target AT an end needs no detour, it already re-datums") {
    Actuator a;
    const ActuatorConfig cfg(10000, 10000, 0.02f, 1000, 250, 500, 0.8f);
    settle_at(a, 0.0f, cfg);
    settle_at(a, 0.6f, cfg);
    settle_at(a, 0.2f, cfg);
    REQUIRE(a.redatum_due(cfg));

    INFO("a full open seats on the far stop, which datums just as well");
    a.step(1.0f, cfg, 50);
    CHECK(a.open_cmd());
    settle_at(a, 1.0f, cfg);
    CHECK(a.state() == Actuator::ST_OPEN);
    CHECK(a.drift() == doctest::Approx(0.0f).epsilon(0.05));
}

TEST_CASE("Actuator: the allowance can be switched off") {
    Actuator a;
    const ActuatorConfig cfg(10000, 10000, 0.02f, 1000, 250, 500, /*redatum*/ 0.0f);
    settle_at(a, 0.0f, cfg);
    for (int i = 0; i < 6; i++) settle_at(a, (i % 2) ? 0.7f : 0.3f, cfg);
    CHECK(a.drift() > 2.0f);
    CHECK_FALSE(a.redatum_due(cfg));
    INFO("and it keeps taking the short way, never detouring");
    a.step(0.5f, cfg, 50);
    CHECK(a.close_cmd());
}

}  // namespace actuator_drift
