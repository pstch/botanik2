#ifndef SITE_PROGRAM_WEATHER_WATCH_H
#define SITE_PROGRAM_WEATHER_WATCH_H

/**
 * @file weather_watch.h
 * @brief HAND-WRITTEN - the weather station, watched: answering, and what it says.
 */

#include <cstdint>

#include "weather_watch.gen.h"

namespace Site {

using namespace Botanik;

class WeatherWatch final : public Gen::WeatherWatch {

    /** @brief Whether the station has EVER answered. Before it has, silence is
     *  bring-up; after, it is a fault. @see AcSupply */
    bool     _seen = false;
    /** @brief Ms since the gauge last read wet, and whether it ever has in this
     *  run. A boot does not know when it last rained, so before the first
     *  shower the row is held rather than reading "just now". */
    uint64_t _dry_ms = 0;
    bool     _wet_seen = false;

public:
    WeatherWatch(const SetupContext& ctx, Vars::Bank& io)
        : Gen::WeatherWatch(ctx, io) {}

protected:
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;
};

}  // namespace Site

#endif // SITE_PROGRAM_WEATHER_WATCH_H
