/**
 * @file strat_sensor.h
 * @brief The declaration half of the `strat_sensor` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_STRAT_SENSOR_H
#define SITE_PROGRAM_STRAT_SENSOR_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The stratification gap: how much warmer the ceiling is than the floor,
 *        unsigned.
 */
class StratSensor final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float> ceiling;
    Vars::RefIn<float> floor;
    Vars::RefIn<bool>  hi_qlt;
    Vars::RefIn<bool>  lo_qlt;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> strat;

    /** @} */

    StratSensor(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "strat", Tasks::GROUP_FAST, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

inline void StratSensor::bind(Vars::Bank& io) {
    ceiling = _bind_input<float>(io, "cli_temp_hi");
    floor   = _bind_input<float>(io, "cli_temp_lo");
    hi_qlt  = _bind_input<bool>(io, "cli_temp_hi_qlt");
    lo_qlt  = _bind_input<bool>(io, "cli_temp_lo_qlt");
    strat   = _bind_output<float>(io, "cli_dTStrat_degC");

}

}  // namespace Site

#endif // SITE_PROGRAM_STRAT_SENSOR_H
