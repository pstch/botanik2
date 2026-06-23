#ifndef UTILS_FILTERS_H
#define UTILS_FILTERS_H

/**
 * @file filters.h
 * @brief Dependency-free metric filter primitives.
 *
 * The asymmetric EMA, the sliding-window maximum, and the EMA alpha helper.
 * LoopScheduler builds its per-beat instrumentation on these (CPU load, jitter,
 * work duration, stack).
 */

#include <cstdint>

#include "libbotanik_config.h"

namespace Botanik { namespace Utils {

    /** @name Helpers
     *  @{ */

    /** @brief EMA smoothing factor for a time constant @p tau_ms at sample
     *  period @p period_ms.
     *
     *  Derived from the sample period so the filter's wall-clock response stays
     *  fixed regardless of how often it is fed (forward-Euler: alpha = dt/tau).
     *
     *  @warning A tau at or below one sample period collapses to 1.0 (instant,
     *  since the filter can't resolve anything faster than a single sample).
     *
     *  @note Defaults to the tick period for per-tick metrics. */
    constexpr float ema_alpha(uint32_t tau_ms,
                              uint32_t period_ms = Config::TASK_PERIOD_MS) {
        return tau_ms <= period_ms
            ? 1.0f
            : (float)period_ms / (float)tau_ms;
    }

    /** @} */

    /** @name Metric filters
     *  @{ */

    /** @brief Exponential moving average with asymmetric attack/decay: a sample
     *  above the current value is folded in with @c attack, one below it with
     *  @c decay, so the filter can rise fast and fall slow (or vice versa). */
    class ExpMovingAverage {
        float _value  = 0.0f;   // current filtered value
        float _attack = 1.0f;   // blend factor for samples above the value
        float _decay  = 1.0f;   // blend factor for samples below the value
    public:
        /** @brief Set the attack and decay blend factors (see ema_alpha()). */
        void  configure(float attack, float decay) {
            _attack = attack;
            _decay = decay;
        }
        /** @brief Fold one sample in (attack if it rises, decay if it falls).
         */
        void  push(float x) {
            const float a = (x > _value) ? _attack : _decay;
            _value = a * x + (1.0f - a) * _value;
        }
        float value() const { return _value; }
        void  reset()       { _value = 0.0f; }
    };

    /** @brief Sliding-window maximum over roughly the last @p window_ticks
     *  samples, approximated with K rotating time buckets. @note O(1) per
     *  sample and O(K) to read, using K words of state - instead of a full
     *  per-sample ring buffer rescanned O(window) every tick. Each bucket holds
     *  the max of one time slice; the newest is updated in place and the oldest
     *  falls off on rotation, so the effective window wobbles by up to one
     *  bucket width. @warning Assumes non-negative samples with 0 as the
     *  identity (true for the microsecond / byte metrics here). */
    template <uint32_t K>
    class WindowedMax {
        static_assert(K >= 2, "WindowedMax needs at least two buckets");
        uint32_t _bucket[K]   = {};  // K-sized bucket array
        uint32_t _head        = 0;   // index of the newest (filling) bucket
        uint32_t _fill        = 0;   // samples folded into the newest bucket
        uint32_t _bucket_span = 1;   // samples per bucket = window / K (>= 1)
    public:

        /** @brief Set the window length in ticks; clears all buckets. */
        void configure(uint32_t window_ticks) {
            _bucket_span = (window_ticks >= K) ? (window_ticks / K) : 1;
            reset();
        }

        /** @brief Fold one sample into the window. @p weight is how many sample
         *  slots it represents (1 normally; the count of elapsed beats on a
         *  scheduler overrun, so the window ages by wall-time rather than by
         *  released-tick count - otherwise a stale peak lingers past the window
         *  while ticks are being skipped). */
        void push(uint32_t v, uint32_t weight = 1) {
            _fill += weight;
            // rotate per elapsed slice
            for (uint32_t r = 0; _fill > _bucket_span && r < K; ++r) {
                _fill -= _bucket_span;
                _head = (_head + 1) % K;
                _bucket[_head] = 0;
            }
            // weight spanned the whole window
            if (_fill > _bucket_span) _fill = _bucket_span;
            if (v > _bucket[_head]) _bucket[_head] = v;
        }

        /** @brief Current windowed maximum. */
        uint32_t value() const {
            uint32_t m = 0;
            for (uint32_t i = 0; i < K; ++i) if (_bucket[i] > m) m = _bucket[i];
            return m;
        }

        void reset() {
            for (uint32_t i = 0; i < K; ++i) _bucket[i] = 0;
            _head = 0; _fill = 0;
        }
    };

    /** @brief Windowed-max sized to the load-metric bucket count. */
    typedef WindowedMax<Config::LOAD_METRIC_BUCKETS> MetricWindow;

    /** @} */

}}

#endif // UTILS_FILTERS_H
