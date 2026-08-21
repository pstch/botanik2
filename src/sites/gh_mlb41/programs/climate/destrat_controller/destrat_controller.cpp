/**
 * @file destrat_controller.cpp
 * @brief HAND-WRITTEN - the body of the `destrat` POU.
 */

#include "destrat_controller.gen.h"

namespace Site {

void DestratController::run(ProgramContext& c) {
    if (c.first_scan) _bands.reset();

    // A band chosen from a gap nobody measured is a speed chosen from a
    // DEFAULT, and this row's default is 0 - which reads as "no stratification"
    // and stops the fans. Say the demand is unknown instead and let each
    // actuator decide what that means for it.
    if (!strat.valid()) {
        demand.hold();
        // The bands are a claim about where the gap has recently been, and we
        // stopped seeing it.
        _bands.reset();
        faulted();
        return;
    }

    // Three fan speeds, so three rungs on one staircase, each engaging at its
    // own gap and released by the common hysteresis. All three, not two with
    // the middle one decided by a bare comparison: a speed change needs the gap
    // to have really moved, whichever pair of speeds it is between.
    const float edge[3] = { low.get(), med.get(),
                            high.get() };
    const uint8_t speed = _bands(strat.get(), edge, hyst.get());

    // The demand the arbitration reads is the FANS' OWN rung for that speed.
    // Any other number would have to be translated twice, and the two ladders
    // would drift apart the first time one of them was retuned.
    const float rung[4] = { 0.0f, fan_low.get(),
                            fan_med.get(), fan_high.get() };
    demand.set(rung[speed]);
}

}  // namespace Site
