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
        demand.hold();
        window.hold();
        // Both gates SHUT, as they are whenever the crawlspace probe is out:
        // drawing air is a decision about which side of a difference is
        // cooler, and one end of it has gone. Written, not held - a gate is a
        // command, and this one is the safe answer rather than a stale guess.
        intake_ok.set(false);
        ground_ok.set(false);
        pid.reset();      // the integral is about a plant we stopped seeing
        faulted();
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
    demand.set(dem);
    // Where the ladder wants the windows, published as the one INTENDED
    // opening: the windows apply the interlocks to it, and an operator can see
    // what was asked for beside where each ram actually is.
    window.set(Plc::ladder(dem, win_start.get(), win_full.get()));
    _intake_gates(t_in, dem);
}

void CoolingController::_intake_gates(float t_in, float dem) {
    // Drawing air only moves the interior toward whatever the intake is. Both
    // gates are shut while the crawlspace probe is out, which costs a little
    // ventilation and cannot cost a plant: the windows and the fans are on
    // their own paths.
    const bool  ok = intake.valid();
    if (!ok) degraded();
    const float t_intake = intake.get();
    const float margin = temp_margin.get();
    const float hyst   = vent_hyst.get();

    // Each gate opens AT its margin and closes a band under it, so a difference
    // sitting on the margin - which is what two converging temperatures do -
    // holds the gate where it is instead of flipping it every beat.
    intake_ok.set(ok && cool_gate(t_in - t_intake, margin - hyst, margin));

    // The mirror, and the only place the negative half of the ladder does
    // anything: cold enough to be asking for heat, and ground warm enough to
    // be worth pulling it out of. Both halves are banded - the demand is a PID
    // output and it crosses a rung as slowly as it likes.
    const float rung   = ground_dem.get();
    const bool  asking = ground_band(-dem, -rung, -rung + dem_hyst.get());
    ground_ok.set(ok && asking &&
                  ground_gate(t_intake - t_in, margin - hyst, margin));
}

}  // namespace Site
