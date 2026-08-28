/**
 * @file intake_dry_gate.cpp
 * @brief HAND-WRITTEN - the body of the `intake_dry_gate` POU.
 */

#include "intake_dry_gate.h"

namespace Site {

void IntakeDryGate::run(ProgramContext& c) {
    (void)c;

    // DEW POINTS, not humidities: air at 90% and 5 degC is drier than air at
    // 60% and 25, and swapping one for the other is what ventilating does.
    const bool have = in_t.valid() &&
                      in_rh.valid() &&
                      ext_t.valid() &&
                      ext_rh.valid();
    if (!have) {
        // Shut, and said: opening on a guess would mean drawing in air that may
        // be wetter than what is already here, which is the one thing this
        // gate exists to prevent. The rot ladder is a program of its own and
        // still vents.
        dry_ok.set(false, true);
        return;
    }
    const float dp_in   = Plc::dew_point(in_t.get(), in_rh.get());
    const float outside = Plc::dew_point(ext_t.get(), ext_rh.get());
    const float margin  = dry_margin.get();
    const float hyst    = vent_hyst.get();
    // Banded, like the temperature gates: a morning where the two dew points
    // meet would otherwise open and shut the intake every beat.
    dry_ok.set(dry_gate_band(dp_in - outside, margin - hyst, margin), true);
}

}  // namespace Site
