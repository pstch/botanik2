#ifndef SITE_TASKS_CLIMATE_H
#define SITE_TASKS_CLIMATE_H

/**
 * @file tasks_climate.h
 * @brief HAND-WRITTEN - The climate ladders: the day's shape, and the three demands.
 *
 * The spec's three control scales, each bringing its quantity to a demand
 * between -1 and 1 that the actuators spread themselves along, plus the day
 * phase that sets the target they are measured against. They command nothing
 * either: a ladder produces a number and the arbitration decides what moves.
 *
 * @note Included by tasks.h, which carries the file-set's rules and the map of
 * what these programs pass to each other. Not included directly.
 */

#include <cstdint>

#include "libbotanik.h"

#include "vars.h"

#include "generated/programs.gen.h"

// The rot margin the zone publishes is the same one this ladder acts on.
#include "tasks_sense.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The day's phase, and the temperature target it puts in force.
 *
 * The phase never commands an actuator: it sets the target and the permissions,
 * and the ladders below decide what moves. See the spec's phase table.
 *
 * The six phases are five boundaries, each declared as an offset in minutes
 * from sunrise or from sunset, so the whole table slides with the season
 * instead of being a clock an operator re-enters four times a year.
 */
class DayPhaseController : public Plc::Program {

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

    Refs::DayPhaseController _v;

public:
    DayPhaseController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::DayPhaseController::name(),
                  Refs::DayPhaseController::group(), io) {}

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override;

private:
    /** @brief The phase two offsets land in, with the two BANK phases skipped
     *  when yesterday did not arm them. */
    DayPhase _phase_at(int16_t from_rise, int16_t from_set) const;
};

/**
 * @brief The temperature the greenhouse is being held to: the base setting,
 *        shifted by whichever bank phase is both IN FORCE and ARMED.
 *
 * Its own program because the target is a decision, not a property of the
 * phase: the two bank phases only move it when yesterday justified them, and a
 * reader wants the number rather than the rule that produced it.
 */
class TargetTempController : public Plc::Program {

    Refs::TargetTempController _v;

public:
    TargetTempController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::TargetTempController::name(),
                  Refs::TargetTempController::group(), io) {}

    /** @brief The floor and ceiling the bank offsets may not push the target
     *  past, however aggressively they are set. */
    static const float TARGET_MIN;
    static const float TARGET_MAX;

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override;
};

/**
 * @brief The cooling ladder: a PID on the interior temperature against the
 *        phase's target, published as one signed demand.
 *
 * Negative is heating - the intractors drawing warm air from under the floor,
 * and below that the thermal screen. The actuators spread themselves along the
 * demand; this program does not know which they are.
 */
class CoolingController : public Plc::Program {

    Plc::Pid _pid;

    /** @name The intake gates
     *  Two comparisons of temperatures that CONVERGE - the house and the space
     *  under it - so each carries a dead band. Without one a gate flips beat to
     *  beat as the difference sits on its margin, and the intractors' whole
     *  contribution appears and vanishes with it.
     *  @{ */

    Plc::Hysteresis _cool_gate;    ///< the intake is cooler by the margin
    Plc::Hysteresis _ground_gate;  ///< ...or warmer by it
    Plc::Hysteresis _ground_dem;   ///< and the ladder is asking for heat

    /** @} */

    Refs::CoolingController _v;

public:
    CoolingController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::CoolingController::name(),
                  Refs::CoolingController::group(), io) {}

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override;

private:
    /** @brief Whether drawing crawlspace air would help, in either direction.
     *  Published rather than acted on: this law owns the reading, the
     *  intractors own the device. */
    void _intake_gates(ProgramContext& c, float inside, float demand);
};

/**
 * @brief The destratification ladder: three bands with hysteresis on the
 *        ceiling-to-floor gap, published as a demand.
 *
 * A band change needs the gap to move by the declared hysteresis, so the fans
 * do not step between speeds on noise.
 */
class DestratController : public Plc::Program {

    Plc::Staircase<3> _bands;

    Refs::DestratController _v;

public:
    DestratController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::DestratController::name(),
                  Refs::DestratController::group(), io) {}

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override;
};

/**
 * @brief The humidity ladder: four bands and a dead zone on the dew-point
 *        margin, published as one signed demand - and the rot guard under it.
 *
 * Negative dehumidifies (the margin is closing on condensation), positive
 * humidifies (the air is drier than the plants want). Set in degC rather than
 * kPa because it is CONDENSATION being guarded against, and that happens at
 * the temperature the surfaces are actually at.
 *
 * The GUARD is the same signal read against a different question: the ladder's
 * corners are the operator's comfort band, and `cli_tooHumid_degC` is the limit
 * `alarm_rot` means. One program, because two reading one margin can disagree
 * about it on the beat it moves. The guard's two rows are published from
 * observe(): they command nothing, and their verdict has to stand while the
 * plant is parked.
 */
class HumidityController : public Plc::Program {

    Plc::Tof _lockout_for;            ///< the lockout's overrun past the condition
    /** @brief The dryness gate's dead band: two dew points that converge, so
     *  the same treatment as the temperature gates next door. */
    Plc::Hysteresis _dry_gate_band;

    Refs::HumidityController _v;

public:
    HumidityController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::HumidityController::name(),
                  Refs::HumidityController::group(), io) {}

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;

private:
    /** @brief Whether the air outside would dry the greenhouse if drawn in.
     *  Dew points, so it survives a cold damp morning reading 95% RH. */
    void _dry_gate(ProgramContext& c);
};

}  // namespace Site

#endif // SITE_TASKS_CLIMATE_H
