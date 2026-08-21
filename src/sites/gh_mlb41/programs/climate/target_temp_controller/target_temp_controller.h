#ifndef SITE_PROGRAM_TARGET_TEMP_CONTROLLER_H
#define SITE_PROGRAM_TARGET_TEMP_CONTROLLER_H

/**
 * @file target_temp_controller.h
 * @brief HAND-WRITTEN - the temperature the greenhouse is being held to.
 */

#include <cstdint>

#include "target_temp_controller.gen.h"

namespace Site {

using namespace Botanik;

class TargetTempController final : public Gen::TargetTempController {

public:
    TargetTempController(const SetupContext& ctx, Vars::Bank& io)
        : Gen::TargetTempController(ctx, io) {}

protected:
    void run(ProgramContext& c) override;
};

}  // namespace Site

#endif // SITE_PROGRAM_TARGET_TEMP_CONTROLLER_H
