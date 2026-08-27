/**
 * @file target_temp_controller.cpp
 * @brief HAND-WRITTEN - the body of the `target_temp` POU.
 */

#include "target_temp_controller.h"

namespace Site {

void TargetTempController::run(ProgramContext& c) {
    // The chain STOPS here. Every cooling stage keys off this row, so a target
    // nobody could place must not be published as one nobody can use: the base
    // setpoint is a proper value on its own - it is what the plant runs at for
    // most of the day - and the phase only ever shifts it. So an unplaced phase
    // takes the base unshifted, the law says it is running short, and the row
    // stays believable for everything downstream.
    const bool placed = phase.valid();
    if (!placed) degraded();
    const DayPhase now = (DayPhase)phase.get();
    float held = cool_set.get();

    // A bank phase shifts the base only when yesterday justified it: pre-cool
    // before dawn after a hot day, bank heat at dusk after a cold night. In
    // force but not armed, the phase runs at the ordinary target.
    if (placed && now == PHASE_COLD_BANK && cold_arm.get())
        held -= cold_bank_ofs.get();
    else if (placed && now == PHASE_HEAT_BANK && heat_arm.get())
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
    target.set(Plc::limit(eased, t_min.get(), t_max.get()), true);
}

}  // namespace Site
