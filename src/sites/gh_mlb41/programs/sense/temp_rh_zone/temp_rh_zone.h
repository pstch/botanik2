/**
 * @file temp_rh_zone.h
 * @brief The declaration half of the `temp_rh_zone` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_TEMP_RH_ZONE_H
#define SITE_PROGRAM_TEMP_RH_ZONE_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The psychrometrics every other law is set in, off the two interior
 *        levels: one temperature for the house, and the worst case per quantity.
 */
class TempRHZone final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float> temp_hi;
    Vars::RefIn<float> rh_hi;
    Vars::RefIn<float> temp_lo;
    Vars::RefIn<float> rh_lo;
    Vars::RefIn<float> rot_limit;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> temp_in;
    Vars::RefOut<float> dew_point;
    Vars::RefOut<float> dp_margin;
    Vars::RefOut<float> sat_press;
    Vars::RefOut<float> vpd;
    Vars::RefOut<bool>  rot;

    /** @} */

    TempRHZone(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "temp_rh_zone", Tasks::GROUP_FAST, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void TempRHZone::bind(Vars::Bank& io) {
    temp_hi   = _bind_input<float>(io, "cli_temp_hi");
    rh_hi     = _bind_input<float>(io, "cli_rh_hi");
    temp_lo   = _bind_input<float>(io, "cli_temp_lo");
    rh_lo     = _bind_input<float>(io, "cli_rh_lo");
    rot_limit = _bind_input<float>(io, "cli_rotMargin_degC");
    temp_in   = _bind_output<float>(io, "cli_temp_in");
    dew_point = _bind_output<float>(io, "cli_dewPoint_degC");
    dp_margin = _bind_output<float>(io, "cli_dpMargin_degC");
    sat_press = _bind_output<float>(io, "cli_satPress_kPa");
    vpd       = _bind_output<float>(io, "cli_vpd_kPa");
    rot       = _bind_output<bool>(io, "alarm_rot");

}

}  // namespace Site

#endif // SITE_PROGRAM_TEMP_RH_ZONE_H
