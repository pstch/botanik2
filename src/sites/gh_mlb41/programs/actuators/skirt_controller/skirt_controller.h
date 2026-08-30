/**
 * @file skirt_controller.h
 * @brief The declaration half of the `skirt_controller` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_SKIRT_CONTROLLER_H
#define SITE_PROGRAM_SKIRT_CONTROLLER_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

#include "../../../blocks/valve_controller/valve_controller.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The wet skirt: one valve pulsed against two demands.
 */
class SkirtController final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float>    cool_dem;
    Vars::RefIn<float>    humid_dem;
    Vars::RefIn<float>    cool_start;
    Vars::RefIn<float>    dry_min;
    Vars::RefIn<float>    dp_margin;
    Vars::RefIn<bool>     rot_lock;
    Vars::RefIn<uint16_t> period_s;
    Vars::RefIn<uint16_t> min_on_s;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<bool>  valve;
    Vars::RefOut<float> duty;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    ValveController pulse;  ///< the on-time within a period

    /** @} */

public:
    SkirtController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "skirt", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void on_bind();  ///< wire a block to a row it was handed
};

inline void SkirtController::bind(Vars::Bank& io) {
    cool_dem   = _bind_input<float>(io, "cli_coolDem");
    humid_dem  = _bind_input<float>(io, "cli_humidDem");
    cool_start = _bind_input<float>(io, "cli_skirtCoolDem");
    dry_min    = _bind_input<float>(io, "cli_skirtDryMin_degC");
    dp_margin  = _bind_input<float>(io, "cli_dpMargin_degC");
    rot_lock   = _bind_input<bool>(io, "cli_rotLockout");
    period_s   = _bind_input<uint16_t>(io, "cli_skirtPeriod_sec");
    min_on_s   = _bind_input<uint16_t>(io, "cli_skirtMinOn_sec");
    valve      = _bind_output<bool>(io, "cli_skirtValve");
    duty       = _bind_output<float>(io, "cli_skirtDuty");

    pulse.bind(ValveRefs{ valve });
}

}  // namespace Site

#endif // SITE_PROGRAM_SKIRT_CONTROLLER_H
