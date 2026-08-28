/**
 * @file sun_watch.cpp
 * @brief HAND-WRITTEN - the body of the `sun_watch` POU.
 */

#include "sun_watch.h"

namespace Site {

void SunWatch::run(ProgramContext& c) { (void)c; }

static int16_t _minutes(int64_t v) {
    if (v < -1440) return -1440;
    if (v >  1440) return  1440;
    return (int16_t)v;
}

void SunWatch::observe(ProgramContext& c) {
    (void)c;
    // No location, or no wall clock yet - every boot passes through the second
    // for as long as NTP takes. The offsets ARE the sun, so they hold rather
    // than publishing a day nobody has computed. A tracker that is not there at
    // all was INVALID at bind() and this never runs.
    if (!_sun->ready()) {
        from_rise.hold();
        from_set.hold();
        elevation.hold();
        return;
    }

    // Signed minutes either side of the two events, which is the form every
    // boundary in the phase table is declared in. Both belong to the same solar
    // day, so one of them is always in the past and the other in the future.
    const int64_t now = (int64_t)HAL::Clock::wall_ms();
    from_rise.set(_minutes((now - (int64_t)_sun->sunrise_ms()) / 60000), true);
    from_set.set(_minutes((now - (int64_t)_sun->sunset_ms()) / 60000), true);

    // Degrees, because that is what a person reads. The math is in radians.
    elevation.set((float)(_sun->elevation() * 57.2957795), true);
}

}  // namespace Site
