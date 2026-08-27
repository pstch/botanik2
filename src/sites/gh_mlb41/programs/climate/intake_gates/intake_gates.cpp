/**
 * @file intake_gates.cpp
 * @brief HAND-WRITTEN - the body of the `intake_gates` POU.
 */

#include "intake_gates.h"

namespace Site {

void IntakeGates::run(ProgramContext& c) {
    (void)c;

    // The cooling ladder is the other half of this: a gate is a decision about
    // which side of a difference is cooler AND whether anything is being asked
    // for. The demand row is where the ladder says it stopped having an
    // opinion, so it is read for its QUALITY as much as its value.
    if (!inside.valid() || !cool_dem.valid()) {
        // Both SHUT, and the bands left where they are: a gate is a command,
        // and this is the safe answer rather than a stale guess. Stepping them
        // on a reading nobody has would advance a hysteresis that is supposed
        // to be a claim about a real difference.
        degraded();
        intake_ok.set(false, true);
        ground_ok.set(false, true);
        return;
    }

    // Drawing air only moves the interior toward whatever the intake is. Both
    // gates are shut while the crawlspace probe is out, which costs a little
    // ventilation and cannot cost a plant: the windows and the fans are on
    // their own paths.
    const bool  ok = intake.valid();
    if (!ok) degraded();
    const float t_in     = inside.get();
    const float dem      = cool_dem.get();
    const float t_intake = intake.get();
    const float margin   = temp_margin.get();
    const float hyst     = vent_hyst.get();

    // Each gate opens AT its margin and closes a band under it, so a difference
    // sitting on the margin - which is what two converging temperatures do -
    // holds the gate where it is instead of flipping it every beat.
    intake_ok.set(ok && cool_gate(t_in - t_intake, margin - hyst, margin), true);

    // The mirror, and the only place the negative half of the ladder does
    // anything: cold enough to be asking for heat, and ground warm enough to
    // be worth pulling it out of. Both halves are banded - the demand is a PID
    // output and it crosses a rung as slowly as it likes.
    const float rung   = ground_dem.get();
    const bool  asking = ground_band(-dem, -rung, -rung + dem_hyst.get());
    ground_ok.set(ok && asking &&
                  ground_gate(t_intake - t_in, margin - hyst, margin), true);
}

}  // namespace Site
