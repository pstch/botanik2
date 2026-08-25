/**
 * @file target_temp_controller.cpp
 * @brief HAND-WRITTEN - the body of the `target_temp` POU.
 */

#include "target_temp_controller.h"

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

    // A phase boundary is a STEP, and every consumer of the target answers a
    // gap proportionally: unfiltered, dawn moves the windows, the fans and the
    // screen in one beat. Adopted whole on the first scan - easing up from
    // whatever the filter held before an outage would call for heat the house
    // does not need.
    if (c.first_scan) smooth.preset(held);
    const float eased = smooth(held, (float)tau.get(), c.dt_ms);

    // The clamp is LAST, and what keeps an aggressively-set offset from pushing
    // the greenhouse somewhere dangerous, whatever the phase asked for: a rail
    // an operator has just moved has to bite now, not one time constant later.
    // The whole ladder hangs off the phase, so a phase nobody can place makes
    // this a target for a time of day we are guessing at.
    target.set(Plc::limit(eased, t_min.get(), t_max.get()), phase.valid());
}

}  // namespace Site
