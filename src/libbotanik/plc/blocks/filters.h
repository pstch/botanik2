#ifndef PLC_BLOCKS_FILTERS_H
#define PLC_BLOCKS_FILTERS_H

/**
 * @file filters.h
 * @brief Smoothing a process value: exponential average, median-of-3, boxcar.
 *
 * Distinct from utils/filters.h, which is the scheduler's own metric
 * instrumentation: those are fed once per beat with a precomputed alpha and are
 * asymmetric on purpose (fast attack, slow decay). These take a time constant
 * in seconds and the scan's dt, so a task's filter keeps the same wall-clock
 * response whatever rate it runs at, and stays correct when a scan comes late.
 */

#include <cstdint>                    // uint8_t/uint32_t - the window index and the sample ages

#include "libbotanik_config.h"        // BOTANIK_FEATURE_PLC_BLK_FILTERS - the gate this whole header sits behind
#if BOTANIK_FEATURE_PLC_BLK_FILTERS

namespace Botanik { namespace Plc {

    /**
     * @brief Exponentially weighted moving average with a time constant.
     *
     * The first-order lag every noisy reading wants before a threshold or a
     * controller sees it. @p tau_s is the 63% response time; the per-scan blend
     * is derived from it and the actual dt, so a 30 s filter is a 30 s filter
     * whether the task runs at 1 Hz or 10 Hz.
     *
     * The first sample is adopted whole rather than blended up from zero: a
     * filter that starts at 0 and creeps to 21 C spends its warm-up reporting a
     * frost that is not there.
     */
    class Ewma {

        /** @name State
         *  @{ */

        float _value = 0.0f;
        bool  _seed  = false;

        /** @} */

    public:
        /** @name Operation
         *  @{ */

        /** @param tau_s time constant in seconds; 0 or less passes through. */
        float operator()(float x, float tau_s, uint32_t dt_ms);
        float value() const { return _value; }
        /** @brief Adopt @p v as the current output (mode change, restart). */
        void preset(float v) { _value = v; _seed = true; }
        void reset() { _value = 0.0f; _seed = false; }

        /** @} */
    };

    /**
     * @brief Median of the last three samples.
     *
     * Kills a single-sample spike outright, which an average only dilutes - the
     * right filter for a reading that is clean except for the occasional
     * garbage conversion. Costs two samples of lag and nothing else; often used
     * ahead of an Ewma rather than instead of one.
     */
    class MedianOf3 {

        /** @name State
         *  @{ */

        float   _s[3] = { 0.0f, 0.0f, 0.0f };   ///< newest last
        uint8_t _n    = 0;                      ///< samples seen, saturating at 3

        /** @} */

    public:
        /** @name Operation
         *  @{ */

        float operator()(float x);
        float value() const;
        void reset() { _n = 0; }

        /** @} */
    };

    /**
     * @brief Boxcar average of the last @p N samples.
     *
     * Flat response and finite memory: unlike an Ewma it forgets a bad sample
     * completely after N of them. A template because N sizes the storage, so it
     * is the one block here that has to live in the header.
     *
     * Sample-counted, not time-counted - the window is N scans, which is N
     * periods only as long as the task keeps its rate. Prefer Ewma when the
     * wall-clock response is what matters.
     */
    template <uint8_t N>
    class MovingAverage {
        static_assert(N >= 1, "MovingAverage needs at least one sample");

        /** @name State
         *  @{ */

        float   _buf[N] = {};
        uint8_t _head   = 0;    ///< where the next sample lands
        uint8_t _fill   = 0;    ///< samples held, saturating at N

        /** @} */

    public:
        /** @name Operation
         *  @{ */

        float operator()(float x) {
            _buf[_head] = x;
            _head = (uint8_t)((_head + 1) % N);
            if (_fill < N) ++_fill;
            return value();
        }

        /** @brief Mean of what is held; the window average once it is full. */
        float value() const {
            if (_fill == 0) return 0.0f;
            float sum = 0.0f;
            for (uint8_t i = 0; i < _fill; ++i) sum += _buf[i];
            return sum / (float)_fill;
        }

        /** @brief Whether N samples have been seen. */
        bool full() const { return _fill >= N; }

        void reset() { _head = 0; _fill = 0; }

        /** @} */
    };

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_FILTERS

#endif // PLC_BLOCKS_FILTERS_H
