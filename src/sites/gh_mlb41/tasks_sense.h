#ifndef SITE_TASKS_SENSE_H
#define SITE_TASKS_SENSE_H

/**
 * @file tasks_sense.h
 * @brief HAND-WRITTEN - Conditioning: the instruments, read and judged.
 *
 * Nothing here commands anything. Every program in this file turns readings
 * into rows the rest of the site is written against: the two supplies, the DC
 * rails, the four probe zones and their psychrometrics, the stratification gap,
 * the weather station and the sun. They run FIRST in the beat, and most of them
 * live entirely in observe() - an observation does not stop being true because
 * control was withdrawn.
 *
 * @note Included by tasks.h, which carries the file-set's rules and the map of
 * what these programs pass to each other. Not included directly.
 */

#include <cstdint>

#include "libbotanik.h"

#include "vars.h"

#include "blocks/ac_supply.h"
#include "blocks/zone_probes.h"

#include "generated/programs.gen.h"

namespace Site {

using namespace Botanik;

/** @brief Below this the humidity ladder is guarding against condensation
 *  rather than merely reporting it, and the rot alarm means it. Fixed, not a
 *  parameter: the operator's adjustable margins sit above it. In degC.
 *  @note Read by the zone that computes the margin and by the ladder that acts
 *  on it, which is why it is here rather than in either. */
static const float ROT_MARGIN_DEGC = 1.0f;

/**
 * @brief The utility feed, watched.
 *
 * A watch program: empty run(), everything in observe(). What it publishes is
 * an observation, and observations are exactly what must survive the plant
 * being parked.
 */
class AcGridWatch : public Plc::Program {

    AcSupply _supply;

    Refs::AcGridWatch _v;

public:
    AcGridWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::AcGridWatch::name(),
                  Refs::AcGridWatch::group(), io) {}

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); _supply.attach(_v); }
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;
};

/**
 * @brief The generator-backed side of the transfer switch, watched.
 *
 * The same law as the grid over different rows and wider frequency bands: a
 * generator wanders where a utility does not.
 */
class AcLocalWatch : public Plc::Program {

    AcSupply _supply;
    /** @brief How long the grid has been gone with nothing taking over. A
     *  generator has a cranking time, so only the WAIT is a failure. */
    Plc::Ton _cranking;

    Refs::AcLocalWatch _v;

public:
    AcLocalWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::AcLocalWatch::name(),
                  Refs::AcLocalWatch::group(), io) {}

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); _supply.attach(_v); }
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;
};

/**
 * @brief The DC rails and the two batteries, watched - and the plant-wide
 *        lockout they raise.
 *
 * `sys_supply_lockout` follows the UPS LOW-BATTERY CONTACT, not a voltage: the
 * UPS knows what its own battery has left, and a divider on the rail does not.
 * The voltages alarm on their own floors, which is warning rather than
 * interlock.
 */
class DcWatch : public Plc::Program {

    /** @name Blocks
     *  @{ */

    Plc::Deadband _dc1, _dc2, _ups, _gen;
    /** @brief One rolling average per divider, its time constant a parameter
     *  per meter: the rail is judged on the AVERAGE, so a single noisy sample
     *  cannot raise a supply fault. */
    Plc::Ewma     _avg1, _avg2, _avgu, _avgg;
    Plc::Ton      _low_for;    ///< debounce: a contact bounces, a UPS does not
    /** @brief Per rail, whether its divider has EVER answered. @see AcSupply */
    bool          _seen[4] = { false, false, false, false };
    /** @brief What this law last told the registry, so the plant-wide hold is
     *  pushed on the EDGE and not walked over every task every beat. */
    bool          _safe_held = false;

    /** @} */

    Refs::DcWatch _v;

public:
    DcWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::DcWatch::name(), Refs::DcWatch::group(), io) {}

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;

private:
    /** @brief One rail: average it, mirror it through its deadband, and judge
     *  the average against @p lo.
     *  @param seen this rail's "has ever answered" latch, updated here
     *  @return false when the rail is under its floor, or has gone silent
     *          after answering at least once */
    struct Rail {
        Plc::Ewma&           avg;
        Plc::Deadband&       band;
        Vars::RefIn<float>   in;
        Vars::RefOut<float>  out;
        Vars::RefIn<float>   lo;
        Vars::RefIn<uint16_t> avg_ms;
    };

    static bool _rail(const Rail& r, uint32_t dt_ms, bool& seen);
};

