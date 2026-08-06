#ifndef PLC_BLOCKS_METER_H
#define PLC_BLOCKS_METER_H

/**
 * @file meter.h
 * @brief The two metering blocks: a rate integrated into a quantity, and a
 *        quantity differentiated back into a rate.
 *
 * IEC 61131-3 calls them INTEGRAL and DERIVATIVE; instrumentation calls the
 * first a totalizer and the second a rate block. Inverses, and a plant usually
 * has both.
 *
 * Every rate here is PER SECOND, because that is what dt is measured in: a meter
 * reading L/min goes in as `flow / 60.0f` and comes back out by `* 60.0f`.
 * Converted at the call site, where the units are visible, rather than a
 * time-base argument on every call.
 */

#include <cstdint>              // uint32_t

#include "libbotanik/config/base.h"       // the BOTANIK_FEATURE_* gates
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    /**
     * @brief Integrates a rate into a running quantity: the totalizer. Volume
     * from flow, energy from power, hours from a run bit.
     *
     * Accumulates `rate * dt`, so it must be handed the REAL dt - a program that
     * ran 400 ms late has genuinely delivered 400 ms of water.
     *
     * The total is a double, and that is not fussiness: a float loses its low
     * bits as it grows, and somewhere near 10000 L an increment of a few
     * millilitres stops changing it at all, so it silently stops advancing.
     * total() still hands back a float for the variable table.
     */
    class Integral {

        double _total = 0.0;
        float  _rate  = 0.0f;   ///< last rate counted, after the cutoff

    public:
        /** @brief Count @p dt_ms worth of @p rate_per_s into the total.
         *  @param cutoff the standard low-flow cut: below it the rate counts as
         *         zero, so a meter idling on noise does not total a plausible
         *         volume overnight with the pump off.
         *  @return the new total. */
        float operator()(float rate_per_s, uint32_t dt_ms, float cutoff = 0.0f);

        float total() const { return (float)_total; }
        /** @brief The total at full precision, for a persisted copy. */
        double total64() const { return _total; }
        /** @brief The rate last counted; 0 while under the cutoff. */
        float rate() const { return _rate; }
        /** @brief Load a starting total (a restored parameter, an operator's
         *  correction). */
        void preset(double total) { _total = total; }
        /** @brief Back to zero: the "reset the day's volume" button. */
        void reset() { _total = 0.0; _rate = 0.0f; }

    };

    /**
     * @brief Differentiates a quantity back into a rate: the rate block. What a
     * pulse total, a cumulative register or a tank level needs. Signed, so a
     * falling level reads as a negative rate.
     *
     * Measured over a WINDOW, not scan to scan: a quantity that advances in
     * steps (one pulse, one litre) differentiated every scan alternates between
     * zero and a spike. The last figure holds between updates, so the output is
     * a rate rather than a sawtooth.
     *
     * A counter reset or wrap shows up as one window of large negative rate;
     * preset() re-seeds without one.
     */
    class Derivative {

        float    _prev    = 0.0f;   ///< quantity at the start of the window
        float    _rate    = 0.0f;   ///< last completed window, per second
        uint32_t _elapsed = 0;      ///< time accumulated into this window
        bool     _seed    = false;

    public:
        /** @brief Fold @p quantity in.
         *  @param window_ms averaging window; 0 differentiates every scan.
         *  @return the current rate, per second. */
        float operator()(float quantity, uint32_t window_ms, uint32_t dt_ms);

        float value() const { return _rate; }
        /** @brief Adopt @p quantity as the window's start without producing a
         *  rate. What to call after a counter reset. */
        void preset(float quantity);
        void reset();

    };

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_METER_H
