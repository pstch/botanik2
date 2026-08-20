#ifndef SITE_TASKS_H
#define SITE_TASKS_H

/**
 * @file tasks.h
 * @brief HAND-WRITTEN - the gh_mlb41 control programs.
 *
 * Not generated: this is the one place a site expresses *behaviour* rather than
 * schema. The rows they read and drive are in `tables/`; the maths they are
 * built from is generic (`plc/functions/`, `plc/blocks/`).
 *
 * A program holds its function blocks as members and its variable references
 * in `_v`, the `Refs` struct `generated/programs.gen.h` builds from
 * `tables/programs.toml`, and reads and drives them in run(). Three rules the
 * scaffolding guarantees, and which the programs rely on:
 *
 *  - every program in a beat sees the same instant (inputs refresh once, before
 *    any of them runs)
 *  - @c dt_ms is the REAL time since this program last ran, so a timer keeps
 *    correct time even when the scan was late
 *  - nothing runs at all while control authority is withheld, and the first
 *    activation after it returns says so (@c c.first_scan), which is where a
 *    program drops whatever the outage made stale
 *
 * ORDER IS EXECUTION ORDER within a phase, and these are declared in the order
 * the signal flows: conditioning, then the day's phase, then the three ladders,
 * then arbitration. A law reading a demand computed in the same beat needs the
 * producer to have run first. See docs/dev/scan.md.
 */

#include <cstdint>

#include "libbotanik.h"

#include "vars.h"

#include "generated/programs.gen.h"

namespace Site {

using namespace Botanik;

/** @brief Below this the humidity ladder is guarding against condensation
 *  rather than merely reporting it, and the rot alarm means it. Fixed, not a
 *  parameter: the operator's adjustable margins sit above it. In degC. */
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

    /** @brief Mirror the four readings and judge them.
     *  @param fault set when this supply is out of spec, or has gone silent
     *         after answering at least once
     *  @return whether the meter answered, so the caller can report itself
     *         degraded rather than publishing a verdict on nothing */
    bool step(Vars::Bank& io, bool& fault);

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

