/**
 * @file cooling_controller.cpp
 * @brief HAND-WRITTEN - the body of the `cooling` POU.
 */

#include "cooling_controller.h"

namespace Site {

void CoolingController::run(ProgramContext& c) {
    if (c.first_scan) pid.reset();   // the integral is a claim about the past

    const bool hi_ok = ceiling.valid();
    const bool lo_ok = floor.valid();
    if (!inside.valid()) {
        // No interior temperature at all. A PID handed a defaulted reading
        // would see 0 degC against the target and call for full HEAT - into a
        // greenhouse nobody can see. Say the demand is unknown instead, and let
        // each actuator go to its own safe state. The window demand goes with
        // it: an opening derived from a temperature nobody has is not an
        // intention, and the windows have their own safe position.
        // HELD, not dropped: the last demand this law made is the best guess
        // at what the plant needs, and every actuator reads the quality and
        // goes to its own safe state anyway. A zero here would read as "nothing
        // is wanted", which is a different and much more reassuring lie.
        degraded();
        // HELD, and the quality is what the intake gates read to know this law
        // stopped having an opinion: they shut on it, which is their own safe
        // answer rather than one this program reaches across and gives them.
        demand.hold();
        window.hold();
        pid.reset();      // the integral is about a plant we stopped seeing
        return;
    }
    // The house's own row, which is the hotter of the two levels: the spec's
    // worst case for cooling, derived once where the probes are voted rather
    // than again here.
    const float t_in = inside.get();
    if (!hi_ok || !lo_ok) degraded();

    // The parameters are in the TIME form an operator tunes in (Ti, Td in
    // seconds); PidTuning takes gains. Ti of 0 means no integral action rather
    // than an infinite one, which is what a divide would give.
    const float p_gain = kp.get();
    const float ti_s   = ti.get();
    const float td_s   = td.get();
    const Plc::PidTuning t(p_gain,
                           (ti_s > 0.0f) ? (p_gain / ti_s) : 0.0f,
                           p_gain * td_s,
                           -1.0f, 1.0f,
                           /* d_tau_s */ 0.0f,
                           /* reverse */ true);

    // Reverse-acting: the demand RISES as the reading does, so above the target
    // it is positive (cooling) and below it negative (the intractors drawing
    // warm floor air, and lower still the thermal screen).
    const float dem = pid(target.get(), t_in, t, c.dt_ms);
    demand.set(dem, true);
    // Where the ladder wants the windows, published as the one INTENDED
    // opening: the windows apply the interlocks to it, and an operator can see
    // what was asked for beside where each ram actually is.
    window.set(Plc::ladder(dem, win_start.get(), win_full.get()), true);
}

}  // namespace Site
