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
 * @brief One AC supply, watched: four readings mirrored onto status rows and
 *        judged against their bands.
 *
 * Not a Program - a supply is what a program watches, and there are two of
 * them. Held by value, so the grid's deadbands cannot become the local
 * supply's. Bound from either program's `Refs` by template: the two declare
 * the same member names, and the compiler is what holds them to it.
 *
 * The mirror exists because a field-read row carries no Modbus register of its
 * own - only parameters, status values and alarms are mapped - so an external
 * master cannot see the meter without one.
 */
class AcSupply {

    /** @name Blocks
     *  One deadband per quantity: the meter answers every few seconds and its
     *  last digit never settles, which would otherwise be a telemetry publish
     *  and a register write per poll.
     *  @{ */

    Plc::Deadband _v, _a, _w, _hz;
    /** @brief Whether this meter has EVER answered. Before it has, silence is
     *  bring-up; after, it is a fault. */
    bool          _seen = false;

    /** @} */
    /** @name Variables
     *  @{ */

    Vars::RefIn<float>  _in_v, _in_a, _in_w, _in_hz;
    Vars::RefIn<float>  _lo_v, _hi_v, _lo_hz, _hi_hz;
    Vars::RefOut<float> _out_v, _out_a, _out_w, _out_hz;
    Vars::RefOut<bool>  _ok;

    /** @} */

public:
    /** @brief Adopt @p r's references. Any `Refs` struct spelling the members
     *  the same way will do, which is the point: one law, two supplies. */
    template <typename R>
    void attach(const R& r) {
        _in_v = r.volts;  _in_a  = r.amps;  _in_w   = r.watts; _in_hz = r.hz;
        _lo_v = r.lo_V;   _hi_v  = r.hi_V;  _lo_hz  = r.lo_Hz; _hi_hz = r.hi_Hz;
        _out_v = r.out_V; _out_a = r.out_A; _out_w  = r.out_W; _out_hz = r.out_Hz;
        _ok    = r.ok;
    }

    /** @brief One reading onto its status row, through the deadband, or the row
     *  marked unmeasured. */
    static void _mirror(Vars::RefOut<float> out, Plc::Deadband& band,
                        Vars::RefIn<float> in, float width);

    /** @brief Mirror the four readings and judge them.
     *  @param fault set when this supply is out of spec, or has gone silent
     *         after answering at least once
     *  @return whether the meter answered, so the caller can report itself
     *         degraded rather than publishing a verdict on nothing */
    bool step(bool& fault);

    void reset();
};

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

/** @brief What a three-probe selection came to: the reading, and how many
 *  probes were behind it. @c used of 0 means nothing was believable. */
struct ZoneVote {
    float   value;
    uint8_t used;
};

/**
 * @brief One measurement location: three probes selected down to one reading,
 *        then filtered.
 *
 * The selection drops the OUTLIER and averages the two that agree, which keeps
 * the second good probe's information where a plain median discards it. Only
 * VALID probes take part; with none left the zone's rows go invalid rather than
 * publishing a number nothing measured.
 *
 * Not a Program - a location is a part OF one. Held by value so one zone's
 * filter state cannot be shared with another's by accident.
 */
class ZoneProbes {

    /** @name Blocks
     *  Filtered against dt, not against a fixed blend: a program due late runs
     *  ONCE with the real elapsed time, so a filter carrying a constant alpha
     *  silently changes its time constant the moment the scan slips. The tau is
     *  the same for every zone (SENSOR_TAU_MS) and the block does the rest.
     *  @{ */

    Plc::Ewma _temp;
    Plc::Ewma _rh;
    Plc::Ton                _quorum_for;   ///< dwell before a lost probe is announced
    bool                    _temp_seeded = false;
    bool                    _rh_seeded   = false;

    /** @} */
    /** @name Variables
     *  @{ */

    Vars::RefIn<float>  _t[3];
    Vars::RefIn<float>  _h[3];
    Vars::RefOut<float> _temp_out;
    Vars::RefOut<float> _rh_out;
    Vars::RefOut<bool>  _fault;
    float               _tau_s = 1.0f;

    /** @} */

public:
    /** @brief Adopt this zone's three probes, the two conditioned rows they
     *  feed and its quorum alarm - all resolved by the program that owns them.
     *  @p tau_s is the filter's time constant, in seconds. */
    void attach(const Vars::RefIn<float>* t, const Vars::RefIn<float>* h,
                Vars::RefOut<float> temp, Vars::RefOut<float> rh,
                Vars::RefOut<bool> fault, float tau_s);

    /** @brief Select, filter and publish. @return how many probes were used. */
    uint8_t step(uint32_t dt_ms);

    float temperature() const { return _temp_out.get(); }
    float humidity() const    { return _rh_out.get(); }
    /** @brief Whether this zone published a believable reading this beat. */
    bool valid() const { return _temp_out.valid(); }

    void reset() {
        _quorum_for.reset();
        _temp.reset(); _rh.reset();
        _temp_seeded = _rh_seeded = false;
    }

private:
    /** @brief The three readings of one quantity, reduced. Invalid probes do
     *  not take part; the outlier of what is left is dropped. */
    static ZoneVote _select(const Vars::RefIn<float>* refs);

    /** @brief Filter and publish one quantity, or mark it unmeasured. */
    static void _publish(Vars::RefOut<float> row, Plc::Ewma& filter,
                         const ZoneVote& v, bool& seeded, float tau_s,
                         uint32_t dt_ms, float lo, float hi);
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
    bool _seen = false;

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