/**
 * @brief The conditioned picture of the greenhouse: four locations voted and
 *        filtered, then the psychrometrics every other law is set in.
 *
 * Runs FIRST. Everything downstream reads its rows rather than the probes.
 */
class TempRHZone : public Plc::Program {

    /** @name Blocks
     *  @{ */

    ZoneProbes _ceiling, _floor, _crawlspace, _exterior;

    /** @} */

    Refs::TempRHZone _v;

public:
    TempRHZone(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::TempRHZone::name(),
                  Refs::TempRHZone::group(), io) {}

protected:
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
};

/**
 * @brief The stratification gap: how much warmer the ceiling is than the floor,
 *        unsigned.
 *
 * Its own program because it is a reading ABOUT two zones rather than part of
 * either, and because the destratification controller wants exactly one number
 * from it.
 */
class StratSensor : public Plc::Program {

    Refs::StratSensor _v;

public:
    StratSensor(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::StratSensor::name(),
                  Refs::StratSensor::group(), io) {}

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override;
};

/**
 * @brief The interior conditions, watched: the alarms that are a READING
 *        crossing a line rather than a law failing to hold one.
 *
 * A watch program - empty run(), everything in observe(). An alarm is an
 * observation, and an observation has to keep being made while the plant is
 * parked, which is exactly when someone is looking at the ALARMS page.
 *
 * An unmeasured reading raises NOTHING here. What it raises is the zone's own
 * quorum alarm, which this rolls up: alarming twice for one dead probe would
 * make the second alarm mean "and also we cannot see".
 */
class ClimateWatch : public Plc::Program {

    Refs::ClimateWatch _v;

public:
    ClimateWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::ClimateWatch::name(),
                  Refs::ClimateWatch::group(), io) {}

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;
};

/**
 * @brief The weather station, watched: whether it is answering, and whether
 *        what it says is a storm.
 *
 * A storm IS what shuts the roof, so this reads `WindowsController`'s two
 * lockouts rather than declaring a second pair of thresholds beside them -
 * an alarm that disagreed with the actuator it is about would be worse than
 * no alarm.
 */
class WeatherWatch : public Plc::Program {

    /** @brief Whether the station has EVER answered. Before it has, silence is
     *  bring-up; after, it is a fault. @see AcSupply */
    bool     _seen = false;
    /** @brief Ms since the gauge last read wet, and whether it ever has in this
     *  run. A boot does not know when it last rained, so before the first
     *  shower the row is held rather than reading "just now". */
    uint64_t _dry_ms = 0;
    bool     _wet_seen = false;

    Refs::WeatherWatch _v;

public:
    WeatherWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::WeatherWatch::name(),
                  Refs::WeatherWatch::group(), io) {}

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;
};

/**
 * @brief Where the sun is, published as rows.
 *
 * The tracker is INSIDE this program: it is an instrument, and what turns an
 * instrument into rows is a watch, exactly as the field-bus client does for the
 * probes. Everything downstream reads the rows - a law that wants to know where
 * the day is asks the table, not the tracker - so no other program needs a
 * reference to anything but its own interface.
 *
 * Configured from the `geo_` pair through Vars::bind_sun(), which the firmware
 * hands tracker() at bring-up.
 */
class SunWatch : public Plc::Program {

    Sun::SunPositionTracker _sun;

    Refs::SunWatch _v;

public:
    SunWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::SunWatch::name(), Refs::SunWatch::group(), io),
          _sun(ctx) {}

    /** @brief The instrument, for the config binder. Nothing else needs it. */
    Sun::SunPositionTracker& tracker() { return _sun; }

    /** @brief This program's own entry, and the tracker's: the recompute is a
     *  slow SERVICE task of its own, not something a control phase does. */
    void register_tasks() override {
        Program::register_tasks();
        _sun.register_tasks();
    }

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;

private:
    /** @brief Whole minutes, clamped to a day either side of the event. */
    static int16_t _minutes(int64_t v);
};

}  // namespace Site

#endif // SITE_TASKS_SENSE_H
