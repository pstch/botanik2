#ifndef SITE_PROGRAM_IRRIGATION_CYCLE_H
#define SITE_PROGRAM_IRRIGATION_CYCLE_H

/**
 * @file irrigation_cycle.h
 * @brief HAND-WRITTEN - one tray at a time, filled to a dose, steeped, and drained.
 */

#include <cstdint>

#include "../../../vars.h"
#include "irrigation_cycle.gen.h"

namespace Site {

using namespace Botanik;

class IrrigationCycle final : public Gen::IrrigationCycle {

    /** @name State
     *  @{ */

    IrrState _state       = IRR_IDLE;
    uint8_t  _tray        = 0;        ///< 1..N_TRAY_VALVE while serving, else 0
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

public:
    IrrigationCycle(const SetupContext& ctx, Vars::Bank& io)
        : Gen::IrrigationCycle(ctx, io) {}

protected:
    void on_bind() override;
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

#endif // SITE_PROGRAM_IRRIGATION_CYCLE_H
