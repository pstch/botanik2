/**
 * @file irr_mtr_input.h
 * @brief The declaration half of the `irr_mtr_input` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_IRR_MTR_INPUT_H
#define SITE_PROGRAM_IRR_MTR_INPUT_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The inlet meter, and the two supply faults it alone can see.
 */
class IrrMtrInput final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<uint16_t> rate_win;
    Vars::RefIn<float>    qty_per_pulse;
    Vars::RefIn<float>    leak_Lpm;
    Vars::RefIn<float>    min_fill_Lpm;
    Vars::RefIn<uint8_t>  state;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> in_flow;
    Vars::RefOut<float> in_total;
    Vars::RefOut<bool>  leak;
    Vars::RefOut<bool>  pressure;
    /** @brief The pulse LINE, which nothing else can publish: LocalInputs skips
     *  an F_PULSE terminal deliberately - the level of a pulse train is not a
     *  reading of anything - so this program, which owns the channel, says
     *  whether it is ticking. */
    Vars::RefOut<bool>  pulsing;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    Plc::Derivative rate;  ///< litres counted, differenced back into a rate
    /** @brief two rate windows of IDLE before a flow means a leak */
    Plc::Ton        idle_for;
    /** @brief two rate windows of FILLING before no flow means no pressure */
    Plc::Ton        fill_for;

    /** @} */

    /** @name VAR - what this law remembers
     *  @{ */

    /** @brief Where the counter stood when this run adopted it: the pulse
     *  counter restarts at boot and the published total does not, so the
     *  total is the retained value plus what has been counted since. */
    float    _base_L = 0.0f;
    uint32_t _base_pulses = 0;
    /** @brief The count at the previous activation, so a beat can tell whether
     *  an edge arrived in it. */
    uint32_t _last_pulses = 0;

    /** @} */

public:
    IrrMtrInput(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "irr_mtr_input", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void IrrMtrInput::bind(Vars::Bank& io) {
    rate_win      = _bind_input<uint16_t>(io, "irr_rateWin_ms");
    qty_per_pulse = _bind_input<float>(io, "irr_input_qtyPerImp");
    leak_Lpm      = _bind_input<float>(io, "irr_leak_Lpm");
    min_fill_Lpm  = _bind_input<float>(io, "irr_minFill_Lpm");
    state         = _bind_input<uint8_t>(io, "irr_state");
    in_flow       = _bind_output<float>(io, "irr_inFlow_Lpm");
    in_total      = _bind_output<float>(io, "irr_inTotal_L");
    leak          = _bind_output<bool>(io, "alarm_water_leak");
    pressure      = _bind_output<bool>(io, "alarm_water_pressure");
    pulsing       = _bind_output<bool>(io, "irr_inPulse");

}

}  // namespace Site

#endif // SITE_PROGRAM_IRR_MTR_INPUT_H
