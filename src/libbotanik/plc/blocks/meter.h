#ifndef PLC_BLOCKS_METER_H
#define PLC_BLOCKS_METER_H

/**
 * @file meter.h
 * @brief The two metering blocks: a rate integrated into a quantity, and a
 *        quantity differentiated back into a rate.
 *
 * IEC 61131-3 calls them INTEGRAL and DERIVATIVE; instrumentation calls the
 * first a totalizer and the second a rate block. They are inverses, and a plant
 * usually has both: a flow meter reading L/min that has to become "litres given
 * today" (Integral), and a tank level or a pulse count that has to become
 * "litres per minute right now" (Derivative).
 *
 * Every rate here is PER SECOND, because that is what dt is measured in. A
 * meter reading L/min goes in as `flow / 60.0f` and a per-second rate comes out
 * as L/min again by `* 60.0f`. One convention, converted at the call site where
 * the units are visible, rather than a time-base argument on every call.
 */

#include <cstdint>

#include "../../../libbotanik_config.h"
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    /**
     * @brief Integrates a rate into a running quantity: the totalizer.
     *
     * Volume from flow, energy from power, hours from a run bit. Accumulates
     * `rate * dt` every scan, which is why it must be handed the REAL dt: a
     * task that ran 400 ms late has genuinely delivered 400 ms of water.
     *
     * Accumulated in double even though everything else here is float, and that
     * is not fussiness: a float total loses its low bits as it grows, and at
     * around 10000 L an increment of a few millilitres per scan stops changing
     * the value at all, so the total silently stops advancing. total() still
     * hands back a float for the variable table; total64() is there for
     * whatever wants the full figure.
     *
     * The @p cutoff is the standard low-flow cut: a meter that idles at a few
     * counts of noise would otherwise total a plausible-looking volume
     * overnight with the pump off.
     */
    class Integral {

        /** @name State
         *  @{ */

        double _total = 0.0;
        float  _rate  = 0.0f;   ///< last rate counted, after the cutoff

        /** @} */

    public:
        /** @name Operation
         *  @{ */

        /** @brief Count @p dt_ms worth of @p rate_per_s into the total.
         *  @param cutoff magnitude below which the rate counts as zero.
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

        /** @} */
    };

    /**
     * @brief Differentiates a quantity back into a rate: the rate block.
     *
     * The inverse of Integral, and the block a counter needs - a pulse total,
     * a meter's cumulative register, a tank level. Signed, so a falling level
     * reads as a negative rate rather than nothing.
     *
     * Measured over a WINDOW rather than scan to scan. A quantity that advances
     * in steps (one pulse, one litre) differentiated every scan gives a rate
     * that alternates between zero and a spike; averaging over a window long
     * enough to hold several steps is what an instrument actually does. The
     * last figure holds between updates, so the output is a rate, not a
     * sawtooth.
     *
     * A counter that is reset or wraps shows up as a large negative rate for
     * one window. preset() re-seeds without one.
     */
    class Derivative {

        /** @name State
         *  @{ */

        float    _prev    = 0.0f;   ///< quantity at the start of the window
        float    _rate    = 0.0f;   ///< last completed window, per second
        uint32_t _elapsed = 0;      ///< time accumulated into this window
        bool     _seed    = false;

        /** @} */

    public:
        /** @name Operation
         *  @{ */

        /** @brief Fold @p quantity in.
         *  @param window_ms averaging window; 0 differentiates every scan.
         *  @return the current rate, per second. */
        float operator()(float quantity, uint32_t window_ms, uint32_t dt_ms);

        float value() const { return _rate; }
        /** @brief Adopt @p quantity as the window's start without producing a
         *  rate. What to call after a counter reset. */
        void preset(float quantity);
        void reset();

        /** @} */
    };

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_METER_H
