#ifndef SITE_TASKS_IRRIGATION_H
#define SITE_TASKS_IRRIGATION_H

/**
 * @file tasks_irrigation.h
 * @brief HAND-WRITTEN - Irrigation: a ladder, two meters, and the cycle that acts.
 *
 * Three programs along the same seam as the climate side: how thirsty the air
 * has been (a factor), what the two meters say (an instrument), and the state
 * machine that fills one tray at a time and drains it.
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
    void attach(Vars::RefOut<bool> coil);

    void set(bool open) { _coil.set(open); }
    /** @brief Drive it where a dead controller would leave it. */
    void park() { set(_fail_open); }
    bool fails_open() const { return _fail_open; }

    /** @brief Modulate: @p duty of @p period_ms, floored at @p min_on_ms -
     *  under which it stays shut, because a pulse that short does not lift the
     *  seat. @return the on-time fraction actually in force */
    float modulate(float duty, uint32_t period_ms, uint32_t min_on_ms,
                   uint32_t dt_ms);

    void reset() { _phase_ms = 0; }
};

/** @brief How many culture trays the plant has. */
static const uint8_t IRR_TRAYS = 8;

/**
 * @brief The irrigation ladder: how thirsty the air has been, as one factor.
 *
 * The spec's irrigation "regulator" - evaporation in, a modulation factor out.
 * It commands nothing, so all of it is in observe() and it keeps folding while
 * the plant is parked: an outage is not a dry spell, and a hole in the average
 * would read as one for days afterwards.
 */
class IrrigationModulation : public Plc::Program {

    /** @brief The dryness tracker. Deliberately slow: an hour's fold over
     *  several days, so one dry afternoon cannot call for a watering. */
    Utils::ExpMovingAverage _dryness;
    bool                    _seeded = false;
    uint64_t                _last_fold_ms = 0;

    Refs::IrrigationModulation _v;

public:
    IrrigationModulation(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::IrrigationModulation::name(),
                  Refs::IrrigationModulation::group(), io) {}

    /** @brief How often the dryness average folds. Not a parameter: the filter
     *  is written against this interval, and changing it silently rescales
     *  `irr_dryEwmaDays`. */
    static const uint64_t FOLD_MS;

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;

private:
    /** @brief Fold the VPD into the dryness average, at most once an hour.
     *  @return the average now in force */
    float _track_dryness(ProgramContext& c);
};

/**
 * @brief Both water meters, and the two supply faults they can see.
 *
 * Opposite ends of the same circuit and opposite instruments: the inlet is a
 * pulse train, so its COUNTER is the measurement and the rate is derived; the
 * outlet is a 4-20 mA loop, so its RATE is the measurement and the counter is.
 * An instrument, not a law - it drives no valve, and it reads while parked.
 */
class WaterMeters : public Plc::Program {

    /** @name Blocks
     *  @{ */

    Plc::Ewma       _out_flow;   ///< the loop, averaged over irr_out_avg_ms
    Plc::Deadband   _out_band;   ///< ...and how far it must move to be written
    Plc::Derivative _in_rate;    ///< litres -> Lpm, over a window
    Plc::Integral   _out_meter;  ///< Lpm -> litres

    /** @} */

    /** @brief Where the inlet counter stood when this run adopted it: the pulse
     *  counter restarts at boot and the published total does not, so the total
     *  is the retained value plus what has been counted since. */
    float    _in_base_L      = 0.0f;
    uint32_t _in_base_pulses = 0;

    Refs::WaterMeters _v;

public:
    WaterMeters(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::WaterMeters::name(),
                  Refs::WaterMeters::group(), io) {}

protected:
    void bind(Vars::Bank& io) override { _v.bind(io, *this); }
    void run(ProgramContext& c) override { (void)c; }
    void observe(ProgramContext& c) override;

private:
    /** @brief Water moving with every valve shut, and water not arriving with
     *  the inlet open. Both are read off the inlet meter alone. */
    void _supply_alarms(ProgramContext& c, float in_flow);
};

