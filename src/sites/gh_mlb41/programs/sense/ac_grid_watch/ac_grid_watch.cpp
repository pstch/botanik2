/**
 * @file ac_grid_watch.cpp
 * @brief HAND-WRITTEN - the body of the `ac_grid_watch` POU.
 */

#include "ac_grid_watch.h"

#include "../../../vars.h"

namespace Site {

void AcGridWatch::observe(ProgramContext& c) {
    if (c.first_scan) supply.reset();
    bool bad = false;
    if (!supply.step(bad)) degraded();
    fault.set(bad);
}

}  // namespace Site
