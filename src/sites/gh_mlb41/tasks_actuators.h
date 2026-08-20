#ifndef SITE_TASKS_ACTUATORS_H
#define SITE_TASKS_ACTUATORS_H

/**
 * @file tasks_actuators.h
 * @brief HAND-WRITTEN - Arbitration: demands become positions.
 *
 * Where the ladders meet the plant. Each of these owns one device and resolves
 * every demand on it in the same order: mechanical interlocks first and
 * absolutely, then the safe state, then the maximum of what is left - the
 * demands act as FLOORS, never as targets. See the spec's arbitration table,
 * and docs/design/control-system.md for the one case it decides differently.
 *
 * @note Included by tasks.h, which carries the file-set's rules and the map of
 * what these programs pass to each other. Not included directly.
 */

#include <cstdint>

#include "libbotanik.h"

#include "vars.h"

#include "generated/programs.gen.h"

// The skirt is a modulated valve, and the valve block is the irrigation
// cycle's: one plant, one kind of solenoid.
#include "tasks_irrigation.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The thermal screen: one coil, deployed when the greenhouse is losing
 *        heat rather than gaining it.
 *
 * Two independent causes, either sufficient: the cooling ladder gone far enough
 * negative, or a cold night. No feedback, so travel is TIMED - and while it
 * runs the destratification fans are cut, because the fabric is moving through
 * the air they blow into.
 */
class ScreenController : public Plc::Program {

    /** @brief The traverse: a pulse of the fabric's travel time, started by a
     *  change of command. Whichever way it is going - the fabric is in the
     *  fans' way in both. */
    Plc::Tp    _travelling;
    Plc::RTrig _moved;
    bool       _deployed = false;
    bool       _seeded   = false;
    bool       _command_changed = false;

    Refs::ScreenController _v;

public:
    ScreenController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::ScreenController::name(),
                  Refs::ScreenController::group(), io) {}

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;

private:
    /** @brief Command the screen, starting a traverse if that is a change. */
    void _command(bool deploy);

    /** @brief Whether @p phase allows a move in that direction. The spec's
     *  permission table: the day's direction decides what the fabric may do,
     *  and the demand only decides whether it wants to. */
    static bool _allows(DayPhase phase, bool deploy);
};

/**
 * @brief The four roof windows: one target from the cooling ladder, and ONE
 *        MOVING AT A TIME.
 *
 * The one-at-a-time rule is electrical, so a safety cannot break it either: a
 * rain interlock shuts them in sequence, not together. A window keeps the bus
 * until its stroke ends, which is also what stops a target changing mid-travel
 * from handing the bus to the next window and leaving a coil energised.
 *
 * The humidity demand is not read. Anti-dryness would ask them shut and the
 * spec is explicit that cooling wins over it; with no cooling demand the target
 * is 0 and they are shut regardless, so absence is the rule rather than a
 * merge. Taking a MINIMUM of the two was proposed and rejected: it inverts
 * exactly the dry-and-hot case.
 */
class WindowsController : public Plc::Program {

    /** @name State
     *  @{ */

    Plc::Actuator _act[4];
    Plc::Tof      _rain_for;   ///< the rain lockout's overrun past the last drop
    /** @brief The position the arbitration asked for this scan. One number for
     *  all four: they are one roof. */
    float         _target = 0.0f;

    /** @} */

    Refs::WindowsController _v;

public:
    /** @brief Where the roof goes when this law is not acting: PART OPEN, not
     *  shut. It should never be reached - the interior is measured by three
     *  probes at each of two levels precisely so that it is not - but if it is,
     *  a greenhouse with some ventilation survives a summer afternoon and a
     *  sealed one does not.
     *  @note NOT the interlocks: rain and wind shut the roof outright, and a
     *  mechanical safety is resolved before any of this. */
    static const float SAFE_OPEN;

private:

public:
    WindowsController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::WindowsController::name(),
                  Refs::WindowsController::group(), io) {}

    /** @brief How many windows the roof has. */
    static const uint8_t WINDOWS = 4;

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;

private:
    /** @brief The mechanics, rebuilt each scan from the parameters: a
     *  recalibrated travel time has to land on the next stroke, not the next
     *  boot. */
    Plc::ActuatorConfig _mechanics() const;
    /** @brief Whether anything forbids the windows being open at all. Publishes
     *  the two reasons it owns. */
    bool _shut_in(ProgramContext& c);
    /** @brief Which window may move this scan, or -1. */
    int8_t _owner(float target, const Plc::ActuatorConfig& cfg) const;
};

/**
 * @brief The two intractors: variable-speed fans drawing air from under the
 *        floor, driven by whichever of three demands wants the most.
 *
 * Both run together at one speed - they serve one volume - and the fusion is a
 * MAXIMUM, so each input is a floor rather than a share. The ramp runs between
 * the two staging thresholds, so the windows below it and the intractors above
 * it hand over rather than overlap.
 */
class IntractorController : public Plc::Program {

    Refs::IntractorController _v;

public:
    IntractorController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::IntractorController::name(),
                  Refs::IntractorController::group(), io) {}

    /** @brief How many intractors the crawlspace has. */
    static const uint8_t INTRACTORS = 2;

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override;

private:
    /** @brief Where these go when this law is not acting, off the row's own
     *  declared safe value. NOT stopped: some air is always wanted. */
    float _safe_speed() const;
};

/**
 * @brief The two destratification fans: three relayed speeds, driven by
 *        whichever of three demands wants the most.
 *
 * The speed relays are exclusive - one contactor per speed on one motor - and
 * the band the fused demand falls in decides which. That band has its own
 * hysteresis: the inputs are floors, so the winner changes as the plant does,
 * and the fans would step between speeds every time it did.
 *
 * The screen cuts them outright while it travels: the fabric moves through the
 * air they blow into.
 */
class FanController : public Plc::Program {

    /** @brief 0 (off) to 3, and the three rungs it is chosen on. Held across
     *  scans: the hysteresis lives on the rungs. */
    uint8_t _speed = 0;
    Plc::Staircase<3> _steps;

    Refs::FanController _v;

public:
    FanController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::FanController::name(),
                  Refs::FanController::group(), io) {}

    /** @brief How many destratification fans the roof has. */
    static const uint8_t FANS = 2;

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override;

private:
    /** @brief The three demands, fused. */
    float _demand() const;
    /** @brief The speed @p demand asks for, given the one in force. */
    uint8_t _band(float demand);
    /** @brief Energise exactly one speed relay, or none. */
    void _drive(uint8_t speed);
};

/**
 * @brief The wet skirt: one valve pulsed against two demands.
 *
 * It is the humidifier, and it tracks the humidify arm PROPORTIONALLY - the
 * effort is the on-time within a period. It is also an evaporative cooler, and
 * on that axis it is STAGED, above cli_skirtCoolDem and only while the air is
 * dry enough for a wet pad to cool anything at all.
 *
 * The rot lockout cuts it outright. Wetting air that is already condensing is
 * the one thing this valve must never do.
 */
class SkirtController : public Plc::Program {

    ValveController _valve;

    Refs::SkirtController _v;

public:
    SkirtController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::SkirtController::name(),
                  Refs::SkirtController::group(), io) {}

protected:
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

}  // namespace Site

#endif // SITE_TASKS_ACTUATORS_H
