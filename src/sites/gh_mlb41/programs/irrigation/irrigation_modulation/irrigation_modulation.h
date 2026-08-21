#ifndef SITE_PROGRAM_IRRIGATION_MODULATION_H
#define SITE_PROGRAM_IRRIGATION_MODULATION_H

/**
 * @file irrigation_modulation.h
 * @brief HAND-WRITTEN - how thirsty the air has been, as one factor.
 */

#include <cstdint>

#include "../../../vars.h"
#include "irrigation_modulation.gen.h"

namespace Site {

using namespace Botanik;

class IrrigationModulation final : public Gen::IrrigationModulation {

    bool                    _seeded = false;
    uint64_t                _last_fold_ms = 0;

public:
    IrrigationModulation(const SetupContext& ctx, Vars::Bank& io)
        : Gen::IrrigationModulation(ctx, io) {}

    /** @brief How often the dryness average folds. Not a parameter: the filter
     *  is written against this interval, and changing it silently rescales
     *  `irr_dryEwmaDays`. */
    static const uint64_t FOLD_MS;

protected:
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;

private:
    /** @brief Fold the VPD into the dryness average, at most once an hour.
     *  @return the average now in force */
    float _track_dryness(ProgramContext& c);
};

}  // namespace Site

#endif // SITE_PROGRAM_IRRIGATION_MODULATION_H
