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

public:
    IrrMtrOutput(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "irr_mtr_output", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void IrrMtrOutput::bind(Vars::Bank& io) {
    band         = _bind_input<float>(io, "irr_flowBand_Lpm");
    cutoff       = _bind_input<float>(io, "irr_flowCutoff_Lpm");
    out_flow     = _bind_input<float>(io, "irr_out_Lpm");
    out_avg_ms   = _bind_input<uint16_t>(io, "irr_out_avg_ms");
    out_flow_pub = _bind_output<float>(io, "irr_outFlow_Lpm");
    out_total    = _bind_output<float>(io, "irr_outTotal_L");

}

}  // namespace Site

#endif // SITE_PROGRAM_IRR_MTR_OUTPUT_H
