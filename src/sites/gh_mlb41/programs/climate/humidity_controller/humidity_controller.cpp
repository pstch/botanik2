/**
 * @file humidity_controller.cpp
 * @brief HAND-WRITTEN - the body of the `humidity` POU.
 */

#include "humidity_controller.h"

namespace Site {

void HumidityController::observe(ProgramContext& c) {
    if (!dp_margin.valid()) {
        // No margin, no verdict on it - and the guard fails toward acting:
        // ventilating costs energy, not ventilating costs the collection.
        degraded();
        vent_dem.set(1.0f, true);
        lockout.set(lockout_for(true, 1000u * rot_release_s.get(), c.dt_ms), true);
        return;
    }

    const float margin = dp_margin.get();
    const float limit  = rot_limit.get();

    // A FALLING ramp, negated so the one ladder function serves both
    // directions: nothing at the limit, everything at the dew point itself.
    // The span IS the operator's setting - cli_rotMargin_degC sits on its own
    // floor by default, so a ramp to that floor would be a step.
    vent_dem.set(Plc::ladder(-margin, -limit, 0.0f), true);

    // An OVERRUN, not a dwell: the lockout follows the condition up at once and
    // outlives it, because humidifying into air that has only just dried out is
    // what puts the margin back where it was. Nothing is locked out at boot.
    lockout.set(lockout_for(rot.get(), 1000u * rot_release_s.get(), c.dt_ms), true);
}

void HumidityController::run(ProgramContext& c) {
    (void)c;
    if (!dp_margin.valid()) {
        // The ladder is a function of the margin, and its default is a number
        // that means "saturated": published as a demand it would call for full
        // dehumidification on a reading nobody has. The rot guard in observe()
        // still ventilates - that one is a CHOICE, not a derivation.
        demand.hold();
        return;
    }

    // Four bands and a dead zone, and the signed ladder IS that shape: below
    // dehumStart the margin is closing on condensation (negative, dehumidify);
    // above humidStart the air is drier than the plants want (positive); in
    // between nothing moves.
    demand.set(Plc::signed_ladder(dp_margin.get(),
                                     dehum_full.get(),
                                     dehum_start.get(),
                                     humid_start.get(),
                                     humid_full.get()), true);
}

}  // namespace Site
