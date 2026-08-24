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

    Vars::VarRef _watch_rows[6];  ///< swept for validity before each activation

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
    void _dry_gate(ProgramContext& c);
};

inline void HumidityController::bind(Vars::Bank& io) {
    rot_release_s = io.resolve_in<uint16_t>("cli_rotRelease_sec");
    dp_margin     = io.resolve_in<float>("cli_dpMargin_degC");
    dehum_full    = io.resolve_in<float>("cli_dehumFull_degC");
    dehum_start   = io.resolve_in<float>("cli_dehumStart_degC");
    humid_start   = io.resolve_in<float>("cli_humidStart_degC");
    humid_full    = io.resolve_in<float>("cli_humidFull_degC");
    rot_limit     = io.resolve_in<float>("cli_rotMargin_degC");
    rot           = io.resolve_in<bool>("alarm_rot");
    in_t          = io.resolve_in<float>("cli_temp_hi");
    in_rh         = io.resolve_in<float>("cli_rh_hi");
    ext_t         = io.resolve_in<float>("cli_temp_ext");
    ext_rh        = io.resolve_in<float>("cli_rh_ext");
    dry_margin    = io.resolve_in<float>("cli_ventDryMgn_degC");
    vent_hyst     = io.resolve_in<float>("cli_ventHyst_degC");
    demand        = io.resolve_out<float>("cli_humidDem");
    lockout       = io.resolve_out<bool>("cli_rotLockout");
    vent_dem      = io.resolve_out<float>("cli_rotVentDem");
    dry_ok        = io.resolve_out<bool>("cli_intakeDryOk");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = dp_margin.r;
    _watch_rows[1] = rot.r;
    _watch_rows[2] = in_t.r;
    _watch_rows[3] = in_rh.r;
    _watch_rows[4] = ext_t.r;
    _watch_rows[5] = ext_rh.r;
    set_inputs(_watch_rows, 6);
    set_health_row(io.resolve_out<uint8_t>("prog_humidity"));
}

}  // namespace Site

#endif // SITE_PROGRAM_HUMIDITY_CONTROLLER_H
