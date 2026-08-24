/**
 * @file irr_mtr_output.h
 * @brief The declaration half of the `irr_mtr_output` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_IRR_MTR_OUTPUT_H
#define SITE_PROGRAM_IRR_MTR_OUTPUT_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The outlet meter: what came back off the trays.
 */
class IrrMtrOutput final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float>    band;
    Vars::RefIn<float>    cutoff;
    Vars::RefIn<float>    out_flow;
    Vars::RefIn<uint16_t> out_avg_ms;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> out_flow_pub;
    Vars::RefOut<float> out_total;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    Plc::Ewma     flow_avg;  ///< the loop's own last digit, averaged out
    /** @brief how far a flow has to move to be worth a write */
    Plc::Deadband flow_band;
    Plc::Integral meter;     ///< the totalizer, counting litres per second

    /** @} */

    Vars::VarRef _watch_rows[1];  ///< swept for validity before each activation

public:
    IrrMtrOutput(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "irr_mtr_output", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;  ///< reports in EVERY state
};

inline void IrrMtrOutput::bind(Vars::Bank& io) {
    band         = io.resolve_in<float>("irr_flowBand_Lpm");
    cutoff       = io.resolve_in<float>("irr_flowCutoff_Lpm");
    out_flow     = io.resolve_in<float>("irr_out_Lpm");
    out_avg_ms   = io.resolve_in<uint16_t>("irr_out_avg_ms");
    out_flow_pub = io.resolve_out<float>("irr_outFlow_Lpm");
    out_total    = io.resolve_out<float>("irr_outTotal_L");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = out_flow.r;
    set_inputs(_watch_rows, 1);
    set_health_row(io.resolve_out<uint8_t>("prog_irr_mtr_output"));
}

}  // namespace Site

#endif // SITE_PROGRAM_IRR_MTR_OUTPUT_H
