/**
 * @file test_pid.cpp
 * @brief The PID regulator (scan/blocks/pid.h). Driven by an explicit dt, so
 *        the loop runs as fast as the test wants and needs no clock.
 */
#include "doctest.h"

#include "libbotanik/plc/blocks/pid.h"

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
