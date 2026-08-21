/**
 * @file roof_lockout.cpp
 * @brief HAND-WRITTEN - the body of the `roof_lockout` POU.
 */

#include "roof_lockout.gen.h"

namespace Site {

void RoofLockout::run(ProgramContext& c) {
    // A weather station that has stopped answering is not "no rain". Both rows
    // fall back to 0, which reads exactly like fine weather, so the QUALITY is
    // what decides and an unmeasured sky shuts the roof.
    const bool blind = !rain_mmph.valid() || !wind_mps.valid();
    if (blind) degraded();

    // An OVERRUN on the RAIN: the roof stays shut for the declared time after
    // the last drop, because a shower that has just stopped is about to be one
    // that has not. Being BLIND is not a shower - it is a live interlock that
    // holds while it lasts and lets go the moment the station answers, where
    // feeding it the overrun cost the plant a quarter of an hour of ventilation
    // after every restart.
    const bool raining =
        rain_for(rain_mmph.get() > 0.0f, 1000u * rain_hold_s.get(),
                 c.dt_ms) || blind;

    // The GUST, not the average: what tears a window off its ram is the peak,
    // and the station's own window is the hysteresis - no dwell needed here.
    const float gust = gust_mps.get();
    const float mean = wind_mps.get();
    const bool blowing =
        blind || ((gust > mean ? gust : mean) >= wind_shut.get());

    rain_lock.set(raining);
    wind_lock.set(blowing);
    // READ BACK, never the local verdicts: each row is what an operator can
    // force, and a lockout the law then ignored would be a lie on the page.
    window_lock.set(rain_lock.get() || wind_lock.get() || supply_lock.get());
}

}  // namespace Site
