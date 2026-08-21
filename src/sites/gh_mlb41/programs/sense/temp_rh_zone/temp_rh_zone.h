#ifndef SITE_PROGRAM_TEMP_RH_ZONE_H
#define SITE_PROGRAM_TEMP_RH_ZONE_H

/**
 * @file temp_rh_zone.h
 * @brief HAND-WRITTEN - four locations voted and filtered, then the psychrometrics.
 */

#include <cstdint>

#include "temp_rh_zone.gen.h"

namespace Site {

using namespace Botanik;

class TempRHZone final : public Gen::TempRHZone {

public:
    TempRHZone(const SetupContext& ctx, Vars::Bank& io)
        : Gen::TempRHZone(ctx, io) {}

protected:
    void on_bind() override;
    void run(ProgramContext& c) override;
};

}  // namespace Site

#endif // SITE_PROGRAM_TEMP_RH_ZONE_H
