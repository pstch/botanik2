/**
 * @file ac_local_watch.cpp
 * @brief HAND-WRITTEN - the body of the `ac_local_watch` POU.
 */

#include "ac_local_watch.h"

namespace Site {

void AcLocalWatch::run(ProgramContext& c) { (void)c; }

void AcLocalWatch::observe(ProgramContext& c) {
    if (c.first_scan) { supply.reset(); cranking.reset(); }
    const bool answered = supply();

    // The generator only owes an answer once the grid has gone. With the grid
    // up, an unhealthy ATS output is a transfer-switch fault (alarm_ac_local
    // above) and the set was never asked to start.
    const bool waiting = !grid_ok.get() && !answered;
    gen_fault.set(cranking(waiting, 1000u * start_s.get(), c.dt_ms), true);
}

}  // namespace Site