    static bool _rail(Vars::Bank& io, const Rail& r, uint32_t dt_ms, bool& seen);
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
     *  Fixed-alpha, so the filter is time-invariant: the zone runs at one rate
     *  and a constant blend keeps its response a property of the filter rather
     *  than of whatever dt the last beat happened to have.
     *  @{ */

    Utils::ExpMovingAverage _temp;
    Utils::ExpMovingAverage _rh;
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

    /** @} */

public:
    /** @brief Adopt this zone's three probes, the two conditioned rows they
     *  feed and its quorum alarm - all resolved by the program that owns them.
     *  @p alpha is the blend factor; see Utils::ema_alpha(). */
    void attach(const Vars::RefIn<float>* t, const Vars::RefIn<float>* h,
                Vars::RefOut<float> temp, Vars::RefOut<float> rh,
                Vars::RefOut<bool> fault, float alpha);

    /** @brief Select, filter and publish. @return how many probes were used. */
    uint8_t step(Vars::Bank& io, uint32_t dt_ms);

    float temperature(const Vars::Bank& io) const { return io.get(_temp_out); }
    float humidity(const Vars::Bank& io) const    { return io.get(_rh_out); }
    /** @brief Whether this zone published a believable reading this beat. */
    bool valid(const Vars::Bank& io) const { return io.var(_temp_out.r).valid(); }

    void reset() { _quorum_for.reset(); _temp_seeded = _rh_seeded = false; }

private:
    /** @brief The three readings of one quantity, reduced. Invalid probes do
     *  not take part; the outlier of what is left is dropped. */
    static ZoneVote _select(const Vars::Bank& io, const Vars::RefIn<float>* refs);

    /** @brief Filter and publish one quantity, or mark it unmeasured. */
    static void _publish(Vars::Bank& io, Vars::RefOut<float> row,
                         Utils::ExpMovingAverage& filter, const ZoneVote& v,
                         bool& seeded, float lo, float hi);
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

    /** @brief Borrowed, and site-owned: the base does not instantiate one. */
    const Sun::SunPositionTracker& _sun;

    Refs::DayPhaseController _v;

public:
    DayPhaseController(const SetupContext& ctx, Vars::Bank& io,
                       const Sun::SunPositionTracker& sun)
        : Program(ctx, Refs::DayPhaseController::name(),
                  Refs::DayPhaseController::group(), io), _sun(sun) {}

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override;

private:
    /** @brief Where the day is, in minutes either side of the two events.
     *  @return false while the tracker has no position, which is every boot
     *  before NTP: the phase then holds rather than jumping to NIGHT. */
    bool _solar_day(int16_t& from_rise, int16_t& from_set) const;

    /** @brief The phase those two offsets land in, with the two BANK phases
     *  skipped when yesterday did not arm them. */
    DayPhase _phase_at(const Vars::Bank& io, int16_t from_rise,
                       int16_t from_set) const;
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

    Plc::Tof _lockout_for;   ///< the lockout's overrun past the condition

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

/**
 * @brief One two-position valve: its coil, the position it fails to, and the
 *        pulse train that turns a fractional demand into an on-time.
 *
 * Not a Program - a valve is a part OF one, and both the irrigation cycle and
 * the wet skirt hold several. The fail direction comes from the ROW, so the
 * table stays the one place it is declared.
 */
class ValveController {

    Vars::RefOut<bool> _coil;
    bool     _fail_open = false;
    uint32_t _phase_ms  = 0;     ///< into the current modulation period

public:
    /** @brief Take @p coil, and its declared safe state as the fail direction. */
    void attach(const Vars::Bank& io, Vars::RefOut<bool> coil);

    void set(Vars::Bank& io, bool open) { io.drive(_coil, open); }
    /** @brief Drive it where a dead controller would leave it. */
    void park(Vars::Bank& io) { set(io, _fail_open); }
    bool fails_open() const { return _fail_open; }

    /** @brief Modulate: @p duty of @p period_ms, floored at @p min_on_ms -
     *  under which it stays shut, because a pulse that short does not lift the
     *  seat. @return the on-time fraction actually in force */
    float modulate(Vars::Bank& io, float duty, uint32_t period_ms,
                   uint32_t min_on_ms, uint32_t dt_ms);

    void reset() { _phase_ms = 0; }
};

/** @brief How many culture trays the plant has. */
static const uint8_t IRR_TRAYS = 8;

/**
 * @brief Irrigation: one tray at a time, filled to a dose and left to drain,
 *        with both dose and interval modulated by how thirsty the air is.
 *
 * A cycle is long - a fill plus a drain window per tray - so only
 * `irr_traysPerDay` are served in a day and the surplus waits for tomorrow.
 * The summer settings a tray carries are its DRY-WEATHER ones; as the air gets
 * damper `irr_mod` scales them down, and the split decides whether that shows
 * up as smaller doses or rarer ones.
 */
class IrrigationController : public Plc::Program {

    /** @name State
     *  @{ */

    IrrState _state       = IRR_IDLE;
    uint8_t  _tray        = 0;        ///< 1..IRR_TRAYS while serving, else 0
    uint8_t  _served      = 0;        ///< trays done since the day rolled
    uint32_t _in_state_ms = 0;
    /** @brief Pulse count when the current fill began: the dose is the
     *  DIFFERENCE, so a counter that has run since boot needs no reset. */
    uint32_t _fill_start_pulses = 0;
    /** @brief Litres back out of the tray since the drain window opened, and
     *  the dose it is judged against. The dose is kept because the modulation
     *  may move under a cycle that is already draining. */
    float    _drained_L  = 0.0f;
    float    _dose_L     = 0.0f;
    /** @brief When the hourly dryness fold last ran. */
    uint64_t _last_fold_ms = 0;
    /** @brief The UNIX day `_served` is counted against; the tally rolls when
     *  the date does, so a cycle spanning midnight is not counted twice. */
    uint32_t _served_day = 0;
    bool     _forced       = false;   ///< this cycle came from irr_forceNow

    /** @} */
    /** @name Blocks
     *  @{ */

    /** @brief The dryness tracker. Deliberately slow: an hour's fold over
     *  several days, so one dry afternoon cannot call for a watering. */
    Utils::ExpMovingAverage _dryness;
    bool                    _dryness_seeded = false;

    /** @brief The outlet meter, averaged over irr_out_avg_ms and published.
     *  A turbine through a 4-20 mA loop is noisy at low flow, and it is the
     *  average the drain verdict integrates. */
    Plc::Ewma     _out_flow;
    Plc::Deadband _out_band;

    /** @name The two water meters, as counters and as rates
     *
     *  Opposite ends of the same plant and opposite instruments: the inlet is a
     *  pulse train, so its COUNTER is the measurement and the rate is derived;
     *  the outlet is a 4-20 mA loop, so its RATE is the measurement and the
     *  counter is derived.
     *  @{ */

    Plc::Derivative _in_rate;    ///< litres -> Lpm, over a window
    Plc::Integral   _out_meter;  ///< Lpm -> litres

    /** @brief Where the inlet counter stood when this run adopted it: the pulse
     *  counter restarts at boot and the published total does not, so the total
     *  is the retained value plus what has been counted since. */
    float    _in_base_L      = 0.0f;
    uint32_t _in_base_pulses = 0;

    /** @} */

    /** @} */

    /** @name Valves
     *  The cycle's ten, each failing where its row declares. Held so a parked
     *  cycle leaves the plant where a dead controller would.
     *  @{ */

    /** @brief Two contactors on one manifold, per valve: the site switches the
     *  inlet and the drain twice each. */
    ValveController _inlet_v[2];
    ValveController _outlet_v[2];
    ValveController _tray_v[IRR_TRAYS];

    /** @} */

    Refs::IrrigationController _v;

public:
    IrrigationController(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::IrrigationController::name(),
                  Refs::IrrigationController::group(), io) {}

    /** @brief How often the dryness average folds. Not a parameter: the filter
     *  is written against this interval, and changing it silently rescales
     *  `irr_dryEwmaDays`. */
    static const uint64_t FOLD_MS;

protected:
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;

private:
    /** @brief Whether a cycle may START now. Two slots: the end of the night,
     *  and the first hours of the day for when the night was too cold to water
     *  in. Never the afternoon - a tray filled then is still wet at dusk. */
    bool _in_slot(const Vars::Bank& io) const;

    /** @brief The two supply faults the inlet meter can see: water moving with
     *  every valve shut, and water not arriving with the inlet open. */
    void _water_alarms(ProgramContext& c, float in_flow);

    /** @brief Integrate the outlet meter over one activation, and publish the
     *  running total. Invalid flow contributes nothing and is reported. */
    void _accumulate_drain(ProgramContext& c);

    /** @brief Judge the window that has just closed: did enough come back?
     *  A dose of zero or an unusable meter gives no verdict at all - a clog is
     *  a measurement, and there is nothing to measure. */
    void _drain_verdict(Vars::Bank& io);

    /** @brief Fold the VPD into the dryness average, at most once an hour.
     *  @return the average now in force */
    float _track_dryness(ProgramContext& c);
    /** @brief The modulation factor: modMin when the air is damp, 1 when it is
     *  summer-dry. Gamma of 1 or more keeps it failing toward LESS water. */
    float _modulation(const Vars::Bank& io, float vpd_avg) const;
    /** @brief The next tray owed water, or 0. Honours the per-tray forces.
     *  @param today UNIX day number, or 0 when the clock is not yet trusted */
    uint8_t _next_due(const Vars::Bank& io, float mod, uint32_t today) const;

    /** @brief Today as a UNIX day number, or 0 when the RTC has not been set.
     *  NTP owns the clock; before it answers there is no date to schedule
     *  against, and watering on a bogus one is worse than waiting. */
    static uint32_t _today();
    /** @brief Drive the valves for @p state, serving @p tray (0 = none). */
    void _set_valves(Vars::Bank& io, IrrState state, uint8_t tray);
    /** @brief Every valve to the position its row declares it fails to. */
    void _park_valves(Vars::Bank& io);
    /** @brief Set or clear @p per_tray for the tray being served. */
    void _blame(Vars::Bank& io, const Vars::RefOut<bool>* per_tray, bool on);
    /** @brief Litres into the current fill, off the inlet pulse counter. */
    float _delivered_litres(const Vars::Bank& io) const;
    /** @brief Record that the active tray has been watered, durably. */
    void _stamp_served(Vars::Bank& io);
    void _enter(Vars::Bank& io, IrrState state);
};

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

    /** @brief Time left in the current traverse, ms. Counted whichever way it
     *  is going: the fabric is in the fans' way in both. */
    uint32_t _travel_ms = 0;
    bool     _deployed  = false;
    bool     _seeded    = false;

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
    void _command(Vars::Bank& io, bool deploy);

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
    Plc::ActuatorConfig _mechanics(const Vars::Bank& io) const;
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
    float _safe_speed(const Vars::Bank& io) const;
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
    float _demand(const Vars::Bank& io) const;
    /** @brief The speed @p demand asks for, given the one in force. */
    uint8_t _band(const Vars::Bank& io, float demand);
    /** @brief Energise exactly one speed relay, or none. */
    void _drive(Vars::Bank& io, uint8_t speed);
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

#endif // SITE_TASKS_H
