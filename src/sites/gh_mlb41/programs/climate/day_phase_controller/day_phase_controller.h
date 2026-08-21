#ifndef SITE_PROGRAM_DAY_PHASE_CONTROLLER_H
#define SITE_PROGRAM_DAY_PHASE_CONTROLLER_H

/**
 * @file day_phase_controller.h
 * @brief HAND-WRITTEN - the day's phase, and the target it puts in force.
 */

#include <cstdint>

#include "../../../vars.h"
#include "day_phase_controller.gen.h"

namespace Site {

using namespace Botanik;

class DayPhaseController final : public Gen::DayPhaseController {

    /** @name State
     *  @{ */

    DayPhase _phase = PHASE_NIGHT;
    /** @brief The interior extremes since the current day and night began.
     *  What COLD_BANK and HEAT_BANK are gated on: pre-cool only after a hot
     *  day, bank heat only after a cold night. */
    float _day_max   = 0.0f;
    float _night_min = 0.0f;
    bool  _seeded    = false;

    /** @} */

public:
    DayPhaseController(const SetupContext& ctx, Vars::Bank& io)
        : Gen::DayPhaseController(ctx, io) {}

protected:
    void run(ProgramContext& c) override;

private:
    /** @brief The phase two offsets land in, with the two BANK phases skipped
     *  when yesterday did not arm them. */
    DayPhase _phase_at(int16_t from_rise, int16_t from_set) const;
};

}  // namespace Site

#endif // SITE_PROGRAM_DAY_PHASE_CONTROLLER_H
