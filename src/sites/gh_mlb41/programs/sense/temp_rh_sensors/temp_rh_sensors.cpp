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
    ceiling(c.dt_ms);
    floor(c.dt_ms);
    crawlspace(c.dt_ms);
    exterior(c.dt_ms);

    // The two interior levels publish whether they MEASURED, because the
    // worst-case vote downstream is among measured levels only and a level
    // holding an old reading must not speak for the house. The other two
    // zones have no reader that needs the distinction.
    hi_qlt.set(ceiling.valid(), true);
    lo_qlt.set(floor.valid(), true);
}

}  // namespace Site
