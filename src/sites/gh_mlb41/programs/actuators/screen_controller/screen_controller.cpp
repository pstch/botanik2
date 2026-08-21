/**
 * @file screen_controller.cpp
 * @brief HAND-WRITTEN - the body of the `screen` POU.
 */

#include "screen_controller.h"

#include "../../../vars.h"

namespace Site {

bool ScreenController::_allows(DayPhase phase, bool deploy) {
    // Deploying belongs to the half of the day that is closing down, retracting
    // to the half that is opening up. An hour of cold at midday therefore never
    // shuts the fabric against the sun, and a mild night never opens it and
    // gives back the heat it was holding.
    return deploy ? (phase == PHASE_NIGHT || phase == PHASE_DUSK_BREEZE ||
                     phase == PHASE_HEAT_BANK)
                  : (phase == PHASE_DAY || phase == PHASE_DAWN_BREEZE ||
                     phase == PHASE_COLD_BANK);
}

void ScreenController::_command(bool deploy) {
    // A CHANGE starts the traverse; observe() times it. Both ways, because the
    // fabric is in the fans' way whichever direction it travels.
    _command_changed = !_seeded || (deploy != _deployed);
    _deployed = deploy;
    _seeded   = true;
    screen.set(deploy);
}

void ScreenController::run(ProgramContext& c) {
    if (c.first_scan) {
        // Where the fabric actually is, nothing here knows. Command it afresh
        // and let the traverse run, rather than trusting a position from
        // before control was withdrawn.
        _seeded = false;
    }
    if (inhibit_requested()) {
        // Safe is DEPLOYED. A greenhouse nobody is controlling loses heat
        // through the roof all night, and the fabric is what stops it. Free,
        // too: the coil is de-energised there.
        _command(true);
        if (_command_changed || travelling.q()) parking();
        return;
    }

    // An unknown cooling demand DEPLOYS it: that is this one's safe state, and
    // it should never be reached - the interior is measured by three probes at
    // each of two levels precisely so that it is not.
    const bool dem_ok = cool_dem.valid();
    if (!dem_ok) degraded();

    // No dead band on this one, and none is needed: the permission table above
    // makes every phase ONE-DIRECTIONAL, so a demand sitting on the rung can
    // move the fabric at most once per phase whatever it does afterwards.
    const bool cold = !dem_ok || cool_dem.get() <= deploy_dem.get();
    // The phase gives PERMISSION, the demand asks for the move - and nothing
    // else does. A phase commanding an actuator is the one thing the design
    // rules out: a phase sets the target and enables a ladder, and the ladder
    // decides what moves. A cold night reaches the fabric by driving the
    // cooling demand under cli_thermalScreenDem like any other cold hour.
    const DayPhase now = (DayPhase)phase.get();
    if (_allows(now, cold)) _command(cold);
    // A command that has just CHANGED is a traverse about to start, and
    // observe() has not timed it yet: parked is neither.
    if (_command_changed || travelling.q()) parking();
}

void ScreenController::observe(ProgramContext& c) {
    // Timed HERE, so a screen parked on the way to its safe position still
    // reports itself as moving and the fans stay cut.
    //
    // The pulse is capped at the actuator's own SAFE time, which is the
    // datasheet's number where the travel is the operator's: this traverse is
    // what the fans are cut for and what a parking supervisor waits on, so a
    // mis-set travel would hold both for as long as it said.
    const uint32_t travel = 1000u * (uint32_t)travel_s.get();
    const uint32_t safe   = 1000u * (uint32_t)travel_tmo_s.get();

    moving.set(travelling(moved(_command_changed),
                              (travel < safe) ? travel : safe, c.dt_ms));
    _command_changed = false;
}

}  // namespace Site
