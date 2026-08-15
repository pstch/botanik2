#ifndef PLC_BLOCKS_METER_H
#define PLC_BLOCKS_METER_H

/**
 * @file meter.h
 * @brief Integral (rate -> quantity) and Derivative (quantity -> rate).
 * @note IEC 61131-3: INTEGRAL, DERIVATIVE. Instrumentation calls the first a
 *       totalizer and the second a rate block.
 * @note Every rate here is PER SECOND: a meter reading L/min goes in as
 *       `flow / 60.0f`. Converted at the call site, where the units show.
 */

#include <cstdint>

#include "libbotanik/config/base.h"
#include "libbotanik/plc/blocks/block.h"
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    /**
     * @brief Integrates a rate into a running quantity: the totalizer (IEC
     *        INTEGRAL). Volume from flow, energy from power, hours from a run bit.
     * @note Accumulates in double. A float loses its low bits as it grows: near
     *       10000 L an increment of a few millilitres stops changing it at all.
     */
    class Integral : public Block {

        double _total = 0.0;
        float  _rate  = 0.0f;   ///< last rate counted, after the cutoff

    public:
        /** @brief Count @p dt_ms worth of @p rate_per_s into the total. Needs the
         *  REAL dt: a program that ran 400 ms late delivered 400 ms of water.
         *  @param cutoff below this the rate counts as zero, so a meter idling on
         *         noise does not total a plausible volume with the pump off.
         *  @return the new total. */
        float operator()(float rate_per_s, uint32_t dt_ms, float cutoff = 0.0f);

        float total() const { return (float)_total; }
        /** @brief Full precision, for a persisted copy. */
        double total64() const { return _total; }
        /** @brief The rate last counted; 0 while under the cutoff. */
        float rate() const { return _rate; }
        void preset(double total) { _total = total; }
        void reset() { _total = 0.0; _rate = 0.0f; }

    };

    /**
     * @brief Differentiates a quantity back into a rate (IEC DERIVATIVE): a
     *        pulse total, a cumulative register, a tank level. Signed, so a
     *        falling level reads negative.
     * @note Measured over a WINDOW, not scan to scan: a quantity advancing in
     *       steps differentiated every scan alternates between zero and a spike.
     */
    class Derivative : public Block {

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
        /** @brief Adopt @p quantity as the window start without producing a rate.
         *  Call after a counter reset or wrap, which otherwise shows up as one
         *  window of large negative rate. */
        void preset(float quantity);
        void reset();

    };

    /** @name Family checks
     *  @{ */
    BOTANIK_PLC_BLOCK(Integral);
    BOTANIK_PLC_BLOCK(Derivative);
    /** @} */

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_METER_H
