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
    volts  = _bind_input<float>(io, "grid_LN_V");
    amps   = _bind_input<float>(io, "grid_total_A");
    watts  = _bind_input<float>(io, "grid_total_W");
    hz     = _bind_input<float>(io, "grid_Hz");
    lo_V   = _bind_input<float>(io, "pwr_gridLo_V");
    hi_V   = _bind_input<float>(io, "pwr_gridHi_V");
    lo_Hz  = _bind_input<float>(io, "pwr_gridLo_Hz");
    hi_Hz  = _bind_input<float>(io, "pwr_gridHi_Hz");
    out_V  = _bind_output<float>(io, "sys_grid_V");
    out_A  = _bind_output<float>(io, "sys_grid_A");
    out_W  = _bind_output<float>(io, "sys_grid_W");
    out_Hz = _bind_output<float>(io, "sys_grid_Hz");
    ok     = _bind_output<bool>(io, "sys_grid_ok");
    fault  = _bind_output<bool>(io, "alarm_ac_grid");

    supply.attach(*this);
}

}  // namespace Site

#endif // SITE_PROGRAM_AC_GRID_WATCH_H
