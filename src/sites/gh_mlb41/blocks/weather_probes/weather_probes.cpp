/**
 * @file weather_probes.cpp
 * @brief HAND-WRITTEN - the weather block, out of line.
 */

#include "weather_probes.h"

namespace Site {

/** @brief How long a station may be silent before the alarm is raised. The
 *  same dwell a zone's quorum gets: a scan or two of RS-485 silence is the bus
 *  being busy, not a station that has gone. */
static const uint32_t WX_QUORUM_MS = 5000u;

WxVote WeatherProbes::_worse(const Vars::RefIn<float>* refs) {
    WxVote out = { 0.0f, 0 };
    for (uint8_t i = 0; i < 2; i++) {
        // An unbelievable station is EXCLUDED rather than voted on: it reads
        // its declared value, and a declared calm would be the lower of the
        // two and lose the vote every time - which is the one way a dead
        // station could open a roof.
        if (!refs[i].valid()) continue;
        const float v = refs[i].get();
        if (out.used == 0 || v > out.value) out.value = v;
        out.used++;
    }
    return out;
}

WxVote WeatherProbes::_mean(const Vars::RefIn<float>* refs) {
    WxVote out = { 0.0f, 0 };
    for (uint8_t i = 0; i < 2; i++) {
        if (!refs[i].valid()) continue;
        out.value += refs[i].get();
        out.used++;
    }
    if (out.used == 2) out.value *= 0.5f;
    return out;
}

void WeatherProbes::_publish(Vars::RefOut<float> row, Plc::Ewma& filter,
                            const WxVote& v, bool& seeded, float tau_s,
                            uint32_t dt_ms, float lo, float hi) {
    if (v.used == 0) {
        // Nothing believable left. The row keeps the last average the weather
        // could make and says it is not current; a law reading it through its
        // own reference gets the declared value instead. The filter keeps its
        // state and seeding is not re-armed, so a station coming back rejoins
        // the average it left.
        if (seeded) row.set(Plc::limit(filter.value(), lo, hi), false);
        else        row.invalidate();     // nothing has ever measured the sky
        return;
    }
    seeded = true;                      // the block starts AT its first reading
    row.set(Plc::limit(filter(v.value, tau_s, dt_ms), lo, hi), true);
}

uint8_t WeatherProbes::operator()(uint32_t dt_ms) {
    // Three of the four take the worse reading, because three of the four are
    // read by an interlock. Irradiance is not: nothing controls on it, so the
    // worst case has no meaning and the mean is the better estimate.
    const WxVote wind  = _worse(_r.wind);
    const WxVote gust  = _worse(_r.gust);
    const WxVote rain  = _worse(_r.rain);
    const WxVote sun   = _mean(_r.irrad);

    // The rails are the rows' own declared bands, so a filter that has been
    // fed a spike cannot publish past what the instrument can report.
    _publish(_r.wind_out,  _wind_f,  wind, _wind_seeded,  _tau_s, dt_ms, 0.0f,   80.0f);
    _publish(_r.gust_out,  _gust_f,  gust, _gust_seeded,  _tau_s, dt_ms, 0.0f,   80.0f);
    _publish(_r.rain_out,  _rain_f,  rain, _rain_seeded,  _tau_s, dt_ms, 0.0f,  200.0f);
    _publish(_r.irrad_out, _irrad_f, sun,  _irrad_seeded, _tau_s, dt_ms, 0.0f, 1500.0f);

    // The WORST-served quantity decides, so a station answering three of its
    // four registers is still a station short. One is enough to keep the plant
    // running and is worth sending somebody out for.
    uint8_t worst = wind.used;
    if (gust.used < worst) worst = gust.used;
    if (rain.used < worst) worst = rain.used;
    if (sun.used  < worst) worst = sun.used;

    _r.fault.set(_quorum_for(worst < 2, WX_QUORUM_MS, dt_ms), true);
    return worst;
}

}  // namespace Site
