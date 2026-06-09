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

// Stack-usage measurement (paint + scan) is fundamentally incompatible with
// AddressSanitizer: with detect_stack_use_after_return, locals move to a
// heap-backed "fake stack", so the paint and the scan touch different memory and
// the binary search walks to its cap (STACK_SCAN_LIMIT). Tagging the functions
// that paint/scan the raw stack with no_sanitize("address") keeps their allocas
// (incl. paint_stack's sp_marker) on the real thread stack — so paint and scan
// agree — and stops the downward scan from tripping a redzone false positive.
// Expands to nothing in non-sanitized builds.
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

    /** @brief EMA smoothing factor for a response time constant @p tau_ms, given
     *  the filter's sample period @p period_ms. Derived from the sample period so
     *  the filter's wall-clock response stays fixed regardless of how often it is
     *  fed (forward-Euler: alpha = dt/tau). A tau at or below one sample period
     *  collapses to 1.0 (instant, since the filter can't resolve anything faster
     *  than a single sample). Defaults to the tick period for per-tick metrics. */
    constexpr float ema_alpha(uint32_t tau_ms, uint32_t period_ms = Config::TASK_PERIOD_MS) {
        return tau_ms <= period_ms
            ? 1.0f
            : (float)period_ms / (float)tau_ms;
    }

    /** @brief Number of per-tick samples spanning LOAD_METRIC_WINDOW_MS. */
    static constexpr uint32_t LOAD_METRIC_WINDOW =
        Config::LOAD_METRIC_WINDOW_MS / Config::TASK_PERIOD_MS;

    /** @brief Stack high-water is sampled once per slice rather than every tick:
     *  the painted region is left to accumulate the worst case across a slice, then
     *  measured and repainted at the boundary. One slice per metric bucket spans the
     *  whole LOAD_METRIC_WINDOW, so the rolling max keeps its ~LOAD_METRIC_WINDOW_MS
     *  reach for a fraction of the per-tick paint/scan cost. */
    static constexpr uint32_t STACK_SLICE_TICKS =
        (LOAD_METRIC_WINDOW >= Config::LOAD_METRIC_BUCKETS)
            ? LOAD_METRIC_WINDOW / Config::LOAD_METRIC_BUCKETS : 1;
    static constexpr uint32_t STACK_SLICE_MS = STACK_SLICE_TICKS * Config::TASK_PERIOD_MS;

    /**
     * @brief Exponential moving average with asymmetric attack/decay: a sample
     *  above the current value is folded in with @c attack, one below it with
     *  @c decay, so the filter can rise fast and fall slow (or vice versa).
     */
    class Ema {
        float _value  = 0.0f;
        float _attack = 1.0f;
        float _decay  = 1.0f;
    public:
        void  configure(float attack, float decay) { _attack = attack; _decay = decay; }
        void  push(float x) {
            const float a = (x > _value) ? _attack : _decay;
            _value = a * x + (1.0f - a) * _value;
        }
        float value() const { return _value; }
        void  reset()       { _value = 0.0f; }
    };

    /**
     * @brief Sliding-window maximum over roughly the last @p window_ticks samples,
     *  approximated with K rotating time buckets.
     *
     * O(1) per sample and O(K) to read, using K words of state — instead of a full
     * per-sample ring buffer rescanned O(window) every tick. Each bucket holds the
     * max of one time slice; the newest is updated in place and the oldest falls
     * off on rotation, so the effective window wobbles by up to one bucket width.
     * Assumes non-negative samples with 0 as the identity (true for the
     * microsecond / byte metrics here).
     */
    template <uint32_t K>
    class WindowedMax {
        static_assert(K >= 2, "WindowedMax needs at least two buckets");
        uint32_t _bucket[K]   = {};
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
        void reset() {
            for (uint32_t i = 0; i < K; ++i) _bucket[i] = 0;
            _head = 0; _fill = 0;
        }
    };

    /** @brief Stable performance snapshot published to external consumers (status
     *  bar, `stats`). 
     *      EMA         : smoothed recent view; 
     *      *_max       : worst case since reset;
     *      *_recent    : worst case over sliding window */
    struct PerfSnapshot {
        float    cpu_load_ema       = 0.0f;  // fraction of the tick period, 0..1+
        float    jitter_ema         = 0.0f;  // scheduling jitter as fraction of the tick period, 0..1+
        float    stack_usage_ema    = 0.0f;  // fraction of stack_depth, 0..1
        uint32_t cpu_loop_us_max    = 0;     // all-time peak work duration, microseconds
        uint32_t cpu_loop_us_recent = 0;     // windowed peak work duration, microseconds
        uint32_t jitter_us_recent   = 0;     // windowed peak tick scheduling jitter, microseconds
        uint32_t jitter_us_max      = 0;     // all-time peak tick scheduling jitter, microseconds
        uint32_t stack_bytes_max    = 0;     // all-time peak used stack, bytes
        uint32_t stack_bytes_recent = 0;     // windowed peak used stack, bytes
        uint32_t stack_depth        = 0;     // total thread stack allocation, bytes
    };

    /**
     * @brief Measures the cost of one main-loop iteration and the stack it consumes.
     *
     * Usage per loop iteration: paint_stack() at user-code depth, run the work, then
     * close the window. LoopScheduler drives this: work_begin() when it releases a
     * tick, sample() at the top of the next tick to record the elapsed work and the
     * resulting stack high-water. snapshot() exposes the filtered metrics.
     */
    class PerfMonitor {
    public:
        explicit PerfMonitor(uint32_t period_ms);

        /** @brief Open the work window (loop work about to run). */
        void work_begin();

        /** @brief Close the work window: record the work duration, advance the stack
         *  slice (measuring its high-water and arming a repaint at slice boundaries),
         *  and update the metrics. No-op if no window is open. */
        void sample();

        /** @brief Record one tick's scheduling jitter — its lateness against the
         *  grid, in microseconds — into the rolling-max window. Fed by LoopScheduler. */
        void record_jitter(uint32_t jitter_us);

        /**
         * @brief Repaint the free stack below the caller's SP with STACK_PAINT_BYTE,
         *  but only when a new slice is due (sample() arms _repaint_pending at slice
         *  boundaries). Cheap no-op on every other tick.
         *
         * Must be inlined into the caller's frame so &sp_marker reflects the caller's
         * SP, not this method's. Call once per iteration at user-code depth; between
         * repaints the running work keeps dirtying the painted region, so its
         * high-water at the next boundary is the worst case across the whole slice.
         */
        BOTANIK_NO_ASAN inline void paint_stack() {
            if (!_repaint_pending) return;
            _repaint_pending = false;
            volatile uint8_t sp_marker = 0;
            _paint_below(const_cast<uint8_t*>(&sp_marker));
        }

        void reset();

        /** @brief Latest filtered metrics (refreshed by sample()). */
        const PerfSnapshot& snapshot() const { return _snap; }

    private:
        uint32_t  _period_ms;
        // Work-window timing
        bool      _measuring    = false;
        uint64_t  _work_start_us = 0;
        uint32_t  _last_busy_us  = 0;
        // CPU-load metrics
        Ema       _cpu_load;
        uint32_t  _cpu_loop_us_max = 0;
        WindowedMax<Config::LOAD_METRIC_BUCKETS> _cpu_loop_us_window;
        // Scheduling-jitter metric (fed by the scheduler each tick).
        Ema       _jitter;
        WindowedMax<Config::LOAD_METRIC_BUCKETS> _jitter_window;
        uint32_t  _jitter_us_max = 0;
        // Stack-usage metrics. The free stack is painted once per slice and left to
        // accumulate the high-water mark; _stack_slices holds the per-slice peaks
        // (one per bucket) whose max is the rolling "recent" figure.
        uint32_t  _stack_depth = 0;
        uintptr_t _stack_base  = 0;
        Ema       _stack_usage;
        uint32_t  _stack_bytes_max = 0;
        uint32_t  _stack_slices[Config::LOAD_METRIC_BUCKETS] = {};
        uint32_t  _stack_head        = 0;     // next slice slot to overwrite
        uint32_t  _stack_slice_tick  = 0;     // ticks accumulated into the current slice
        bool      _repaint_pending   = true;  // repaint the stack on the next paint_stack()
        // Published snapshot
        PerfSnapshot _snap;

        /** @brief Binary-search the painted stack for the high-water mark, update
         *  the stack metrics. */
        BOTANIK_NO_ASAN void _measure_stack_usage();
        /** @brief Paint the free stack region below @p caller_sp. */
        BOTANIK_NO_ASAN void _paint_below(uint8_t* caller_sp);
    };

}}

#endif // UTILS_PERF_H
