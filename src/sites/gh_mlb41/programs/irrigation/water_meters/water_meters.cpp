/**
 * @file water_meters.cpp
 * @brief HAND-WRITTEN - the body of the `irr_meters` POU.
 */

#include "water_meters.h"

#include "../../../vars.h"

namespace Site {

void WaterMeters::observe(ProgramContext& c) {
    const uint8_t  ch = Site::pulse_inputs[0].channel;
    const uint32_t pulses = HAL::Pulse::sample(ch).count;
    if (c.first_scan) {
        flow_avg.reset(); flow_band.reset(); in_rate.reset();
        // Adopt both counters where they stand rather than starting from zero:
        // the retained rows carry what was metered before this run, and the
        // pulse counter carries what has been metered since the last boot.
        _in_base_L      = in_total.get();
        _in_base_pulses = pulses;
        out_meter.preset(out_total.get());
    }

    const float lpm = flow_avg(out_flow.get(),
                               0.001f * (float)out_avg_ms.get(), c.dt_ms);
    if (!out_flow.valid()) {
        // A flow derived from a loop that has stopped answering is not a flow.
        // The TOTAL beside it keeps its value: a totalizer is a record of what
        // went past, and the instrument failing does not unmake it.
        // The last flow the loop reported, held: a totalizer beside it that
        // stopped counting says the same thing, and zero would read as "no
        // water is moving", which is what a leak detector must not be told.
        out_flow_pub.hold();
        degraded();
    } else if (lpm < band.get()) {
        // WITHIN THE DEADBAND OF ZERO, so it reads zero: a reading the band
        // cannot tell from nothing is nothing. Left to the block the row parks
        // on the last tenth it reported - a drain meter saying 0.09 Lpm with
        // every valve shut, which is what a leak looks like. Reset with it, so
        // the next real flow is written at once rather than waiting out a band
        // from a figure the plant has left behind.
        flow_band.reset();
        out_flow_pub.set(0.0f);
    } else {
        out_flow_pub.set(flow_band(lpm, band.get()));
    }
    // Per SECOND into the totalizer, which is what the block counts in.
    out_total.set(out_meter(lpm / 60.0f, c.dt_ms, cutoff.get() / 60.0f));

    // Unsigned difference, so the 32-bit counter wrapping costs nothing.
    const float total = _in_base_L
                      + (float)(pulses - _in_base_pulses) * qty_per_pulse.get();
    in_total.set(total);
    const float flow = 60.0f * in_rate(total, rate_win.get(), c.dt_ms);
    in_flow.set(flow);
    _supply_alarms(flow);
}

void WaterMeters::_supply_alarms(float in_flow_Lpm) {
    const IrrState st = (IrrState)state.get();

    // Idle means every valve in the circuit is shut, so anything the inlet
    // meter counts is going somewhere nobody opened.
    leak.set(st == IRR_IDLE && in_flow_Lpm > leak_Lpm.get());

    // The mirror image: the inlet is open and the water is not arriving. The
    // row's own dwell covers the start of a fill, where the rate window has not
    // filled yet and every fill would otherwise open with a pressure fault.
    pressure.set(st == IRR_FILLING && in_flow_Lpm < min_fill_Lpm.get());
}

}  // namespace Site
