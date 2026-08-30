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

    // The roof interlock reads both of these, and both rows declare 0, which
    // reads as fine weather. So the companions are what let it tell a calm
    // dry sky from one nobody is watching.
    wind_qlt.set(stations.wind_ok(), true);
    rain_qlt.set(stations.rain_ok(), true);
}

}  // namespace Site
