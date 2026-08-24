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

    /** @} */

    Vars::VarRef _watch_rows[1];  ///< swept for validity before each activation

public:
    IrrMtrInput(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "irr_mtr_input", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;  ///< reports in EVERY state
};

inline void IrrMtrInput::bind(Vars::Bank& io) {
    rate_win      = io.resolve_in<uint16_t>("irr_rateWin_ms");
    qty_per_pulse = io.resolve_in<float>("irr_input_qtyPerImp");
    leak_Lpm      = io.resolve_in<float>("irr_leak_Lpm");
    min_fill_Lpm  = io.resolve_in<float>("irr_minFill_Lpm");
    state         = io.resolve_in<uint8_t>("irr_state");
    in_flow       = io.resolve_out<float>("irr_inFlow_Lpm");
    in_total      = io.resolve_out<float>("irr_inTotal_L");
    leak          = io.resolve_out<bool>("alarm_water_leak");
    pressure      = io.resolve_out<bool>("alarm_water_pressure");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = state.r;
    set_inputs(_watch_rows, 1);
    set_health_row(io.resolve_out<uint8_t>("prog_irr_mtr_input"));
}

}  // namespace Site

#endif // SITE_PROGRAM_IRR_MTR_INPUT_H
