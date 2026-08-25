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

    // Only a MEASURED sample is folded in: the row keeps its last reading while
    // the loop is out, and averaging that is the same number over and over.
    // The filter KEEPS its state, so a loop that comes back rejoins where it
    // left rather than climbing out of a hole.
    const bool measured = out_flow.valid();
    const float lpm = measured ? flow_avg(out_flow.get(),
                                          0.001f * (float)out_avg_ms.get(),
                                          c.dt_ms)
                               : flow_avg.value();
    if (!measured) {
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
    // A totalizer is a record of what went past, so nothing is counted from a
    // loop that is not answering: the last flow it reported, integrated, is
    // invented water - and it is invented into the figure the drain verdict
    // and the leak alarm are made against. The total STANDS, and says it is
    // not current.
    out_total.set(measured ? meter(lpm / 60.0f, c.dt_ms, cutoff.get() / 60.0f)
                           : meter.total(),
                  measured);
}

}  // namespace Site
