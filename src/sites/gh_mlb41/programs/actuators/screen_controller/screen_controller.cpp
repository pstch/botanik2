/**
 * @file screen_controller.cpp
 * @brief HAND-WRITTEN - the body of the `screen` POU.
 */

#include "screen_controller.h"

namespace Site {

static bool _allows(DayPhase phase, bool deploy) {
    // Deploying belongs to the half of the day that is closing down, retracting
    // to the half that is opening up. An hour of cold at midday therefore never
    // shuts the fabric against the sun, and a mild night never opens it and
    // gives back the heat it was holding.
    return deploy ? (phase == PHASE_NIGHT || phase == PHASE_DUSK_BREEZE ||
                     phase == PHASE_HEAT_BANK)
                  : (phase == PHASE_DAY || phase == PHASE_DAWN_BREEZE ||
                     phase == PHASE_COLD_BANK);
}

Plc::ActuatorConfig ScreenController::_mechanics() const {
    const uint32_t travel = 1000u * (uint32_t)travel_s.get();
    const uint32_t tmo    = 1000u * (uint32_t)travel_tmo_s.get();
    // Capped at the actuator's own SAFE time, which is the datasheet's number
    // where the travel is the operator's: this traverse is what the fans are
    // cut for and what a parking supervisor waits on, so a mis-set travel must
    // not hold both for as long as it says.
    const uint32_t stroke = (travel < tmo) ? travel : tmo;
    // No seating run and no reversal dead time: the coil is HELD rather than
    // driven, so the fabric starts back the moment the command does, and the
    // stroke is the whole of the traverse.
    return Plc::ActuatorConfig(stroke, stroke, 0.02f, /* seat_ms */ 0u,
                               /* min_run_ms */ 250u, /* reverse_ms */ 0u);
}

void ScreenController::run(ProgramContext& c) {
    if (c.first_scan) {
        // DEPLOYED, and known to be: this coil is held rather than driven, and
        // it deploys on the de-energised one - so a controller that was not
        // driving anything left the fabric out. Presetting says that, where
        // UNKNOWN would send it on a full datum stroke to learn what the
        // wiring already guarantees.
        fabric.preset(1.0f);
    }

    // An unknown cooling demand DEPLOYS it: that is this one's safe state, and
    // a greenhouse nobody can see loses heat through the roof all night. Free,
    // too - the fabric deploys on the de-energised coil.
    const bool dem_ok = cool_dem.valid();

    // No dead band on this one, and none is needed: the permission table above
    // makes every phase ONE-DIRECTIONAL, so a demand sitting on the rung can
    // move the fabric at most once per phase whatever it does afterwards.
    const bool cold = !dem_ok || cool_dem.get() <= deploy_dem.get();
    // The phase gives PERMISSION, the demand asks for the move - and nothing
    // else does. Denied, the command STANDS, read back off the row an operator
    // can force rather than off a copy of it here.
    const DayPhase now = (DayPhase)phase.get();
    screen.set(_allows(now, cold) ? cold : screen.get(), true);

    // Stepped HERE, so a screen parked on the way to its safe position still
    // reports itself as moving and the fans stay cut. The target is the ROW,
    // which is what this law asked for and what an operator may have forced.
    fabric.step(screen.get() ? 1.0f : 0.0f, _mechanics(), c.dt_ms);
    moving.set(fabric.moving(), true);
}


}  // namespace Site
