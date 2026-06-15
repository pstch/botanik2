// REVIEW_34: OK
#ifndef UTILS_PERF_H
#define UTILS_PERF_H

/**
 * @file perf.h
 * @brief PerfMonitor — main-loop performance instrumentation: per-iteration work
 *        timing (CPU load), stack painting + high-water scan (stack usage), and
 *        the metric filters layered on top (EMA smoothing, cumulative running max,
 *        and a sliding-window "recent peak"). Owned by LoopScheduler, which feeds
 *        it the work window and publishes its snapshot in the SchedulerContext.
 */

#include <cstdint>

#include "libbotanik_config.h"

// Because the address sanitizer uses a fake stack for locals, stack-usage
// measurement is fundamentally incompatible with ASAN, we define the
// BOTANIK_NO_ASAN macro to annotate the stack measurement code.
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define BOTANIK_NO_ASAN __attribute__((no_sanitize("address")))
#  endif
#elif defined(__SANITIZE_ADDRESS__)
#  define BOTANIK_NO_ASAN __attribute__((no_sanitize("address")))
#endif
#ifndef BOTANIK_NO_ASAN
#  define BOTANIK_NO_ASAN
#endif

namespace Botanik { namespace Utils {

    /** @name Tuning constants & helpers
     *  @{ */

    /** @brief EMA smoothing factor for a response time constant @p tau_ms, given
     *  the filter's sample period @p period_ms. Derived from the sample period so
     *  the filter's wall-clock response stays fixed regardless of how often it is
     *  fed (forward-Euler: alpha = dt/tau). 
     * 
     *  @warning A tau at or below one sample period collapses to 1.0 (instant, since the 
     *  filter can't resolve anything faster than a single sample). 
     * 
     *  @note Defaults to the tick period for per-tick metrics. */
    constexpr float ema_alpha(uint32_t tau_ms, uint32_t period_ms = Config::TASK_PERIOD_MS) {
        return tau_ms <= period_ms
            ? 1.0f
            : (float)period_ms / (float)tau_ms;
    }

    /** @brief Number of per-tick samples spanning LOAD_METRIC_WINDOW_MS. */
    static constexpr uint32_t LOAD_METRIC_WINDOW =
        Config::LOAD_METRIC_WINDOW_MS / Config::TASK_PERIOD_MS;

    /** @brief We sample the stack high-water mark (and re-paint) once per slice 
     *  rather than every tick.
     * 
     *  One slice per metric bucket spans the whole LOAD_METRIC_WINDOW, so the
     *  rolling max keeps its ~LOAD_METRIC_WINDOW_MS reach for a fraction of 
     *  the per-tick paint/scan cost. */
    static constexpr uint32_t STACK_SLICE_TICKS =
        (LOAD_METRIC_WINDOW >= Config::LOAD_METRIC_BUCKETS)
            ? LOAD_METRIC_WINDOW / Config::LOAD_METRIC_BUCKETS : 1;
    static constexpr uint32_t STACK_SLICE_MS = STACK_SLICE_TICKS * Config::TASK_PERIOD_MS;

    /** @} */

    /** @name Metric filters
     *  @{ */

    /** @brief Exponential moving average with asymmetric attack/decay: a sample
     *  above the current value is folded in with @c attack, one below it with
     *  @c decay, so the filter can rise fast and fall slow (or vice versa). */
    class ExpMovingAverage {
        /** @brief Current filtered value. */
        float _value  = 0.0f;
        /** @brief Blend factor applied to samples above the current value. */
        float _attack = 1.0f;
        /** @brief Blend factor applied to samples below the current value. */
        float _decay  = 1.0f;
    public:
        /** @brief Set the attack and decay blend factors (see ema_alpha()). */
        void  configure(float attack, float decay) {
            _attack = attack;
            _decay = decay;
        }
        /** @brief Fold one sample in, using attack if it rises and decay if it falls. */
        void  push(float x) {
            const float a = (x > _value) ? _attack : _decay;
            _value = a * x + (1.0f - a) * _value;
        }
        /** @brief Current filtered value. */
        float value() const { return _value; }
        /** @brief Clear the filter back to zero. */
        void  reset()       { _value = 0.0f; }
    };

