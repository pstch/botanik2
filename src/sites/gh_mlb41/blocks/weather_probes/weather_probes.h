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
 * Not a Program - the stations are a part OF one. Unfiltered, deliberately:
 * the roof interlock reads these, each station already averages over its own
 * window, and a filter here would be a second lag in front of a gust.
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

    /** @} */

    /** @name Blocks
     *  @{ */

    Plc::Ton _quorum_for;   ///< dwell before a lost station is announced

    /** @} */

    /** @name Selection
     *  @{ */

    /** @brief The WORSE of the stations - the higher reading - over those that
     *  are believable. What every quantity an interlock reads is voted by. */
    static WxVote _worse(const Vars::RefIn<float>* refs);
    /** @brief Their MEAN, for a quantity nothing controls on. */
    static WxVote _mean(const Vars::RefIn<float>* refs);
    /** @brief Publish @p v, or hold @p row when nothing measured it. */
    static void _publish(Vars::RefOut<float> row, const WxVote& v);

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
                Vars::RefOut<bool> fault);

    /** @brief Select and publish. @return how many stations the WORST-served
     *  quantity had; 2 is both answering. */
    uint8_t step(uint32_t dt_ms);

    void reset() { _quorum_for.reset(); }

    /** @} */
};

}  // namespace Site

#endif // SITE_BLOCKS_WEATHER_PROBES_H
