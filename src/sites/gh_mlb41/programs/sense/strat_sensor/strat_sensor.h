#ifndef SITE_PROGRAM_STRAT_SENSOR_H
#define SITE_PROGRAM_STRAT_SENSOR_H

/**
 * @file strat_sensor.h
 * @brief HAND-WRITTEN - the ceiling-to-floor gap, as one unsigned number.
 */

#include <cstdint>

#include "strat_sensor.gen.h"

namespace Site {

using namespace Botanik;

class StratSensor final : public Gen::StratSensor {

public:
    StratSensor(const SetupContext& ctx, Vars::Bank& io)
        : Gen::StratSensor(ctx, io) {}

protected:
    void run(ProgramContext& c) override;
};

}  // namespace Site

#endif // SITE_PROGRAM_STRAT_SENSOR_H
