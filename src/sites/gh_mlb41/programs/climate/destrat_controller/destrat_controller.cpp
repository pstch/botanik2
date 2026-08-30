/**
 * @file destrat_controller.cpp
 * @brief HAND-WRITTEN - the body of the `destrat` POU.
 */

#include "destrat_controller.h"

namespace Site {

void DestratController::run(ProgramContext& c) {
    if (c.first_scan) bands.reset();

    // Three fan speeds, so three rungs on one staircase, each engaging at its
    // own gap and released by the common hysteresis. All three, not two with
    // the middle one decided by a bare comparison: a speed change needs the gap
    // to have really moved, whichever pair of speeds it is between.
    const uint8_t speed = bands(strat.get(), low.get(), med.get(), high.get(),
                                hyst.get());

    // The demand the arbitration reads is the FANS' OWN rung for that speed.
    // Any other number would have to be translated twice, and the two ladders
    // would drift apart the first time one of them was retuned.
    demand.set(Plc::mux4(speed, 0.0f, fan_low.get(), fan_med.get(),
                         fan_high.get()), true);
}

}  // namespace Site
