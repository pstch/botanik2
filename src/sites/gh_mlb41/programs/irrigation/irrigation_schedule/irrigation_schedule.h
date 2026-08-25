/**
 * @file irrigation_schedule.h
 * @brief The declaration half of the `irrigation_schedule` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_IRRIGATION_SCHEDULE_H
#define SITE_PROGRAM_IRRIGATION_SCHEDULE_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief Which tray is owed water, and whether the climate will allow a cycle to
 *        start.
 */
class IrrigationSchedule final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float>    mod;
    Vars::RefIn<float>    split;
    Vars::RefIn<uint16_t> trays_per_day;
    Vars::RefIn<uint8_t>  days[8];
    Vars::RefIn<float>    outside_temp;
    Vars::RefIn<float>    dawn_gate;
    Vars::RefIn<uint8_t>  phase;
    Vars::RefIn<int16_t>  from_rise;
    Vars::RefIn<int16_t>  day_rise;
    Vars::RefIn<uint16_t> morning_win;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<uint8_t> due_tray;
    Vars::RefOut<bool>    slot_open;
    Vars::RefOut<float>   period;

    /** @} */

    /** @name VAR_IN_OUT
     *  @{ */

    Vars::RefInOut<uint32_t> last[8];
    Vars::RefInOut<bool>     force_on[8];
    Vars::RefInOut<bool>     force_off[8];
    Vars::RefInOut<bool>     force_now;
    Vars::RefInOut<uint32_t> today;
    Vars::RefInOut<uint8_t>  served;

    /** @} */

private:

    /** @name VAR - what this law remembers
     *  @{ */

    /** @brief The UNIX day the tally is counted against; it rolls when the
     *  date does, so a cycle spanning midnight is not counted twice. */
    uint32_t _served_day = 0;

    /** @} */

    /** @name Sizes - what a body loops over
     *  @{ */

    static const uint8_t N_DAYS      = 8;
    static const uint8_t N_LAST      = 8;
    static const uint8_t N_FORCE_ON  = 8;
    static const uint8_t N_FORCE_OFF = 8;

    /** @} */

    Vars::VarRef _watch_rows[6];  ///< swept for validity before each activation

public:
    IrrigationSchedule(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "irr_schedule", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;

private:

    /** @brief The tray owed water, or 0. Honours the per-tray forces, and
     *  publishes the modulated interval of whichever tray it picks. */
    uint8_t _next_due(float mod_now, uint32_t day);
    /** @brief Whether a cycle may START now. Two slots: the end of the
     *  night, and the first hours of the day for when the night was too
     *  cold to water in. Never the afternoon - a tray filled then is still
     *  wet at dusk.
     *  @param known cleared when the rows the answer rests on are not
     *         believable; WHICH those are depends on the phase, so the test
     *         is what reports it */
    bool    _in_slot(bool& known) const;
    /** @brief Stamp tray @p tray (0-based) as watered on UNIX day @p day. */
    void    _stamp(uint8_t tray, uint32_t day) const;
};

inline void IrrigationSchedule::bind(Vars::Bank& io) {
    mod           = io.resolve_in<float>("irr_mod");
    split         = io.resolve_in<float>("irr_volFreqSplit");
    trays_per_day = io.resolve_in<uint16_t>("irr_traysPerDay");
    days[0]       = io.resolve_in<uint8_t>("irr_days_tray1");
    days[1]       = io.resolve_in<uint8_t>("irr_days_tray2");
    days[2]       = io.resolve_in<uint8_t>("irr_days_tray3");
    days[3]       = io.resolve_in<uint8_t>("irr_days_tray4");
    days[4]       = io.resolve_in<uint8_t>("irr_days_tray5");
    days[5]       = io.resolve_in<uint8_t>("irr_days_tray6");
    days[6]       = io.resolve_in<uint8_t>("irr_days_tray7");
    days[7]       = io.resolve_in<uint8_t>("irr_days_tray8");
    outside_temp  = io.resolve_in<float>("cli_temp_ext");
    dawn_gate     = io.resolve_in<float>("irr_dawnGate_degC");
    phase         = io.resolve_in<uint8_t>("cli_phase");
    from_rise     = io.resolve_in<int16_t>("sol_fromRise_min");
    day_rise      = io.resolve_in<int16_t>("sol_dayRise_min");
    morning_win   = io.resolve_in<uint16_t>("irr_morningWin_min");
    due_tray      = io.resolve_out<uint8_t>("irr_dueTray");
    slot_open     = io.resolve_out<bool>("irr_slotOpen");
    period        = io.resolve_out<float>("irr_period_days");
    last[0]       = io.resolve_inout<uint32_t>("irr_last_tray1");
    last[1]       = io.resolve_inout<uint32_t>("irr_last_tray2");
    last[2]       = io.resolve_inout<uint32_t>("irr_last_tray3");
    last[3]       = io.resolve_inout<uint32_t>("irr_last_tray4");
    last[4]       = io.resolve_inout<uint32_t>("irr_last_tray5");
    last[5]       = io.resolve_inout<uint32_t>("irr_last_tray6");
    last[6]       = io.resolve_inout<uint32_t>("irr_last_tray7");
    last[7]       = io.resolve_inout<uint32_t>("irr_last_tray8");
    force_on[0]   = io.resolve_inout<bool>("irr_forceOn_tray1");
    force_on[1]   = io.resolve_inout<bool>("irr_forceOn_tray2");
    force_on[2]   = io.resolve_inout<bool>("irr_forceOn_tray3");
    force_on[3]   = io.resolve_inout<bool>("irr_forceOn_tray4");
    force_on[4]   = io.resolve_inout<bool>("irr_forceOn_tray5");
    force_on[5]   = io.resolve_inout<bool>("irr_forceOn_tray6");
    force_on[6]   = io.resolve_inout<bool>("irr_forceOn_tray7");
    force_on[7]   = io.resolve_inout<bool>("irr_forceOn_tray8");
    force_off[0]  = io.resolve_inout<bool>("irr_forceOff_tray1");
    force_off[1]  = io.resolve_inout<bool>("irr_forceOff_tray2");
    force_off[2]  = io.resolve_inout<bool>("irr_forceOff_tray3");
    force_off[3]  = io.resolve_inout<bool>("irr_forceOff_tray4");
    force_off[4]  = io.resolve_inout<bool>("irr_forceOff_tray5");
    force_off[5]  = io.resolve_inout<bool>("irr_forceOff_tray6");
    force_off[6]  = io.resolve_inout<bool>("irr_forceOff_tray7");
    force_off[7]  = io.resolve_inout<bool>("irr_forceOff_tray8");
    force_now     = io.resolve_inout<bool>("irr_forceNow");
    today         = io.resolve_inout<uint32_t>("irr_today");
    served        = io.resolve_inout<uint8_t>("irr_servedToday");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = mod.r;
    _watch_rows[1] = outside_temp.r;
    _watch_rows[2] = phase.r;
    _watch_rows[3] = from_rise.r;
    _watch_rows[4] = today.r;
    _watch_rows[5] = served.r;
    set_inputs(_watch_rows, 6);
    set_health_row(io.resolve_out<uint8_t>("prog_irr_schedule"));
}

}  // namespace Site

#endif // SITE_PROGRAM_IRRIGATION_SCHEDULE_H
