/**
 * @file irrigation_cycle.h
 * @brief The declaration half of the `irrigation_cycle` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_IRRIGATION_CYCLE_H
#define SITE_PROGRAM_IRRIGATION_CYCLE_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief Irrigation: one tray at a time, filled to a dose, steeped, and drained.
 */
class IrrigationCycle final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<uint8_t>  due_tray;
    Vars::RefIn<float>    mod;
    Vars::RefIn<float>    in_total;
    Vars::RefIn<float>    out_total;
    Vars::RefIn<uint16_t> litres[8];
    Vars::RefIn<float>    split;
    Vars::RefIn<uint16_t> max_fill_s;
    Vars::RefIn<uint16_t> soak_s;
    Vars::RefIn<uint16_t> drain_window_s;
    Vars::RefIn<uint16_t> flush_s;
    Vars::RefIn<float>    exp_drain;
    Vars::RefIn<float>    drain_margin;
    Vars::RefIn<float>    out_flow;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float>   volume;
    Vars::RefOut<uint8_t> state;
    Vars::RefOut<uint8_t> active;
    Vars::RefOut<bool>    fill_fault;
    Vars::RefOut<bool>    drain_fault;
    Vars::RefOut<bool>    fill_tray[8];
    Vars::RefOut<bool>    drain_tray[8];

    /** @} */

    /** @name VAR_IN_OUT
     *  @{ */

    Vars::RefInOut<uint8_t>  served;
    Vars::RefInOut<uint32_t> last[8];
    Vars::RefInOut<bool>     force_on[8];
    Vars::RefInOut<bool>     force_now;
    Vars::RefInOut<uint32_t> today;
    Vars::RefInOut<float>    delivered;
    Vars::RefInOut<float>    drained;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    Plc::Ton dwell;  ///< how long this state has run; _enter() resets it

    /** @} */

    /** @name VAR - what this law remembers
     *  @{ */

    IrrState _state = IRR_IDLE;
    uint8_t  _tray = 0;        ///< 1..N_TRAY_VALVE while serving, else 0
    /** @brief The two meters' totals when the window under judgement
     *  opened. Litres are the DIFFERENCE, so a counter that has run since
     *  boot - or since last year, retained - needs no reset. */
    float    _fill_start_L = 0.0f;
    float    _drain_start_L = 0.0f;
    /** @brief What the tray actually got, kept because the modulation may
     *  move under a cycle that is already draining. */
    float    _dose_L = 0.0f;
    bool     _forced = false;  ///< this cycle came from irr_forceNow

    /** @} */

    /** @name Sizes - what a body loops over
     *  @{ */

    static const uint8_t N_LITRES     = 8;
    static const uint8_t N_FILL_TRAY  = 8;
    static const uint8_t N_DRAIN_TRAY = 8;
    static const uint8_t N_LAST       = 8;
    static const uint8_t N_FORCE_ON   = 8;

    /** @} */

public:
    IrrigationCycle(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "irrigation", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;

private:

    /** @brief Judge the window that has just closed: did enough come back?
     *  A dose of zero or an unusable meter gives no verdict at all - a clog
     *  is a measurement, and there is nothing to measure. */
    void     _drain_verdict();
    /** @brief Drive the valves for @p state, serving @p tray (0 = none). */
    void     _set_valves(IrrState state, uint8_t tray);
    /** @brief Every valve to the position its row declares it fails to. */
    void     _park_valves();
    /** @brief Set or clear @p per_tray for the tray being served. */
    void     _blame(const Vars::RefOut<bool>* per_tray, bool on);
    /** @brief Record that the active tray has been watered, durably, and
     *  spend the force that asked for it. */
    void     _stamp_served();
    /** @brief Stamp tray @p tray (0-based) as watered on UNIX day @p day. */
    void     _stamp(uint8_t tray, uint32_t day) const;
    /** @brief What follows a fill: the soak, or the drain when there is
     *  none. */
    IrrState _after_fill() const;
    void     _enter(IrrState state);
};

