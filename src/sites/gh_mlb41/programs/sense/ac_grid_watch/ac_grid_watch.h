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

    /** @name VAR - the meter's own rows
     *  The block's declared interface, filled by bind(). Held here rather than
     *  as fourteen loose members, so what this program reads IS what the block
     *  was handed. @{ */

    AcSupplyRefs meter;

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
    void on_bind();  ///< wire a block to a row it was handed
};

inline void AcGridWatch::bind(Vars::Bank& io) {
    meter.volts  = _bind_input<float>(io, "grid_LN_V");
    meter.amps   = _bind_input<float>(io, "grid_total_A");
    meter.watts  = _bind_input<float>(io, "grid_total_W");
    meter.hz     = _bind_input<float>(io, "grid_Hz");
    meter.lo_V   = _bind_input<float>(io, "pwr_gridLo_V");
    meter.hi_V   = _bind_input<float>(io, "pwr_gridHi_V");
    meter.lo_Hz  = _bind_input<float>(io, "pwr_gridLo_Hz");
    meter.hi_Hz  = _bind_input<float>(io, "pwr_gridHi_Hz");
    meter.out_V  = _bind_output<float>(io, "sys_grid_V");
    meter.out_A  = _bind_output<float>(io, "sys_grid_A");
    meter.out_W  = _bind_output<float>(io, "sys_grid_W");
    meter.out_Hz = _bind_output<float>(io, "sys_grid_Hz");
    meter.ok     = _bind_output<bool>(io, "sys_grid_ok");
    meter.fault  = _bind_output<bool>(io, "alarm_ac_grid");
    supply.bind(meter);
}

}  // namespace Site

#endif // SITE_PROGRAM_AC_GRID_WATCH_H
