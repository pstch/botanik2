/**
 * @file wx_sensors.cpp
 * @brief HAND-WRITTEN - the body of the `wx_sensors` POU.
 */

#include "wx_sensors.h"

namespace Site {

void WxSensors::on_bind() {
    stations.attach(wind, gust, rain, irrad,
                    wind_out, gust_out, rain_out, irrad_out, fault,
                    0.001f * (float)tau_ms.get());
}

void WxSensors::run(ProgramContext& c) {
    if (c.first_scan) stations.reset();

    // Two stations, so a station short is half the weather gone - and the roof
    // interlock is downstream of every one of these rows. Said here rather
    // than left to the alarm alone: the alarm has a dwell, and a supervisor
    // reading one health code should not have to wait it out.
    const uint8_t used = stations.step(c.dt_ms);
    if (used < 2) degraded();
}

}  // namespace Site
