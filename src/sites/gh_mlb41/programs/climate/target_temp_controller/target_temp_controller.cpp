/**
 * @file target_temp_controller.cpp
 * @brief HAND-WRITTEN - the body of the `target_temp` POU.
 */

#include "target_temp_controller.h"

#include "../../../vars.h"

namespace Site {

void TargetTempController::run(ProgramContext& c) {
    const DayPhase now = (DayPhase)phase.get();
    float held = cool_set.get();

    // A bank phase shifts the base only when yesterday justified it: pre-cool
    // before dawn after a hot day, bank heat at dusk after a cold night. In
    // force but not armed, the phase runs at the ordinary target.
    if (now == PHASE_COLD_BANK && cold_arm.get())
        held -= cold_bank_ofs.get();
    else if (now == PHASE_HEAT_BANK && heat_arm.get())
        held += heat_bank_ofs.get();

    // The clamp is what keeps an aggressively-set offset from pushing the
    // greenhouse somewhere dangerous, whatever the phase asked for.
    target.set(Plc::limit(held, t_min.get(), t_max.get()));
}

}  // namespace Site
