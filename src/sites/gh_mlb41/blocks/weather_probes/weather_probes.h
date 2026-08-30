#ifndef SITE_BLOCKS_WEATHER_PROBES_H
#define SITE_BLOCKS_WEATHER_PROBES_H

/**
 * @file weather_probes.h
 * @brief HAND-WRITTEN - the weather: two stations, voted down to one reading.
 */

#include <cstdint>

#include "libbotanik.h"

namespace Site {

using namespace Botanik;

/** @brief What a two-station selection came to: the reading, and how many
 *  stations were behind it. @c used of 0 means neither was believable. */
struct WxVote {
    float   value;
    uint8_t used;
};

/**
 * @brief Two weather stations selected down to one reading per quantity.
 *
 * TWO is a weaker vote than the three a zone gets: a disagreement can be seen
 * but not resolved, because there is no third reading to say which station is
 * lying. So there is no outlier rule here - the interlock quantities take the
 * WORSE of the two, which is the only choice that cannot be argued into
 * opening a roof on a station that has stuck low.
 *
 * Not a Program - the stations are a part OF one. Held by value so one
 * quantity's filter state cannot be shared with another's by accident.
 */
struct WeatherProbeRefs {
    /** @name VAR_INPUT
     *  Two stations per quantity - a weaker vote than a zone's three. @{ */
    Vars::RefIn<float>  wind[2];
    Vars::RefIn<float>  gust[2];
    Vars::RefIn<float>  rain[2];
    Vars::RefIn<float>  irrad[2];
    /** @} */
    /** @name VAR_OUTPUT
     *  @{ */
    Vars::RefOut<float> wind_out, gust_out, rain_out, irrad_out;
    Vars::RefOut<bool>  fault;
    /** @} */
};

class WeatherProbes : public Plc::Block {

    /** @name Variables
     *  @{ */

    WeatherProbeRefs _r;
    float            _tau_s = 1.0f;

    /** @} */

    /** @name Blocks
     *  Filtered against dt, not against a fixed blend: a program due late runs
     *  ONCE with the real elapsed time, so a filter carrying a constant alpha
     *  silently changes its time constant the moment the scan slips. One tau
     *  for all four (cli_wxTau_ms), shorter than the probes' - a roof interlock
     *  answers weather that moves in seconds.
     *  @{ */

    Plc::Ewma _wind_f;
    Plc::Ewma _gust_f;
    Plc::Ewma _rain_f;
    Plc::Ewma _irrad_f;
    bool      _wind_seeded  = false;
    bool      _gust_seeded  = false;
    bool      _rain_seeded  = false;
    bool      _irrad_seeded = false;
    Plc::Ton  _quorum_for;   ///< dwell before a lost station is announced

    /** @} */

    /** @name Selection
     *  @{ */

    /** @brief The WORSE of the stations - the higher reading - over those that
     *  are believable. What every quantity an interlock reads is voted by. */
    static WxVote _worse(const Vars::RefIn<float>* refs);
    /** @brief Their MEAN, for a quantity nothing controls on. */
    static WxVote _mean(const Vars::RefIn<float>* refs);
    /** @brief Filter @p v and publish it, clamped to @p lo / @p hi. With
     *  nothing measured the row keeps the last average and says so. */
    static void _publish(Vars::RefOut<float> row, Plc::Ewma& filter,
                         const WxVote& v, bool& seeded, float tau_s,
                         uint32_t dt_ms, float lo, float hi);

    /** @} */

public:
    /** @name Lifecycle
     *  @{ */

    /** @brief Adopt the rows the program resolved. */
    void bind(const WeatherProbeRefs& r, float tau_s) { _r = r; _tau_s = tau_s; }

    /** @brief Select and publish. @return how many stations the WORST-served
     *  quantity had; 2 is both answering. */
    uint8_t operator()(uint32_t dt_ms);

    void reset() {
        _quorum_for.reset();
        _wind_f.reset(); _gust_f.reset(); _rain_f.reset(); _irrad_f.reset();
        _wind_seeded = _gust_seeded = _rain_seeded = _irrad_seeded = false;
    }

    /** @} */
};

BOTANIK_PLC_BLOCK(WeatherProbes);

}  // namespace Site

#endif // SITE_BLOCKS_WEATHER_PROBES_H
