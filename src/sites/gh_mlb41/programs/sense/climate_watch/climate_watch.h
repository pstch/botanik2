#ifndef SITE_PROGRAM_CLIMATE_WATCH_H
#define SITE_PROGRAM_CLIMATE_WATCH_H

/**
 * @file climate_watch.h
 * @brief HAND-WRITTEN - the interior conditions, watched.
 */

#include <cstdint>

#include "climate_watch.gen.h"

namespace Site {

using namespace Botanik;

class ClimateWatch final : public Gen::ClimateWatch {

public:
    ClimateWatch(const SetupContext& ctx, Vars::Bank& io)
        : Gen::ClimateWatch(ctx, io) {}

protected:
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;
};

}  // namespace Site

#endif // SITE_PROGRAM_CLIMATE_WATCH_H
