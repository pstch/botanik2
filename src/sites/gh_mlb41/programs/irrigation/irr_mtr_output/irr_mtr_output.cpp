/**
 * @file irr_mtr_output.cpp
 * @brief HAND-WRITTEN - the body of the `irr_mtr_output` POU.
 */

#include "irr_mtr_output.h"

namespace Site {

void IrrMtrOutput::run(ProgramContext& c) { (void)c; }

void IrrMtrOutput::observe(ProgramContext& c) {
    if (c.first_scan) {
        flow_avg.reset();
        flow_band.reset();
        // The retained row carries what was metered before this run.
        meter.preset(out_total.get());
    }

    const float lpm = flow_avg(out_flow.get(),
                               0.001f * (float)out_avg_ms.get(), c.dt_ms);
    if (!out_flow.valid()) {
        // A flow derived from a loop that has stopped answering is not a flow.
        // The TOTAL beside it keeps its value: a totalizer is a record of what
        // went past, and the instrument failing does not unmake it. Zero would
        // read as "no water is moving", which is what a leak detector must not
        // be told.
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
        out_flow_pub.set(0.0f, true);
    } else {
        out_flow_pub.set(flow_band(lpm, band.get()), true);
    }
    // Integrated from `lpm`, which is the average of a reading that may have
    // stopped arriving - so while the loop is out this is a total that keeps
    // growing on the last flow anybody saw.
    out_total.set(meter(lpm / 60.0f, c.dt_ms, cutoff.get() / 60.0f),
                  out_flow.valid());
}

}  // namespace Site
