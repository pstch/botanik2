/**
 * @file humidity_controller.cpp
 * @brief HAND-WRITTEN - the body of the `humidity` POU.
 */

#include "humidity_controller.h"

#include "../../../limits.h"
#include "../../../vars.h"

namespace Site {

/** @brief How long the margin must stay above the limit before humidifying is
 *  allowed again. In TIME, because the limit it releases against is already a
 *  parameter: what this adds is that a margin hovering on that limit does not
 *  let the skirt back on every other scan. */
static const uint32_t ROT_RELEASE_MS = 300000;

void HumidityController::observe(ProgramContext& c) {
    if (!dp_margin.valid()) {
        // No margin, no verdict on it - and the guard fails toward acting:
        // ventilating costs energy, not ventilating costs the collection.
        degraded();
        vent_dem.set(1.0f);
        lockout.set(lockout_for(true, ROT_RELEASE_MS, c.dt_ms));
        return;
    }

    const float margin = dp_margin.get();
    const float limit  = Plc::limit(rot_limit.get(), ROT_MARGIN_DEGC, 10.0f);

    // A FALLING ramp, negated so the one ladder function serves both
    // directions: nothing at the limit, everything at the dew point itself.
    // The span IS the operator's setting - cli_tooHumid_degC sits on its own
    // floor by default, so a ramp to that floor would be a step.
    vent_dem.set(Plc::ladder(-margin, -limit, 0.0f));

    // An OVERRUN, not a dwell: the lockout follows the condition up at once and
    // outlives it, because humidifying into air that has only just dried out is
    // what puts the margin back where it was. Nothing is locked out at boot.
    lockout.set(lockout_for(rot.get(), ROT_RELEASE_MS, c.dt_ms));
    _dry_gate(c);
}

void HumidityController::_dry_gate(ProgramContext& c) {
    // DEW POINTS, not humidities: air at 90% and 5 degC is drier than air at
    // 60% and 25, and swapping one for the other is what ventilating does.
    const bool have = in_t.valid() &&
                      in_rh.valid() &&
                      ext_t.valid() &&
                      ext_rh.valid();
    if (!have) {
        // Shut, and said: opening on a guess would mean drawing in air that may
        // be wetter than what is already here, which is the one thing this
        // gate exists to prevent. The rot ladder is not gated and still vents.
        degraded();
        dry_ok.set(false);
        return;
    }
    const float dp_in   = Plc::dew_point(in_t.get(), in_rh.get());
    const float outside = Plc::dew_point(ext_t.get(), ext_rh.get());
    const float margin  = dry_margin.get();
    const float hyst    = vent_hyst.get();
    // Banded, like the temperature gates: a morning where the two dew points
    // meet would otherwise open and shut the intake every beat.
    dry_ok.set(dry_gate_band(dp_in - outside, margin - hyst, margin));
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
                                     humid_full.get()));
}

}  // namespace Site