    /** @brief Sliding-window maximum over roughly the last @p window_ticks samples,
     *  approximated with K rotating time buckets.
     *
     *  @note O(1) per sample and O(K) to read, using K words of state — instead of a
     *  full per-sample ring buffer rescanned O(window) every tick. Each bucket holds
     *  the max of one time slice; the newest is updated in place and the oldest falls
     *  off on rotation, so the effective window wobbles by up to one bucket width.
     * 
     *  @warning Assumes non-negative samples with 0 as the identity (true for the
     *  microsecond / byte metrics here). */
    template <uint32_t K>
    class WindowedMax {
        static_assert(K >= 2, "WindowedMax needs at least two buckets");
        uint32_t _bucket[K]   = {};  // K-sized bucket array
        uint32_t _head        = 0;   // index of the newest (currently filling) bucket
        uint32_t _fill        = 0;   // samples accumulated into the newest bucket
        uint32_t _bucket_span = 1;   // samples per bucket = window / K (>= 1)
    public:

        /** @brief Set the window length in ticks; clears all buckets. */
        void configure(uint32_t window_ticks) {
            _bucket_span = (window_ticks >= K) ? (window_ticks / K) : 1;
            reset();
        }

        /** @brief Fold one sample into the window. */
        void push(uint32_t v) {
            if (++_fill > _bucket_span) {       // slice elapsed: rotate, drop oldest
                _fill = 1;
                _head = (_head + 1) % K;
                _bucket[_head] = 0;
            }
            if (v > _bucket[_head]) _bucket[_head] = v;
        }

        /** @brief Current windowed maximum. */
        uint32_t value() const {
            uint32_t m = 0;
            for (uint32_t i = 0; i < K; ++i) if (_bucket[i] > m) m = _bucket[i];
            return m;
        }

        /** @brief Clear all buckets and the rotation state. */
        void reset() {
            for (uint32_t i = 0; i < K; ++i) _bucket[i] = 0;
            _head = 0; _fill = 0;
        }
    };

    /** @brief Windowed-max sized to the load-metric bucket count. */
    typedef WindowedMax<Config::LOAD_METRIC_BUCKETS> MetricWindow;

    /** @} */

    /** @name Snapshot
     *  @{ */

    /** @brief Stable performance snapshot published to external consumers */
    struct PerfSnapshot {
        /** @brief moving average of CPU load (fraction of tick interval) */
        float    cpu_load_ema           = 0.0f;
        /** @brief windowed peak work duration, microseconds */
        uint32_t cpu_loop_us_recent     = 0;
        /** @brief all-time peak work duration, microseconds */
        uint32_t cpu_loop_us_max        = 0;     

        /** @brief moving average of CPU load (fraction of tick interval) */
        float    cpu_jitter_ema         = 0.0f;
        /** @brief windowed peak tick scheduling jitter, microseconds */
        uint32_t cpu_jitter_us_recent   = 0;
        /** @brief all-time peak tick scheduling jitter, microseconds */
        uint32_t cpu_jitter_us_max      = 0;

        /** @brief moving average of stack usage (fraction of stack depth) */
        float    stack_usage_ema    = 0.0f;
        /** @brief windowed peak used stack, bytes */
        uint32_t stack_bytes_recent = 0;
        /** @brief all-time peak used stack, bytes */
        uint32_t stack_bytes_max    = 0;
        /** @brief total thread stack allocation, bytes */
        uint32_t stack_depth        = 0;
    };

    /** @} */

    /** @name Monitor
     *  @{ */

    /** @brief Measures CPU and stack metrics of the main loop iterations.
     *
     *  Usage per loop iteration: 
     *   - paint_stack() at user-code depth
     *   - then run the work
     *   - then close the window. 
     * 
     *  LoopScheduler drives this: 
     *   - work_begin() when it releases a tick
     *   - sample() at the top of the next tick to record metrics
     *   - snapshot() to expose the filtered metrics */
    class PerfMonitor {
        /** @name Configuration
         *  @{ */

        /** @brief Tick interval, microseconds, used to normalize busy/jitter into a load fraction. */
        uint32_t          _period_ms;

        /** @} */

        /** @name Work-window timing
         *  @{ */

        /** @brief True between work_begin() and the matching sample(): a window is open. */
        bool              _measuring     = false;
        /** @brief uptime_us() captured at work_begin(), the open window's start. */
        uint64_t          _work_start_us = 0;
        /** @brief Duration of the last closed work window, microseconds. */
        uint32_t          _last_busy_us  = 0;

        /** @} */

