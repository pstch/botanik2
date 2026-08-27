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
class WeatherProbes {

    /** @name Variables
     *  @{ */

    Vars::RefIn<float>  _wind[2];
    Vars::RefIn<float>  _gust[2];
    Vars::RefIn<float>  _rain[2];
    Vars::RefIn<float>  _irrad[2];
    Vars::RefOut<float> _wind_out;
    Vars::RefOut<float> _gust_out;
    Vars::RefOut<float> _rain_out;
    Vars::RefOut<float> _irrad_out;
    Vars::RefOut<bool>  _fault;
    float               _tau_s = 1.0f;

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

    /** @brief Adopt the two stations' rows and the four they feed, all
     *  resolved by the program that owns them. */
    void attach(const Vars::RefIn<float>* wind, const Vars::RefIn<float>* gust,
                const Vars::RefIn<float>* rain, const Vars::RefIn<float>* irrad,
                Vars::RefOut<float> wind_out, Vars::RefOut<float> gust_out,
                Vars::RefOut<float> rain_out, Vars::RefOut<float> irrad_out,
                Vars::RefOut<bool> fault, float tau_s);

    /** @brief Select and publish. @return how many stations the WORST-served
     *  quantity had; 2 is both answering. */
    uint8_t step(uint32_t dt_ms);

    void reset() {
        _quorum_for.reset();
        _wind_f.reset(); _gust_f.reset(); _rain_f.reset(); _irrad_f.reset();
        _wind_seeded = _gust_seeded = _rain_seeded = _irrad_seeded = false;
    }

    /** @} */
};

}  // namespace Site

#endif // SITE_BLOCKS_WEATHER_PROBES_H
