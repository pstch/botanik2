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

void WeatherProbes::attach(const Vars::RefIn<float>* wind,
                           const Vars::RefIn<float>* gust,
                           const Vars::RefIn<float>* rain,
                           const Vars::RefIn<float>* irrad,
                           Vars::RefOut<float> wind_out,
                           Vars::RefOut<float> gust_out,
                           Vars::RefOut<float> rain_out,
                           Vars::RefOut<float> irrad_out,
                           Vars::RefOut<bool> fault) {
    for (uint8_t i = 0; i < 2; i++) {
        _wind[i]  = wind[i];
        _gust[i]  = gust[i];
        _rain[i]  = rain[i];
        _irrad[i] = irrad[i];
    }
    _wind_out  = wind_out;
    _gust_out  = gust_out;
    _rain_out  = rain_out;
    _irrad_out = irrad_out;
    _fault     = fault;
}

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

void WeatherProbes::_publish(Vars::RefOut<float> row, const WxVote& v) {
    // Nothing believable left, so there is nothing to compute FROM: the row
    // keeps what the weather last really was and says it is not current. A law
    // reading it through its own reference gets the declared value instead.
    if (v.used == 0) row.hold();
    else             row.set(v.value, true);
}

uint8_t WeatherProbes::step(uint32_t dt_ms) {
    // Three of the four take the worse reading, because three of the four are
    // read by an interlock. Irradiance is not: nothing controls on it, so the
    // worst case has no meaning and the mean is the better estimate.
    const WxVote wind  = _worse(_wind);
    const WxVote gust  = _worse(_gust);
    const WxVote rain  = _worse(_rain);
    const WxVote sun   = _mean(_irrad);

    _publish(_wind_out,  wind);
    _publish(_gust_out,  gust);
    _publish(_rain_out,  rain);
    _publish(_irrad_out, sun);

    // The WORST-served quantity decides, so a station answering three of its
    // four registers is still a station short. One is enough to keep the plant
    // running and is worth sending somebody out for.
    uint8_t worst = wind.used;
    if (gust.used < worst) worst = gust.used;
    if (rain.used < worst) worst = rain.used;
    if (sun.used  < worst) worst = sun.used;

    _fault.set(_quorum_for(worst < 2, WX_QUORUM_MS, dt_ms), true);
    return worst;
}

}  // namespace Site