        /** @name CPU-load metrics
         *  @{ */

        /** @brief EMA of CPU load (busy time as a fraction of the tick interval). */
        ExpMovingAverage  _cpu_load;
        /** @brief All-time peak work duration, microseconds. */
        uint32_t          _cpu_loop_us_max = 0;
        /** @brief Windowed peak work duration, microseconds. */
        MetricWindow      _cpu_loop_us_window;

        /** @} */

        /** @name Scheduling-jitter metrics
         *  @brief Fed by the scheduler each tick.
         *  @{ */

        /** @brief EMA of scheduling jitter (as a fraction of the tick interval). */
        ExpMovingAverage  _jitter;
        /** @brief Windowed peak scheduling jitter, microseconds. */
        MetricWindow      _jitter_window;
        /** @brief All-time peak scheduling jitter, microseconds. */
        uint32_t          _jitter_us_max = 0;

        /** @} */

        /** @name Stack-usage metrics
         *  @brief The free stack is painted once per slice and left to accumulate the
         *  high-water mark; _stack_slices holds the per-slice peaks (one per bucket)
         *  whose max is the rolling "recent" figure.
         *  @{ */

        /** @brief Total thread stack allocation, bytes. */
        uint32_t          _stack_depth = 0;
        /** @brief Address one past the top of the stack; scans index downward from here. */
        uintptr_t         _stack_base  = 0;
        /** @brief EMA of stack usage (used bytes as a fraction of stack depth). */
        ExpMovingAverage  _stack_usage;
        /** @brief All-time peak used stack, bytes. */
        uint32_t          _stack_bytes_max = 0;
        /** @brief Per-slice stack high-water peaks; their max is the "recent" figure. */
        uint32_t          _stack_slices[Config::LOAD_METRIC_BUCKETS] = {};
        /** @brief Next _stack_slices slot to overwrite. */
        uint32_t          _stack_head        = 0;
        /** @brief Ticks accumulated into the current slice. */
        uint32_t          _stack_slice_tick  = 0;
        /** @brief Repaint the stack on the next paint_stack(). */
        bool              _repaint_pending   = true;

        /** @} */

        /** @name Published snapshot
         *  @{ */

        /** @brief Latest filtered metrics exposed via snapshot(). */
        PerfSnapshot      _snap;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        explicit PerfMonitor(uint32_t period_ms);

        /** @brief Clear all metrics and filters and republish a cleared snapshot. */
        void reset();

        /** @} */

        /** @name Per-loop measurement
         *  @{ */

        /** @brief Open the work window (loop work about to run). */
        void work_begin();

        /** @brief Close the work window: record the work duration, advance the
         *  stack slice (repaint at slice boundaries) and update the metrics. */
        void sample();

        /** @brief Record a tick's scheduling jitter.
         *  @note Fed by LoopScheduler. */
        void record_jitter(uint32_t jitter_us);

        /** @brief Repaint the free stack below the caller's SP with STACK_PAINT_BYTE,
         *  but only when a new slice is due, cheap no-op on every other tick.
         *
         *  @warning Must be inlined into the caller's frame so &sp_marker reflects the caller's
         *  SP, not this method's — hence always_inline. LoopScheduler::tick() calls this just
         *  before it releases a tick, so the painted ceiling sits at the scheduler frame's SP,
         *  one level above the loop work that then dirties the region.
         *
         *  @note Called once per released tick; between repaints the running work keeps
         *  dirtying the painted region, so its high-water at the next boundary is the worst
         *  case across the whole slice. */
        BOTANIK_NO_ASAN inline __attribute__((always_inline)) void paint_stack() {
            if (!_repaint_pending) return;
            _repaint_pending = false;
            volatile uint8_t sp_marker = 0;
            _paint_below(const_cast<uint8_t*>(&sp_marker));
        }

        /** @} */

        /** @name Snapshot access
         *  @{ */

        /** @brief Latest filtered metrics (refreshed by sample()). */
        const PerfSnapshot& snapshot() const { return _snap; }

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Binary-search the painted stack for the high-water mark, update
         *  the stack metrics. */
        BOTANIK_NO_ASAN void _measure_stack_usage();
        /** @brief Paint the free stack region below @p caller_sp. */
        BOTANIK_NO_ASAN void _paint_below(uint8_t* caller_sp);

        /** @} */
    };

    /** @} */

}}

#endif // UTILS_PERF_H
