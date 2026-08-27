/**
 * @file irrigation_modulation.h
 * @brief The declaration half of the `irrigation_modulation` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_IRRIGATION_MODULATION_H
#define SITE_PROGRAM_IRRIGATION_MODULATION_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The irrigation ladder: how thirsty the air has been, as one factor.
 */
class IrrigationModulation final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float>    vpd;
    Vars::RefIn<uint16_t> dry_days;
    Vars::RefIn<float>    dry_lo;
    Vars::RefIn<float>    dry_hi;
    Vars::RefIn<float>    mod_min;
    Vars::RefIn<float>    mod_gamma;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> vpd_avg;
    Vars::RefOut<float> mod;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    /** @brief one hour, self-reset: when the dryness average takes a sample */
    Plc::Ton                fold;
    Utils::ExpMovingAverage dryness;  ///< the dryness tracker

    /** @} */

    /** @name VAR - what this law remembers
     *  @{ */

    bool _seeded = false;

    /** @} */

public:
    IrrigationModulation(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "irr_modulation", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;  ///< reports in EVERY state

private:

    /** @brief Fold the VPD into the dryness average, at most once an hour.
     *  @return the average now in force */
    float _track_dryness(ProgramContext& c);
};

inline void IrrigationModulation::bind(Vars::Bank& io) {
    vpd       = _bind_input<float>(io, "cli_vpd_kPa");
    dry_days  = _bind_input<uint16_t>(io, "irr_dryEwmaDays");
    dry_lo    = _bind_input<float>(io, "irr_dryLo_kPa");
    dry_hi    = _bind_input<float>(io, "irr_dryHi_kPa");
    mod_min   = _bind_input<float>(io, "irr_modMin");
    mod_gamma = _bind_input<float>(io, "irr_modGamma");
    vpd_avg   = _bind_output<float>(io, "irr_vpdAvg_kPa");
    mod       = _bind_output<float>(io, "irr_mod");

    set_health_row(_bind_output<uint8_t>(io, "prog_irr_modulation"));
}

}  // namespace Site

#endif // SITE_PROGRAM_IRRIGATION_MODULATION_H
