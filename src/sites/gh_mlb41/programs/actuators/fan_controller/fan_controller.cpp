/**
 * @file fan_controller.cpp
 * @brief HAND-WRITTEN - the body of the `fans` POU.
 */

#include "fan_controller.h"

namespace Site {

float FanController::_demand() const {
    // An unknown demand is NOT "nothing to do". Either ladder invalidates its
    // own when it cannot see what it is about, and still air is itself a rot
    // risk - so an unknown one contributes these fans' OWN lowest band, which
    // is their safe state, rather than zero.
    const float floor    = low_dem.get();
    const float strat    = strat_dem.valid() ? strat_dem.get() : floor;
    const float cooling  = cool_dem.valid() ? cool_dem.get() : floor;
    // A cycle in progress wants air moving over the trays it has just wetted.
    const bool  cycling  = irr_state.get() != IRR_IDLE;
    const float watering = cycling ? irr_dem.get() : 0.0f;

    // The day's two breezes. At dusk it dries the leaves before the stomata
    // open - these plants breathe at night, and a wet leaf cannot - and at dawn
    // it takes the night's condensation off them. A FLOOR like the others: a
    // cooling demand above it still takes the fans higher.
    const DayPhase now  = (DayPhase)phase.get();
    const bool breezing = (now == PHASE_DAWN_BREEZE ||
                           now == PHASE_DUSK_BREEZE);
    const float breeze   = breezing ? breeze_dem.get() : 0.0f;

    return Plc::max4(strat, cooling, watering, breeze);
}

uint8_t FanController::_band(float demand) {
    // Three rungs, each engaging at its own threshold and released a little
    // under it, which is what keeps a demand sitting on an edge from stepping
    // the relays. The block is the same one the destratification ladder uses.
    return steps(demand, low_dem.get(), med_dem.get(), high_dem.get(),
                 dem_hyst.get());
}

void FanController::_drive(uint8_t speed) {
    for (uint8_t i = 0; i < N_SPEED1; i++) {
        speed1[i].set(speed == 1);
        speed2[i].set(speed == 2);
        speed3[i].set(speed == 3);
    }
    band.set(speed);
}

void FanController::run(ProgramContext& c) {
    // MECHANICAL, so it is resolved before any demand and before the safe
    // state: the fabric is travelling through the air these blow into.
    if (c.first_scan) steps.reset();
    if (screen_moving.get()) {
        steps.reset();
        _drive(0);
        return;
    }
    if (inhibit_requested()) {
        // Speed 1, not off, and the table's safe column says the same: still
        // air is itself a rot risk, so the fans are one of the few things that
        // keep working once everything else has stopped.
        steps.preset(1);
        _drive(1);
        return;
    }
    if (!strat_dem.valid() ||
        !cool_dem.valid()) degraded();
    _drive(_band(_demand()));
}

}  // namespace Site
