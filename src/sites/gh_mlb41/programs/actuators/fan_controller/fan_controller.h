#ifndef SITE_PROGRAM_FAN_CONTROLLER_H
#define SITE_PROGRAM_FAN_CONTROLLER_H

/**
 * @file fan_controller.h
 * @brief HAND-WRITTEN - three relayed speeds, driven by whichever demand wants the most.
 */

#include <cstdint>

#include "../../../vars.h"
#include "fan_controller.gen.h"

namespace Site {

using namespace Botanik;

class FanController final : public Gen::FanController {

    /** @brief 0 (off) to 3, and the three rungs it is chosen on. Held across
     *  scans: the hysteresis lives on the rungs. */
    uint8_t _speed = 0;
    Plc::Staircase<3> _steps;

public:
    FanController(const SetupContext& ctx, Vars::Bank& io)
        : Gen::FanController(ctx, io) {}

    /** @brief How many destratification fans the roof has. */
    static const uint8_t FANS = 2;

protected:
    void run(ProgramContext& c) override;

private:
    /** @brief The three demands, fused. */
    float _demand() const;
    /** @brief The speed @p demand asks for, given the one in force. */
    uint8_t _band(float demand);
    /** @brief Energise exactly one speed relay, or none. */
    void _drive(uint8_t speed);
};

}  // namespace Site

#endif // SITE_PROGRAM_FAN_CONTROLLER_H
