#ifndef PLC_BLOCKS_FILTERS_H
#define PLC_BLOCKS_FILTERS_H

/**
 * @file filters.h
 * @brief Smoothing a process value: exponential average, median-of-3, boxcar.
 *
 * Not utils/filters.h, which is the scheduler's metric instrumentation - fed
 * once per beat with a precomputed alpha, asymmetric on purpose. These take a
 * time constant in seconds and the scan's dt, so the wall-clock response holds
 * whatever rate the program runs at, and stays right when a scan comes late.
 */

#include <cstdint>              // uint8_t/uint32_t

#include "libbotanik_config.h"  // BOTANIK_FEATURE_PLC_BLK_FILTERS
#if BOTANIK_FEATURE_PLC_BLK_FILTERS

namespace Botanik { namespace Plc {

    /**
     * @brief Exponentially weighted moving average: the first-order lag a noisy
     * reading wants before a threshold or a controller sees it.
     *
     * @p tau_s is the 63% response time, and the per-scan blend derives from it
     * and the actual dt - so a 30 s filter is a 30 s filter at 1 Hz or 10 Hz.
     * The first sample is adopted whole rather than blended up from zero: a
     * filter creeping from 0 to 21 C spends its warm-up reporting a frost that
     * is not there.
     */
    class Ewma {

        float _value = 0.0f;
        bool  _seed  = false;

    public:
        /** @param tau_s time constant in seconds; 0 or less passes through. */
        float operator()(float x, float tau_s, uint32_t dt_ms);
        float value() const { return _value; }
        /** @brief Adopt @p v as the current output (mode change, restart). */
        void preset(float v) { _value = v; _seed = true; }
        void reset() { _value = 0.0f; _seed = false; }

    };

    /**
     * @brief Median of the last three samples: kills a single-sample spike
     * outright, where an average only dilutes it.
     *
     * For a reading that is clean except for the occasional garbage conversion.
     * Two samples of lag; often used ahead of an Ewma rather than instead of it.
     */
    class MedianOf3 {

        float   _s[3] = { 0.0f, 0.0f, 0.0f };   ///< newest last
        uint8_t _n    = 0;                      ///< samples seen, saturating at 3

    public:
        float operator()(float x);
        float value() const;
        void reset() { _n = 0; }

    };

    /**
     * @brief Boxcar average of the last @p N samples: flat response, and unlike
     * an Ewma it forgets a bad sample completely after N of them.
     *
     * Sample-counted, not time-counted - the window is N scans, which is N
     * periods only while the program keeps its rate. Prefer Ewma when the
     * wall-clock response is what matters. A template because N sizes the
     * storage, so this is the one block that lives entirely in the header.
     */
    template <uint8_t N>
    class MovingAverage {
        static_assert(N >= 1, "MovingAverage needs at least one sample");

        float   _buf[N] = {};
        uint8_t _head   = 0;    ///< where the next sample lands
        uint8_t _fill   = 0;    ///< samples held, saturating at N

    public:
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

    };

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_FILTERS

#endif // PLC_BLOCKS_FILTERS_H
