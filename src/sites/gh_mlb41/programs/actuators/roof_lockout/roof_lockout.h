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

    Vars::VarRef _watch_rows[7];  ///< swept for validity before each activation

public:
    RoofLockout(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "roof_lockout", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void RoofLockout::bind(Vars::Bank& io) {
    rain_mmph   = io.resolve_in<float>("cli_rain_mmph");
    wind_mps    = io.resolve_in<float>("cli_wind_mps");
    gust_mps    = io.resolve_in<float>("cli_windGust_mps");
    shut_end    = io.resolve_in<float>("cli_windShutEnd_mps");
    shut_begin  = io.resolve_in<float>("cli_windShutBegin_mps");
    shut_max    = io.resolve_in<float>("cli_windShutMax");
    rain_hold_s = io.resolve_in<uint16_t>("cli_rainLockout_sec");
    supply_lock = io.resolve_in<bool>("sys_supply_lockout");
    cap         = io.resolve_out<float>("cli_windowCap");
    rain_lock   = io.resolve_inout<bool>("cli_rainLockout");
    wind_lock   = io.resolve_inout<bool>("cli_windLockout");
    window_lock = io.resolve_inout<bool>("cli_windowLockout");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = rain_mmph.r;
    _watch_rows[1] = wind_mps.r;
    _watch_rows[2] = gust_mps.r;
    _watch_rows[3] = supply_lock.r;
    _watch_rows[4] = rain_lock.r;
    _watch_rows[5] = wind_lock.r;
    _watch_rows[6] = window_lock.r;
    set_inputs(_watch_rows, 7);
    set_health_row(io.resolve_out<uint8_t>("prog_roof_lockout"));
}

}  // namespace Site

#endif // SITE_PROGRAM_ROOF_LOCKOUT_H
