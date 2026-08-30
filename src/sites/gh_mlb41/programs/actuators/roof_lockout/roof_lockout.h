/**
 * @file roof_lockout.h
 * @brief The declaration half of the `roof_lockout` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_ROOF_LOCKOUT_H
#define SITE_PROGRAM_ROOF_LOCKOUT_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief What forbids the roof being open at all, whatever the ladder asks.
 */
class RoofLockout final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float>    rain_mmph;
    Vars::RefIn<float>    wind_mps;
    Vars::RefIn<float>    gust_mps;
    Vars::RefIn<float>    shut_end;
    Vars::RefIn<float>    shut_begin;
    Vars::RefIn<float>    shut_max;
    Vars::RefIn<uint16_t> rain_hold_s;
    Vars::RefIn<bool>     supply_lock;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> cap;

    /** @} */

    /** @name VAR_IN_OUT
     *  @{ */

    Vars::RefInOut<bool> rain_lock;
    Vars::RefInOut<bool> wind_lock;
    Vars::RefInOut<bool> window_lock;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    Plc::Tof rain_for;  ///< the rain lockout's overrun past the last drop

    /** @} */

public:
    RoofLockout(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "roof_lockout", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void RoofLockout::bind(Vars::Bank& io) {
    rain_mmph   = _bind_input<float>(io, "cli_rain_mmph");
    wind_mps    = _bind_input<float>(io, "cli_wind_mps");
    gust_mps    = _bind_input<float>(io, "cli_windGust_mps");
    shut_end    = _bind_input<float>(io, "cli_windShutEnd_mps");
    shut_begin  = _bind_input<float>(io, "cli_windShutBegin_mps");
    shut_max    = _bind_input<float>(io, "cli_windShutMax");
    rain_hold_s = _bind_input<uint16_t>(io, "cli_rainLockout_sec");
    supply_lock = _bind_input<bool>(io, "sys_supply_lockout");
    cap         = _bind_output<float>(io, "cli_windowCap");
    rain_lock   = _bind_inout<bool>(io, "cli_rainLockout");
    wind_lock   = _bind_inout<bool>(io, "cli_windLockout");
    window_lock = _bind_inout<bool>(io, "cli_windowLockout");

}

}  // namespace Site

#endif // SITE_PROGRAM_ROOF_LOCKOUT_H
