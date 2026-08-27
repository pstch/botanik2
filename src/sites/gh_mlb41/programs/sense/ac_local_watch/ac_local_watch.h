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

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float>    volts;
    Vars::RefIn<float>    amps;
    Vars::RefIn<float>    watts;
    Vars::RefIn<float>    hz;
    Vars::RefIn<float>    lo_V;
    Vars::RefIn<float>    hi_V;
    Vars::RefIn<float>    lo_Hz;
    Vars::RefIn<float>    hi_Hz;
    Vars::RefIn<bool>     grid_ok;
    Vars::RefIn<uint16_t> start_s;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> out_V;
    Vars::RefOut<float> out_A;
    Vars::RefOut<float> out_W;
    Vars::RefOut<float> out_Hz;
    Vars::RefOut<bool>  ok;
    Vars::RefOut<bool>  fault;
    Vars::RefOut<bool>  gen_fault;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    AcSupply supply;
    Plc::Ton cranking;  ///< the grid gone with nothing taking over

    /** @} */

public:
    AcLocalWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "ac_local_watch", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;  ///< reports in EVERY state
    void on_bind();  ///< wire a block to a row it was handed
};

inline void AcLocalWatch::bind(Vars::Bank& io) {
    volts     = _bind_input<float>(io, "ats_V");
    amps      = _bind_input<float>(io, "ats_A");
    watts     = _bind_input<float>(io, "ats_W");
    hz        = _bind_input<float>(io, "ats_Hz");
    lo_V      = _bind_input<float>(io, "pwr_localLo_V");
    hi_V      = _bind_input<float>(io, "pwr_localHi_V");
    lo_Hz     = _bind_input<float>(io, "pwr_localLo_Hz");
    hi_Hz     = _bind_input<float>(io, "pwr_localHi_Hz");
    grid_ok   = _bind_input<bool>(io, "sys_grid_ok");
    start_s   = _bind_input<uint16_t>(io, "pwr_genStart_sec");
    out_V     = _bind_output<float>(io, "sys_local_V");
    out_A     = _bind_output<float>(io, "sys_local_A");
    out_W     = _bind_output<float>(io, "sys_local_W");
    out_Hz    = _bind_output<float>(io, "sys_local_Hz");
    ok        = _bind_output<bool>(io, "sys_local_ok");
    fault     = _bind_output<bool>(io, "alarm_ac_local");
    gen_fault = _bind_output<bool>(io, "alarm_gen");

    set_health_row(_bind_output<uint8_t>(io, "prog_ac_local_watch"));
    on_bind();
}

}  // namespace Site

#endif // SITE_PROGRAM_AC_LOCAL_WATCH_H
