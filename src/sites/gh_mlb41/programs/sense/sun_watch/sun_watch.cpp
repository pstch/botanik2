/**
 * @file sun_watch.cpp
 * @brief HAND-WRITTEN - the body of the `sun_watch` POU.
 */

#include "sun_watch.h"

#include "../../../vars.h"

namespace Site {

int16_t SunWatch::_minutes(int64_t v) {
    if (v < -1440) return -1440;
    if (v >  1440) return  1440;
    return (int16_t)v;
}

void SunWatch::observe(ProgramContext& c) {
    if (_sun == nullptr) { faulted(); return; }
    (void)c;
    if (!_sun->ready()) {
        // No location, or no wall clock yet - every boot passes through here
        // for as long as NTP takes. The offsets ARE the sun, so they hold
        // rather than publishing a day nobody has computed.
        from_rise.hold();
        from_set.hold();
        elevation.hold();
        degraded();
        return;
    }

    // Signed minutes either side of the two events, which is the form every
    // boundary in the phase table is declared in. Both belong to the same solar
    // day, so one of them is always in the past and the other in the future.
    const int64_t now = (int64_t)HAL::Clock::wall_ms();
    from_rise.set(_minutes((now - (int64_t)_sun->sunrise_ms()) / 60000));
    from_set.set(_minutes((now - (int64_t)_sun->sunset_ms()) / 60000));

    // Degrees, because that is what a person reads. The math is in radians.
    elevation.set((float)(_sun->elevation() * 57.2957795));
}

}  // namespace Site
