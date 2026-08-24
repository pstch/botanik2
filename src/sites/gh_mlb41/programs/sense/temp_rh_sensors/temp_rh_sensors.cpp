/**
 * @file temp_rh_sensors.cpp
 * @brief HAND-WRITTEN - the body of the `temp_rh_sensors` POU.
 */

#include "temp_rh_sensors.h"

namespace Site {

void TempRHSensors::on_bind() {
    const float tau_s = 0.001f * (float)tau_ms.get();
    ceiling.attach(t_hi, h_hi, temp_hi, rh_hi, fault_hi, tau_s);
    floor.attach(t_lo, h_lo, temp_lo, rh_lo, fault_lo, tau_s);
    crawlspace.attach(t_cs, h_cs, temp_cs, rh_cs, fault_cs, tau_s);
    exterior.attach(t_ext, h_ext, temp_ext, rh_ext, fault_ext, tau_s);
}

void TempRHSensors::run(ProgramContext& c) {
    if (c.first_scan) {
        // The filters hold a claim about the recent past, and not being called
        // means we stopped watching. Seed them again rather than resuming an
        // average of readings from before the outage.
        ceiling.reset(); floor.reset();
        crawlspace.reset(); exterior.reset();
    }

    ceiling.step(c.dt_ms);
    floor.step(c.dt_ms);
    crawlspace.step(c.dt_ms);
    exterior.step(c.dt_ms);
}

}  // namespace Site
