#ifndef SITE_PROGRAM_WINDOWS_CONTROLLER_H
#define SITE_PROGRAM_WINDOWS_CONTROLLER_H

/**
 * @file windows_controller.h
 * @brief HAND-WRITTEN - four roof windows, one target, and one moving at a time.
 */

#include <cstdint>

#include "../../../vars.h"
#include "windows_controller.gen.h"

namespace Site {

using namespace Botanik;

class WindowsController final : public Gen::WindowsController {

    /** @name State
     *  @{ */

    /** @brief The position the arbitration asked for this scan. One number for
     *  all four: they are one roof. */
    float         _target = 0.0f;

    /** @} */

public:
    /** @brief Where the roof goes when this law is not acting: PART OPEN, not
     *  shut. It should never be reached - the interior is measured by three
     *  probes at each of two levels precisely so that it is not - but if it is,
     *  a greenhouse with some ventilation survives a summer afternoon and a
     *  sealed one does not.
     *  @note NOT the interlocks: rain and wind shut the roof outright, and a
     *  mechanical safety is resolved before any of this. */
    static const float SAFE_OPEN;

private:

public:
    WindowsController(const SetupContext& ctx, Vars::Bank& io)
        : Gen::WindowsController(ctx, io) {}

    /** @brief How many windows the roof has. */
    static const uint8_t WINDOWS = N_ACT;

protected:
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;

private:
    /** @brief The mechanics, rebuilt each scan from the parameters: a
     *  recalibrated travel time has to land on the next stroke, not the next
     *  boot. */
    Plc::ActuatorConfig _mechanics() const;
    /** @brief Whether anything forbids the windows being open at all. Publishes
     *  the two reasons it owns. */
    bool _shut_in(ProgramContext& c);
    /** @brief Which window may move this scan, or -1. */
    int8_t _owner(float target, const Plc::ActuatorConfig& cfg) const;
};

}  // namespace Site

#endif // SITE_PROGRAM_WINDOWS_CONTROLLER_H