inline void IrrigationCycle::bind(Vars::Bank& io) {
    due_tray       = _bind_input<uint8_t>(io, "irr_dueTray");
    mod            = _bind_input<float>(io, "irr_mod");
    in_total       = _bind_input<float>(io, "irr_inTotal_L");
    out_total      = _bind_input<float>(io, "irr_outTotal_L");
    litres[0]      = _bind_input<uint16_t>(io, "irr_liters_tray1");
    litres[1]      = _bind_input<uint16_t>(io, "irr_liters_tray2");
    litres[2]      = _bind_input<uint16_t>(io, "irr_liters_tray3");
    litres[3]      = _bind_input<uint16_t>(io, "irr_liters_tray4");
    litres[4]      = _bind_input<uint16_t>(io, "irr_liters_tray5");
    litres[5]      = _bind_input<uint16_t>(io, "irr_liters_tray6");
    litres[6]      = _bind_input<uint16_t>(io, "irr_liters_tray7");
    litres[7]      = _bind_input<uint16_t>(io, "irr_liters_tray8");
    split          = _bind_input<float>(io, "irr_volFreqSplit");
    max_fill_s     = _bind_input<uint16_t>(io, "irr_maxFill_sec");
    soak_s         = _bind_input<uint16_t>(io, "irr_soak_sec");
    drain_window_s = _bind_input<uint16_t>(io, "irr_drainWindow_sec");
    flush_s        = _bind_input<uint16_t>(io, "irr_flush_sec");
    exp_drain      = _bind_input<float>(io, "irr_expDrainFrac");
    drain_margin   = _bind_input<float>(io, "irr_drainMargin");
    out_flow       = _bind_input<float>(io, "irr_outFlow_Lpm");
    volume         = _bind_output<float>(io, "irr_volume_L");
    state          = _bind_output<uint8_t>(io, "irr_state");
    active         = _bind_output<uint8_t>(io, "irr_activeTray");
    fill_fault     = _bind_output<bool>(io, "alarm_irr_fill");
    drain_fault    = _bind_output<bool>(io, "alarm_irr_drain");
    fill_tray[0]   = _bind_output<bool>(io, "alarm_irr_fill_tray1");
    fill_tray[1]   = _bind_output<bool>(io, "alarm_irr_fill_tray2");
    fill_tray[2]   = _bind_output<bool>(io, "alarm_irr_fill_tray3");
    fill_tray[3]   = _bind_output<bool>(io, "alarm_irr_fill_tray4");
    fill_tray[4]   = _bind_output<bool>(io, "alarm_irr_fill_tray5");
    fill_tray[5]   = _bind_output<bool>(io, "alarm_irr_fill_tray6");
    fill_tray[6]   = _bind_output<bool>(io, "alarm_irr_fill_tray7");
    fill_tray[7]   = _bind_output<bool>(io, "alarm_irr_fill_tray8");
    drain_tray[0]  = _bind_output<bool>(io, "alarm_irr_drain_tray1");
    drain_tray[1]  = _bind_output<bool>(io, "alarm_irr_drain_tray2");
    drain_tray[2]  = _bind_output<bool>(io, "alarm_irr_drain_tray3");
    drain_tray[3]  = _bind_output<bool>(io, "alarm_irr_drain_tray4");
    drain_tray[4]  = _bind_output<bool>(io, "alarm_irr_drain_tray5");
    drain_tray[5]  = _bind_output<bool>(io, "alarm_irr_drain_tray6");
    drain_tray[6]  = _bind_output<bool>(io, "alarm_irr_drain_tray7");
    drain_tray[7]  = _bind_output<bool>(io, "alarm_irr_drain_tray8");
    served         = _bind_inout<uint8_t>(io, "irr_servedToday");
    last[0]        = _bind_inout<uint32_t>(io, "irr_last_tray1");
    last[1]        = _bind_inout<uint32_t>(io, "irr_last_tray2");
    last[2]        = _bind_inout<uint32_t>(io, "irr_last_tray3");
    last[3]        = _bind_inout<uint32_t>(io, "irr_last_tray4");
    last[4]        = _bind_inout<uint32_t>(io, "irr_last_tray5");
    last[5]        = _bind_inout<uint32_t>(io, "irr_last_tray6");
    last[6]        = _bind_inout<uint32_t>(io, "irr_last_tray7");
    last[7]        = _bind_inout<uint32_t>(io, "irr_last_tray8");
    force_on[0]    = _bind_inout<bool>(io, "irr_forceOn_tray1");
    force_on[1]    = _bind_inout<bool>(io, "irr_forceOn_tray2");
    force_on[2]    = _bind_inout<bool>(io, "irr_forceOn_tray3");
    force_on[3]    = _bind_inout<bool>(io, "irr_forceOn_tray4");
    force_on[4]    = _bind_inout<bool>(io, "irr_forceOn_tray5");
    force_on[5]    = _bind_inout<bool>(io, "irr_forceOn_tray6");
    force_on[6]    = _bind_inout<bool>(io, "irr_forceOn_tray7");
    force_on[7]    = _bind_inout<bool>(io, "irr_forceOn_tray8");
    force_now      = _bind_inout<bool>(io, "irr_forceNow");
    today          = _bind_inout<uint32_t>(io, "irr_today");
    delivered      = _bind_inout<float>(io, "irr_delivered_L");
    drained        = _bind_inout<float>(io, "irr_drained_L");

}

}  // namespace Site

#endif // SITE_PROGRAM_IRRIGATION_CYCLE_H
