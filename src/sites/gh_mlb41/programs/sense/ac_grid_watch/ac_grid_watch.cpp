/**
 * @file ac_grid_watch.cpp
 * @brief HAND-WRITTEN - the body of the `ac_grid_watch` POU.
 */

#include "ac_grid_watch.h"

namespace Site {

void AcGridWatch::on_bind() { supply.attach(*this); }

void AcGridWatch::run(ProgramContext& c) { (void)c; }

void AcGridWatch::observe(ProgramContext& c) {
    if (c.first_scan) supply.reset();
    bool bad = false;
    fault.set(bad, true);
}

}  // namespace Site
