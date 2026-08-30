/**
 * @file ac_local_watch.h
 * @brief The declaration half of the `ac_local_watch` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_AC_LOCAL_WATCH_H
#define SITE_PROGRAM_AC_LOCAL_WATCH_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

#include "../../../blocks/ac_supply/ac_supply.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The generator-backed side of the transfer switch, watched.
 */
class AcLocalWatch final : public Plc::Program {

public:

    /** @name VAR - the meter's own rows
     *  The block's declared interface, filled by bind(). @{ */

    AcSupplyRefs meter;

    /** @} */

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<bool>     grid_ok;
    Vars::RefIn<uint16_t> start_s;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<bool> gen_fault;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    AcSupply supply;
    Plc::Ton cranking;  ///< the grid gone with nothing taking over

    /** @} */

public:
    AcLocalWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "ac_local_watch", BOTANIK_SITE_CONTROL_MS, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void on_bind();  ///< wire a block to a row it was handed
};

inline void AcLocalWatch::bind(Vars::Bank& io) {
    meter.volts  = _bind_input<float>(io, "ats_V");
    meter.amps   = _bind_input<float>(io, "ats_A");
    meter.watts  = _bind_input<float>(io, "ats_W");
    meter.hz     = _bind_input<float>(io, "ats_Hz");
    meter.lo_V   = _bind_input<float>(io, "pwr_localLo_V");
    meter.hi_V   = _bind_input<float>(io, "pwr_localHi_V");
    meter.lo_Hz  = _bind_input<float>(io, "pwr_localLo_Hz");
    meter.hi_Hz  = _bind_input<float>(io, "pwr_localHi_Hz");
    meter.out_V  = _bind_output<float>(io, "sys_local_V");
    meter.out_A  = _bind_output<float>(io, "sys_local_A");
    meter.out_W  = _bind_output<float>(io, "sys_local_W");
    meter.out_Hz = _bind_output<float>(io, "sys_local_Hz");
    meter.ok     = _bind_output<bool>(io, "sys_local_ok");
    meter.fault  = _bind_output<bool>(io, "alarm_ac_local");
    supply.bind(meter);

    grid_ok   = _bind_input<bool>(io, "sys_grid_ok");
    start_s   = _bind_input<uint16_t>(io, "pwr_genStart_sec");
    gen_fault = _bind_output<bool>(io, "alarm_gen");
}

}  // namespace Site

#endif // SITE_PROGRAM_AC_LOCAL_WATCH_H
