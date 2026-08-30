/**
 * @file irr_mtr_input.cpp
 * @brief HAND-WRITTEN - the body of the `irr_mtr_input` POU.
 */

#include "irr_mtr_input.h"

namespace Site {

void IrrMtrInput::run(ProgramContext& c) {
    const uint8_t  ch = Site::pulse_inputs[0].channel;
    const uint32_t pulses = HAL::Pulse::sample(ch).count;
    if (c.first_scan) {
        rate.reset();
        _last_pulses = pulses;
        // Adopt the counter where it stands rather than starting from zero:
        // the retained row carries what was metered before this run, and the
        // pulse counter carries what has been metered since the last boot.
        _base_L      = in_total.get();
        _base_pulses = pulses;
    }

    // Unsigned difference, so the 32-bit counter wrapping costs nothing.
    const float total = _base_L
                      + (float)(pulses - _base_pulses) * qty_per_pulse.get();
    // Whether the line is ticking, which is the only thing a pulse terminal
    // can say about itself: its LEVEL is a moment of a train. Written every
    // beat, so the row reads as a live terminal rather than as one nothing has
    // ever published.
    pulsing.set(pulses != _last_pulses, true);
    _last_pulses = pulses;

    in_total.set(total, true);
    const float flow = 60.0f * rate(total, rate_win.get(), c.dt_ms);
    in_flow.set(flow, true);

    // The two alarms below are gated on the cycle's state: without it they
    // would be judged against IDLE, which is what a leak is measured from.
    const IrrState st = (IrrState)state.get();
    // TWO windows, not one. The rate is the last COMPLETED window and stands
    // until the next one closes, and the block's window boundaries have nothing
    // to do with when the state changed: a window straddling the change closes
    // with the old state's water still in it, and that figure then stands for a
    // window more. Until two have passed, the rate can still be reporting the
    // state before - which is how the flush's 60 L/min read as a leak the
    // moment the cycle went idle, and idle's zero read as a pressure fault the
    // moment a fill opened. Both fired on every cycle the plant ran.
    const uint32_t win = 2u * rate_win.get();
    const bool idle = idle_for(st == IRR_IDLE, win, c.dt_ms);
    const bool fill = fill_for(st == IRR_FILLING, win, c.dt_ms);

    // Idle means every valve in the circuit is shut, so anything the inlet
    // meter counts is going somewhere nobody opened.
    leak.set(idle && flow > leak_Lpm.get(), true);

    // The mirror image: the inlet is open and the water is not arriving.
    pressure.set(fill && flow < min_fill_Lpm.get(), true);
}


}  // namespace Site
