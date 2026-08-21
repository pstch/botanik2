#ifndef SITE_PROGRAM_HUMIDITY_CONTROLLER_H
#define SITE_PROGRAM_HUMIDITY_CONTROLLER_H

/**
 * @file humidity_controller.h
 * @brief HAND-WRITTEN - four bands on the dew-point margin, and the rot guard.
 */

#include <cstdint>

#include "humidity_controller.gen.h"

namespace Site {

using namespace Botanik;

class HumidityController final : public Gen::HumidityController {

public:
    HumidityController(const SetupContext& ctx, Vars::Bank& io)
        : Gen::HumidityController(ctx, io) {}

protected:
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;

private:
    /** @brief Whether the air outside would dry the greenhouse if drawn in.
     *  Dew points, so it survives a cold damp morning reading 95% RH. */
    void _dry_gate(ProgramContext& c);
};

}  // namespace Site

#endif // SITE_PROGRAM_HUMIDITY_CONTROLLER_H
