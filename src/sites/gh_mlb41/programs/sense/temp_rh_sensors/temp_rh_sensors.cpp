/**
 * @file temp_rh_sensors.cpp
 * @brief HAND-WRITTEN - the body of the `temp_rh_sensors` POU.
 */

#include "temp_rh_sensors.h"

namespace Site {

void TempRHSensors::run(ProgramContext& c) {
    if (c.first_scan) {
        // The filters hold a claim about the recent past, and not being called
        // means we stopped watching. Seed them again rather than resuming an
        // average of readings from before the outage.
        ceiling.reset(); floor.reset();
        crawlspace.reset(); exterior.reset();
    }

    // Each publishes its zone and raises its own quorum alarm; how many probes
    // it could use is that alarm's business, not this program's.
    ceiling.step(c.dt_ms);
    floor.step(c.dt_ms);
    crawlspace.step(c.dt_ms);
    exterior.step(c.dt_ms);
}

}  // namespace Site
