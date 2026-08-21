#ifndef SITE_PROGRAM_DESTRAT_CONTROLLER_H
#define SITE_PROGRAM_DESTRAT_CONTROLLER_H

/**
 * @file destrat_controller.h
 * @brief HAND-WRITTEN - three bands with hysteresis on the ceiling-to-floor gap.
 */

#include <cstdint>

#include "destrat_controller.gen.h"

namespace Site {

using namespace Botanik;

class DestratController final : public Gen::DestratController {

    Plc::Staircase<3> _bands;

public:
    DestratController(const SetupContext& ctx, Vars::Bank& io)
        : Gen::DestratController(ctx, io) {}

protected:
    void run(ProgramContext& c) override;
};

}  // namespace Site

#endif // SITE_PROGRAM_DESTRAT_CONTROLLER_H
