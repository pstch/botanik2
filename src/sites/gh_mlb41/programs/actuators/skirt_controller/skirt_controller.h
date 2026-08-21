#ifndef SITE_PROGRAM_SKIRT_CONTROLLER_H
#define SITE_PROGRAM_SKIRT_CONTROLLER_H

/**
 * @file skirt_controller.h
 * @brief HAND-WRITTEN - the wet skirt: one valve pulsed against two demands.
 */

#include <cstdint>

#include "skirt_controller.gen.h"

namespace Site {

using namespace Botanik;

class SkirtController final : public Gen::SkirtController {

public:
    SkirtController(const SetupContext& ctx, Vars::Bank& io)
        : Gen::SkirtController(ctx, io) {}

protected:
    void on_bind() override { pulse.attach(valve); }
    void run(ProgramContext& c) override;
};

}  // namespace Site

#endif // SITE_PROGRAM_SKIRT_CONTROLLER_H
