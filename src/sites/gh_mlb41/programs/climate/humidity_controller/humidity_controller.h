/**
 * @file humidity_controller.h
 * @brief The declaration half of the `humidity_controller` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_HUMIDITY_CONTROLLER_H
#define SITE_PROGRAM_HUMIDITY_CONTROLLER_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The humidity ladder: four bands and a dead zone on the dew-point
 *        margin, published as one signed demand - and the rot guard under it.
 */
class HumidityController final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<uint16_t> rot_release_s;
    Vars::RefIn<float>    dp_margin;
    Vars::RefIn<float>    dehum_full;
    Vars::RefIn<float>    dehum_start;
    Vars::RefIn<float>    humid_start;
    Vars::RefIn<float>    humid_full;
    Vars::RefIn<float>    rot_limit;
    Vars::RefIn<bool>     rot;
    Vars::RefIn<float>    in_t;
    Vars::RefIn<float>    in_rh;
    Vars::RefIn<float>    ext_t;
    Vars::RefIn<float>    ext_rh;
    Vars::RefIn<float>    dry_margin;
    Vars::RefIn<float>    vent_hyst;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> demand;
    Vars::RefOut<bool>  lockout;
    Vars::RefOut<float> vent_dem;
    Vars::RefOut<bool>  dry_ok;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    Plc::Tof        lockout_for;  ///< the lockout's overrun past the condition
    /** @brief two dew points that converge, so a dead band */
    Plc::Hysteresis dry_gate_band;

    /** @} */

public:
    HumidityController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "humidity", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;  ///< reports in EVERY state

private:

    /** @brief Whether the air outside would dry the greenhouse if drawn in.
     *  Dew points, so it survives a cold damp morning reading 95% RH. */
    void _dry_gate();
};

inline void HumidityController::bind(Vars::Bank& io) {
    rot_release_s = _bind_input<uint16_t>(io, "cli_rotRelease_sec");
    dp_margin     = _bind_input<float>(io, "cli_dpMargin_degC");
    dehum_full    = _bind_input<float>(io, "cli_dehumFull_degC");
    dehum_start   = _bind_input<float>(io, "cli_dehumStart_degC");
    humid_start   = _bind_input<float>(io, "cli_humidStart_degC");
    humid_full    = _bind_input<float>(io, "cli_humidFull_degC");
    rot_limit     = _bind_input<float>(io, "cli_rotMargin_degC");
    rot           = _bind_input<bool>(io, "alarm_rot");
    in_t          = _bind_input<float>(io, "cli_temp_hi");
    in_rh         = _bind_input<float>(io, "cli_rh_hi");
    ext_t         = _bind_input<float>(io, "cli_temp_ext");
    ext_rh        = _bind_input<float>(io, "cli_rh_ext");
    dry_margin    = _bind_input<float>(io, "cli_ventDryMgn_degC");
    vent_hyst     = _bind_input<float>(io, "cli_ventHyst_degC");
    demand        = _bind_output<float>(io, "cli_humidDem");
    lockout       = _bind_output<bool>(io, "cli_rotLockout");
    vent_dem      = _bind_output<float>(io, "cli_rotVentDem");
    dry_ok        = _bind_output<bool>(io, "cli_intakeDryOk");

    set_health_row(_bind_output<uint8_t>(io, "prog_humidity"));
}

}  // namespace Site

#endif // SITE_PROGRAM_HUMIDITY_CONTROLLER_H
