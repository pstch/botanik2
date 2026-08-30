/**
 * @file wx_sensors.cpp
 * @brief HAND-WRITTEN - the body of the `wx_sensors` POU.
 */

#include "wx_sensors.h"

namespace Site {

void WxSensors::run(ProgramContext& c) {
    if (c.first_scan) stations.reset();

    // A station short is half the weather gone, and alarm_wx_station is what
    // says so - with its own dwell, since one missed scan of RS-485 is the bus
    // being busy rather than a station that has gone.
    stations(c.dt_ms);
}

}  // namespace Site
