/**
 * @file roof_lockout.cpp
 * @brief HAND-WRITTEN - the body of the `roof_lockout` POU.
 */

#include "roof_lockout.h"

namespace Site {

void RoofLockout::run(ProgramContext& c) {
    // A weather station that has stopped answering is not "no rain". Both rows
    // fall back to 0, which reads exactly like fine weather, so the QUALITY is
    // what decides and an unmeasured sky shuts the roof.
    const bool blind = !rain_mmph.valid() || !wind_mps.valid();
    // Wider than blind: the gust and the supply interlock do not shut the roof
    // by themselves - the gust falls back to the mean, which is still measured
    // - but a law running on either unread is one saying so.

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
    const float w    = (gust > mean) ? gust : mean;
    const bool blowing = blind || (w >= shut_end.get());

    rain_lock.set(raining, true);
    wind_lock.set(blowing, true);
    // READ BACK, never the local verdicts: each row is what an operator can
    // force, and a lockout the law then ignored would be a lie on the page.
    window_lock.set(rain_lock.get() || wind_lock.get() || supply_lock.get(), true);

    // How far the roof may open, which is not a yes or a no. Below shut_begin
    // the wind has no say; from there to shut_end the ceiling comes down a
    // straight line from shut_max to nothing, so the roof is already almost
    // shut by the time the lockout above takes it the rest of the way. Off the
    // ROW again, so a forced lockout still means what it says.
    float ceiling = 1.0f;
    const float begin = shut_begin.get(), end = shut_end.get();
    if (w >= begin && end > begin)
        ceiling = shut_max.get() * (1.0f - (w - begin) / (end - begin));
    cap.set(window_lock.get() ? 0.0f : Plc::limit(ceiling, 0.0f, 1.0f), true);
}

}  // namespace Site
