/**
 * @file ac_grid_watch.h
 * @brief The declaration half of the `ac_grid_watch` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_AC_GRID_WATCH_H
#define SITE_PROGRAM_AC_GRID_WATCH_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

#include "../../../blocks/ac_supply/ac_supply.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The utility feed, watched.
 */
class AcGridWatch final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float> volts;
    Vars::RefIn<float> amps;
    Vars::RefIn<float> watts;
    Vars::RefIn<float> hz;
    Vars::RefIn<float> lo_V;
    Vars::RefIn<float> hi_V;
    Vars::RefIn<float> lo_Hz;
    Vars::RefIn<float> hi_Hz;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> out_V;
    Vars::RefOut<float> out_A;
    Vars::RefOut<float> out_W;
    Vars::RefOut<float> out_Hz;
    Vars::RefOut<bool>  ok;
    Vars::RefOut<bool>  fault;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    AcSupply supply;

    /** @} */

    Vars::VarRef _watch_rows[4];  ///< swept for validity before each activation

public:
    AcGridWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "ac_grid_watch", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;  ///< reports in EVERY state
    void on_bind();  ///< wire a block to a row it was handed
};

inline void AcGridWatch::bind(Vars::Bank& io) {
    volts  = io.resolve_in<float>("grid_LN_V");
    amps   = io.resolve_in<float>("grid_total_A");
    watts  = io.resolve_in<float>("grid_total_W");
    hz     = io.resolve_in<float>("grid_Hz");
    lo_V   = io.resolve_in<float>("pwr_gridLo_V");
    hi_V   = io.resolve_in<float>("pwr_gridHi_V");
    lo_Hz  = io.resolve_in<float>("pwr_gridLo_Hz");
    hi_Hz  = io.resolve_in<float>("pwr_gridHi_Hz");
    out_V  = io.resolve_out<float>("sys_grid_V");
    out_A  = io.resolve_out<float>("sys_grid_A");
    out_W  = io.resolve_out<float>("sys_grid_W");
    out_Hz = io.resolve_out<float>("sys_grid_Hz");
    ok     = io.resolve_out<bool>("sys_grid_ok");
    fault  = io.resolve_out<bool>("alarm_ac_grid");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = volts.r;
    _watch_rows[1] = amps.r;
    _watch_rows[2] = watts.r;
    _watch_rows[3] = hz.r;
    set_inputs(_watch_rows, 4);
    set_health_row(io.resolve_out<uint8_t>("prog_ac_grid_watch"));
    on_bind();
}

}  // namespace Site

#endif // SITE_PROGRAM_AC_GRID_WATCH_H