/**
 * @brief Irrigation: one tray at a time, filled to a dose, steeped, and drained.
 *
 * FILLING -> SOAKING -> DRAINING -> FLUSHING, and a cycle is long - so only
 * `irr_traysPerDay` are served in a day and the surplus waits for tomorrow.
 * The summer settings a tray carries are its DRY-WEATHER ones; `irr_mod` scales
 * them down as the air gets damper, and the split decides whether that shows up
 * as smaller doses or rarer ones.
 *
 * It holds no meter state: what went in and what came back out are both read
 * as published TOTALS and differenced across the window that is being judged.
 */
class IrrigationCycle : public Plc::Program {

    /** @name State
     *  @{ */

    IrrState _state       = IRR_IDLE;
    uint8_t  _tray        = 0;        ///< 1..IRR_TRAYS while serving, else 0
    uint8_t  _served      = 0;        ///< trays done since the day rolled
    uint32_t _in_state_ms = 0;
    /** @brief The two meters' totals when the window under judgement opened.
     *  Litres are the DIFFERENCE, so a counter that has run since boot - or
     *  since last year, retained - needs no reset. */
    float    _fill_start_L  = 0.0f;
    float    _drain_start_L = 0.0f;
    /** @brief What the tray actually got, kept because the modulation may move
     *  under a cycle that is already draining. */
    float    _dose_L     = 0.0f;
    /** @brief The UNIX day `_served` is counted against; the tally rolls when
     *  the date does, so a cycle spanning midnight is not counted twice. */
    uint32_t _served_day = 0;
    bool     _forced       = false;   ///< this cycle came from irr_forceNow

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

    Refs::IrrigationCycle _v;

public:
    IrrigationCycle(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, Refs::IrrigationCycle::name(),
                  Refs::IrrigationCycle::group(), io) {}

protected:
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;

private:
    /** @brief Whether a cycle may START now. Two slots: the end of the night,
     *  and the first hours of the day for when the night was too cold to water
     *  in. Never the afternoon - a tray filled then is still wet at dusk. */
    bool _in_slot() const;

    /** @brief Judge the window that has just closed: did enough come back?
     *  A dose of zero or an unusable meter gives no verdict at all - a clog is
     *  a measurement, and there is nothing to measure. */
    void _drain_verdict();

    /** @brief The next tray owed water, or 0. Honours the per-tray forces, and
     *  publishes the modulated interval of whichever tray it picks.
     *  @param today UNIX day number, or 0 when the clock is not yet trusted */
    uint8_t _next_due(float mod, uint32_t today);

    /** @brief Today as a UNIX day number, or 0 when the RTC has not been set.
     *  NTP owns the clock; before it answers there is no date to schedule
     *  against, and watering on a bogus one is worse than waiting. */
    static uint32_t _today();
    /** @brief Drive the valves for @p state, serving @p tray (0 = none). */
    void _set_valves(IrrState state, uint8_t tray);
    /** @brief Every valve to the position its row declares it fails to. */
    void _park_valves();
    /** @brief Set or clear @p per_tray for the tray being served. */
    void _blame(const Vars::RefOut<bool>* per_tray, bool on);
    /** @brief Record that the active tray has been watered, durably, and spend
     *  the force that asked for it. */
    void _stamp_served();
    /** @brief Stamp tray @p tray (0-based) as watered on UNIX day @p day. */
    void _stamp(uint8_t tray, uint32_t day) const;
    /** @brief Spend a one-shot force. */
    static void _clear_force(Vars::RefInOut<bool> r);
    /** @brief What follows a fill: the soak, or the drain when there is none. */
    IrrState _after_fill() const;
    void _enter(IrrState state);
};

}  // namespace Site

#endif // SITE_TASKS_IRRIGATION_H
