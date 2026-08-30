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
    // band, and drives both the verdict and the alarm off the rows it was
    // handed. Whether the meter ANSWERED is its return, and nothing here asks.
    (void)supply();
}

}  // namespace Site
