/**
 * @file cooling_controller.cpp
 * @brief HAND-WRITTEN - the body of the `cooling` POU.
 */

#include "cooling_controller.h"

namespace Site {

void CoolingController::run(ProgramContext& c) {
    if (c.first_scan) pid.reset();   // the integral is a claim about the past

    const bool measured = inside_qlt.get();
    if (!measured) {
        // Nobody is measuring the interior, so the ladder runs on the reading
        // the house last had: temp_rh_zone is ST and writes the row every
        // scan, so it holds a number rather than going invalid. It still
        // produces a demand and still publishes it - every actuator wants a
        // number to fall back from - and the quality is what says the number
        // is not about this greenhouse. The intake gates read cli_coolDem_qlt
        // to know this law stopped having an opinion.
        // The integral is a claim about a plant we stopped seeing, and an error
        // computed against a substituted reading is not an error this house
        // ever had. Reset while it lasts, so nothing winds up on a number
        // nobody measured and the demand stays proportional-only until a probe
        // comes back.
        pid.reset();
    }
    // The house's own row, which is the hotter of the two levels: the spec's
    // worst case for cooling, derived once where the probes are voted rather
    // than again here.
    const float t_in = inside.get();

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
    demand.set(dem, measured);
    // Where the ladder wants the windows, published as the one INTENDED
    // opening: the windows apply the interlocks to it, and an operator can see
    // what was asked for beside where each ram actually is.
    window.set(Plc::ladder(dem, win_start.get(), win_full.get()), measured);
    demand_qlt.set(measured, true);
}

}  // namespace Site
