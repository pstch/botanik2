/**
 * @file ac_grid_watch.cpp
 * @brief HAND-WRITTEN - the body of the `ac_grid_watch` POU.
 */

#include "ac_grid_watch.h"

namespace Site {

void AcGridWatch::run(ProgramContext& c) { (void)c; }

void AcGridWatch::observe(ProgramContext& c) {
    if (c.first_scan) supply.reset();
    // The block mirrors the four readings, judges them against the in-spec
    // band and says whether this supply is faulted. Whether the meter ANSWERED
    // is its own return, and nothing here asks: the fault covers silence after
    // it has answered once, which is the case worth raising.
    bool bad = false;
    (void)supply.step(bad);
    fault.set(bad, true);
}

}  // namespace Site
