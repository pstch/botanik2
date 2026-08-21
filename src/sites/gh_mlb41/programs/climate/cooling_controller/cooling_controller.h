#ifndef SITE_PROGRAM_COOLING_CONTROLLER_H
#define SITE_PROGRAM_COOLING_CONTROLLER_H

/**
 * @file cooling_controller.h
 * @brief HAND-WRITTEN - a PID on the interior temperature, as one signed demand.
 */

#include <cstdint>

#include "cooling_controller.gen.h"

namespace Site {

using namespace Botanik;

class CoolingController final : public Gen::CoolingController {

public:
    CoolingController(const SetupContext& ctx, Vars::Bank& io)
        : Gen::CoolingController(ctx, io) {}

protected:
    void run(ProgramContext& c) override;

private:
    /** @brief Whether drawing crawlspace air would help, in either direction.
     *  Published rather than acted on: this law owns the reading, the
     *  intractors own the device. */
    void _intake_gates(ProgramContext& c, float t_in, float dem);
};

}  // namespace Site

#endif // SITE_PROGRAM_COOLING_CONTROLLER_H
