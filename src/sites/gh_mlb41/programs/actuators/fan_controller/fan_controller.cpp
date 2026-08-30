/**
 * @file fan_controller.cpp
 * @brief HAND-WRITTEN - the body of the `fans` POU.
 */

#include "fan_controller.h"

namespace Site {

float FanController::_demand() const {
    // Both demands declare the low band, so an unbelievable one already reads
    // as this floor: still air is a rot risk, and the fans keep turning.
    const float strat    = strat_dem.get();
    const float cooling  = cool_dem.get();
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
        speed1[i].set(speed == 1, true);
        speed2[i].set(speed == 2, true);
        speed3[i].set(speed == 3, true);
    }
    band.set(speed, true);
}

void FanController::run(ProgramContext& c) {
    if (c.first_scan) steps.reset();
    _drive(_band(_demand()));
}

}  // namespace Site
