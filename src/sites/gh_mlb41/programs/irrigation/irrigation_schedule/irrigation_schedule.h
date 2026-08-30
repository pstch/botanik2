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
    Vars::RefIn<bool>     phase_qlt;
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

public:
    IrrigationSchedule(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "irr_schedule", BOTANIK_SITE_CONTROL_MS, io) {}

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
    mod           = _bind_input<float>(io, "irr_mod");
    split         = _bind_input<float>(io, "irr_volFreqSplit");
    trays_per_day = _bind_input<uint16_t>(io, "irr_traysPerDay");
    days[0]       = _bind_input<uint8_t>(io, "irr_days_tray1");
    days[1]       = _bind_input<uint8_t>(io, "irr_days_tray2");
    days[2]       = _bind_input<uint8_t>(io, "irr_days_tray3");
    days[3]       = _bind_input<uint8_t>(io, "irr_days_tray4");
    days[4]       = _bind_input<uint8_t>(io, "irr_days_tray5");
    days[5]       = _bind_input<uint8_t>(io, "irr_days_tray6");
    days[6]       = _bind_input<uint8_t>(io, "irr_days_tray7");
    days[7]       = _bind_input<uint8_t>(io, "irr_days_tray8");
    outside_temp  = _bind_input<float>(io, "cli_temp_ext");
    dawn_gate     = _bind_input<float>(io, "irr_dawnGate_degC");
    phase         = _bind_input<uint8_t>(io, "cli_phase");
    phase_qlt     = _bind_input<bool>(io, "cli_phase_qlt");
    from_rise     = _bind_input<int16_t>(io, "sol_fromRise_min");
    day_rise      = _bind_input<int16_t>(io, "sol_dayRise_min");
    morning_win   = _bind_input<uint16_t>(io, "irr_morningWin_min");
    due_tray      = _bind_output<uint8_t>(io, "irr_dueTray");
    slot_open     = _bind_output<bool>(io, "irr_slotOpen");
    period        = _bind_output<float>(io, "irr_period_days");
    last[0]       = _bind_inout<uint32_t>(io, "irr_last_tray1");
    last[1]       = _bind_inout<uint32_t>(io, "irr_last_tray2");
    last[2]       = _bind_inout<uint32_t>(io, "irr_last_tray3");
    last[3]       = _bind_inout<uint32_t>(io, "irr_last_tray4");
    last[4]       = _bind_inout<uint32_t>(io, "irr_last_tray5");
    last[5]       = _bind_inout<uint32_t>(io, "irr_last_tray6");
    last[6]       = _bind_inout<uint32_t>(io, "irr_last_tray7");
    last[7]       = _bind_inout<uint32_t>(io, "irr_last_tray8");
    force_on[0]   = _bind_inout<bool>(io, "irr_forceOn_tray1");
    force_on[1]   = _bind_inout<bool>(io, "irr_forceOn_tray2");
    force_on[2]   = _bind_inout<bool>(io, "irr_forceOn_tray3");
    force_on[3]   = _bind_inout<bool>(io, "irr_forceOn_tray4");
    force_on[4]   = _bind_inout<bool>(io, "irr_forceOn_tray5");
    force_on[5]   = _bind_inout<bool>(io, "irr_forceOn_tray6");
    force_on[6]   = _bind_inout<bool>(io, "irr_forceOn_tray7");
    force_on[7]   = _bind_inout<bool>(io, "irr_forceOn_tray8");
    force_off[0]  = _bind_inout<bool>(io, "irr_forceOff_tray1");
    force_off[1]  = _bind_inout<bool>(io, "irr_forceOff_tray2");
    force_off[2]  = _bind_inout<bool>(io, "irr_forceOff_tray3");
    force_off[3]  = _bind_inout<bool>(io, "irr_forceOff_tray4");
    force_off[4]  = _bind_inout<bool>(io, "irr_forceOff_tray5");
    force_off[5]  = _bind_inout<bool>(io, "irr_forceOff_tray6");
    force_off[6]  = _bind_inout<bool>(io, "irr_forceOff_tray7");
    force_off[7]  = _bind_inout<bool>(io, "irr_forceOff_tray8");
    force_now     = _bind_inout<bool>(io, "irr_forceNow");
    today         = _bind_inout<uint32_t>(io, "irr_today");
    served        = _bind_inout<uint8_t>(io, "irr_servedToday");

}

}  // namespace Site

#endif // SITE_PROGRAM_IRRIGATION_SCHEDULE_H
