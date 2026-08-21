#ifndef SITE_PROGRAM_WATER_METERS_H
#define SITE_PROGRAM_WATER_METERS_H

/**
 * @file water_meters.h
 * @brief HAND-WRITTEN - both water meters, and the two supply faults they see.
 */

#include <cstdint>

#include "water_meters.gen.h"

namespace Site {

using namespace Botanik;

class WaterMeters final : public Gen::WaterMeters {

    /** @brief Where the inlet counter stood when this run adopted it: the pulse
     *  counter restarts at boot and the published total does not, so the total
     *  is the retained value plus what has been counted since. */
    float    _in_base_L      = 0.0f;
    uint32_t _in_base_pulses = 0;

public:
    WaterMeters(const SetupContext& ctx, Vars::Bank& io)
        : Gen::WaterMeters(ctx, io) {}

protected:
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;

private:
    /** @brief Water moving with every valve shut, and water not arriving with
     *  the inlet open. Both are read off the inlet meter alone. */
    void _supply_alarms(float in_flow_Lpm);
};

}  // namespace Site

#endif // SITE_PROGRAM_WATER_METERS_H
