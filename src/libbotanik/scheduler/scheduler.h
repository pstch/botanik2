#ifndef SCHEDULER_SCHEDULER_H
#define SCHEDULER_SCHEDULER_H

/**
 * @file scheduler.h
 * @brief LoopScheduler - non-blocking fixed-period loop driver with overrun
 *        handling and built-in performance instrumentation: scheduling jitter,
 *        CPU duty cycle, per-beat work duration, and stack high-water usage. The
 *        reusable metric filters live in utils/filters.h; the SchedulerContext
 *        they feed is defined here.
 */

#include <cstdint>

#include "libbotanik_config.h"

#include "libbotanik/component.h"
#include "libbotanik/utils/filters.h"
#include "libbotanik/hal/platform.h"

// Because the address sanitizer uses a fake stack for locals, stack-usage
// measurement is fundamentally incompatible with ASAN; the BOTANIK_NO_ASAN macro
// annotates the stack paint/scan code so it works against the real stack.
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

namespace Botanik { namespace Logs { class Logger; } }

namespace Botanik { namespace Scheduler {

    /** @brief Stable metrics snapshot consumed by AnsiTerminal for the status bar and
     *  the `stats` command: the CPU-load / work-duration / stack-usage metrics (each
     *  exposed smoothed (EMA), as a recent windowed peak, and as an all-time peak),
     *  plus the scheduler's jitter and tick/overrun fields. Consumers read them all
     *  through one struct via LoopScheduler::context(); LoopScheduler writes every
     *  field in place. */
    struct SchedulerContext {
        float    cpu_load_ema       = 0.0f;   ///< EMA of CPU load (duty cycle, fraction of period)
        uint32_t cpu_loop_us_recent = 0;      ///< windowed peak work duration, us
        uint32_t cpu_loop_us_max    = 0;      ///< all-time peak work duration, us

        /** @brief peak stack usage as a fraction of depth (worst-ever); the high-water is
         *  itself a max, so no EMA/window on top of it. */
        float    stack_usage        = 0.0f;
        /** @brief minimum free stack ever, bytes (= depth - peak used): the
         *  safety-relevant worst-case headroom. */
        uint32_t stack_bytes_free   = 0;
        uint32_t stack_depth        = 0;   ///< total thread stack allocation, bytes

        /** @brief measured loop rate (ticks/sec). Nominal 1000/TASK_PERIOD_MS; dips when
         *  the loop falls behind. */
        uint32_t ticks_per_second   = 0;

        float    cpu_jitter_ema       = 0.0f;   ///< EMA of scheduling jitter (fraction of interval)
        uint32_t cpu_jitter_us_recent = 0;      ///< windowed peak jitter, us
        uint32_t cpu_jitter_us_max    = 0;      ///< all-time peak jitter, us
        uint32_t tick_count      = 0;
        uint32_t overruns        = 0;   ///< cumulative beats skipped to overruns
        /** @brief Loop "now" (uptime ms), republished by tick() when the work window opens;
         *  the single shared timestamp the PLC sync and terminal input timing read.
         *  @warning 32-bit, wraps every ~49 days. Every consumer must use wrap-safe
         *  subtraction `(uint32_t)(now - then) > delta`, never `now > then + delta`. */
        uint32_t timestamp_ms    = 0;
    };

    /** @class LoopScheduler
     *  @brief Non-blocking fixed-period loop driver: releases one tick per period,
     *         absorbs overruns, and measures its own per-beat performance (jitter,
     *         CPU duty cycle, work duration, stack usage), publishing everything
     *         into the SchedulerContext. */
    class LoopScheduler : public Component {
    private:
        /** @name State
         *  @{ */

        // tick() sleeps until 1 ms before the grid point and busy-waits the last
        // ms, so a period under 2 ms would be all spin.
        static_assert(Config::TASK_PERIOD_MS >= 2, "TASK_PERIOD_MS must be >= 2 (sleep/spin split)");

        uint32_t        _period_ms     = Config::TASK_PERIOD_MS;   ///< target loop period (ms)
        uint64_t        _next_tick_ms  = 0;   ///< uptime anchor of the next tick
        Logs::Logger*   _logger        = nullptr;   ///< one-shot startup message (may be null)
        /** @brief Beats skipped on THIS tick (0 normally); weights the metric windows so a
         *  catch-up ages them by wall-time, not one sample. */
        uint32_t        _missed_this_beat    = 0;
        SchedulerContext _context      = {};   ///< published metrics snapshot

        /** @} */

        /** @name Scheduling-jitter metrics
         *  @{ */

        Utils::ExpMovingAverage _jitter;          ///< EMA (fraction of tick interval)
        Utils::MetricWindow     _jitter_window;   ///< windowed peak, us
        uint32_t                _jitter_us_max = 0;   ///< all-time peak, us

        /** @} */

        /** @name CPU-load (duty cycle) metrics
         *  @brief Load is the active fraction of each beat: (elapsed - slept)/elapsed,
         *  so it counts the work plus the loop/scheduler overhead and the busy-wait,
         *  and excludes only the real sleep. Both terms are measured from actual
         *  uptime deltas (not the requested sleep), so an overrunning sleep or a
         *  skipped beat self-normalizes instead of inflating the figure.
         *  @{ */

        Utils::ExpMovingAverage _cpu_load;   ///< EMA of CPU duty cycle
        uint64_t                _slept_us = 0;   ///< time slept so far this beat, us
        uint64_t                _last_release_us = 0;   ///< previous release (duty-cycle window start)

        /** @} */

        /** @name Work-duration metrics
         *  @{ */

        bool              _measuring     = false;   ///< true between a release and the next-tick sample
        uint64_t          _work_start_us = 0;        ///< work-window open time, us
        uint32_t          _cpu_loop_us_max = 0;      ///< all-time peak, us
        Utils::MetricWindow _cpu_loop_us_window;     ///< windowed peak, us

        /** @} */

        /** @name Stack-usage metrics
         *  @brief The high-water mark is itself a max (the RTOS thread watermark on OPTA,
         *  a per-slice painted high-water on the sim), so we just track the all-time peak
         *  - no EMA or rolling-max window layered on top of a max.
         *  @{ */

        uint32_t          _stack_depth = 0;        ///< total thread stack allocation, bytes
        uintptr_t         _stack_base  = 0;        ///< one past the stack top; scans downward
        uint32_t          _stack_bytes_max = 0;    ///< all-time peak used, bytes
        uint32_t          _stack_slice_tick  = 0;  ///< ticks into the current slice
        bool              _repaint_pending   = true;   ///< repaint on the next tick
        /** @brief Bounds of the region _paint_below() last painted (sim only), read back
         *  by _measure_stack_usage(). Null until the first paint. */
        uint8_t*          _paint_top         = nullptr;
        uint8_t*          _paint_bot         = nullptr;

        /** @} */

        /** @name Tick-rate metric
         *  @{ */

        uint64_t          _tps_anchor_ms     = 0;   ///< one-second window anchor (ms)
        uint32_t          _tps_anchor_count  = 0;   ///< tick_count at the anchor
        /** @brief Window open? A flag, not an anchor==0 sentinel: the clock is legitimately
         *  0 for the first ~1 ms. */
        bool              _tps_open          = false;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Construct the loop scheduler.
         *  @param period_ms Target loop period (ms).
         *  @param logger    Optional logger for startup and periodic load reports. */
        LoopScheduler(uint32_t period_ms = Config::TASK_PERIOD_MS, Logs::Logger* logger = nullptr);

        /** @brief Reset the anchor to the current uptime and clear all metrics/filters. */
        void reset();

        /** @} */

        /** @name Tick
         *  @{ */

        /**
         * @brief Non-blocking check. Evaluates if it is time to run the task.
         *
         * On each call it closes the previous beat's work window (work duration +
         * stack scan); while waiting it accumulates the slept time used for the CPU
         * duty cycle. When it releases a beat it republishes the loop "now", records
         * the scheduling jitter and the duty cycle, opens the next work window, and
         * paints the free stack so the loop work that runs next dirties it.
         *
         * @warning always_inline + BOTANIK_NO_ASAN: it must fold into the caller's
         * frame so the stack paint it ends with captures the loop body's SP (and a
         * real-stack &sp_marker, not an ASAN fake-stack one), not a separate tick()
         * frame's. That is why the body lives here and the cold helpers stay out of
         * line - only the hot path that reaches the paint needs inlining.
         *
         * @return true exactly ONCE per period when it is time to execute.
         */
        BOTANIK_NO_ASAN inline __attribute__((always_inline)) bool tick() {
            announce_step(_logger, "LoopScheduler");
            // Derive ms from the same us reading so the jitter math can't underflow against
            // the grid (a separate uptime_ms() read could straddle the ms boundary and land
            // just under _next_tick_ms*1000, wrapping the unsigned subtraction).
            uint64_t now_us = HAL::uptime_us();
            uint64_t now_ms = now_us / 1000;
            // close the previous beat's work window (work duration + stack scan)
            if (_measuring) { _measuring = false; _sample_work(now_us); }
            int64_t remaining_ms = (int64_t)_next_tick_ms - (int64_t)now_ms;
            if (remaining_ms > 0) {
                // Sleep until the grid minus 1 ms (the last ~1 ms is busy-waited = active
                // CPU), counting the *actual* slept time toward idle: timing the sleep
                // rather than trusting the request keeps the duty cycle honest on overrun.
                if (remaining_ms > 1) {
                    uint64_t sleep_start_us = HAL::uptime_us();
                    HAL::sleep_ms((uint32_t)(remaining_ms - 1));
                    _slept_us += HAL::uptime_us() - sleep_start_us;
                }
                return false;   // busy-wait the last ms; defer work
            }
            // publish the loop "now" so consumers share one timestamp per tick
            _context.timestamp_ms = (uint32_t)now_ms;
            _skip_beats(now_ms);   // no log; surfaced via CPU_XRUN + stats
            // Clamp grid-relative lateness at 0: _skip_beats keeps now >= grid so it
            // shouldn't underflow, but one wrapped sample would poison the all-time max.
            uint64_t grid_us = _next_tick_ms * 1000;
            _record_jitter(now_us > grid_us ? (uint32_t)(now_us - grid_us) : 0);
            _record_load(now_us);
            _next_tick_ms += _period_ms;
            ++_context.tick_count;
            _record_tps(now_ms);
            // open the work window for the beat about to run
            _work_start_us = now_us;
            _measuring     = true;
            // Repaint the free stack for the high-water scan, at slice boundaries only.
            // Sim only: on hardware the RTOS watermarks the thread stack, and repainting
            // would clobber that watermark (see _measure_stack_usage).
            if (_repaint_pending) {
                _repaint_pending = false;
#ifdef TARGET_SIMULATOR
                _paint_below();
#endif
            }
            return true;
        }

        /** @} */

        /** @name Metrics access
         *  @{ */

        /** @brief Stable metrics snapshot for external consumers (e.g. status bar). */
        SchedulerContext* context() { return &_context; }

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Advance the next-tick anchor to skip missed beats when the loop falls behind. */
        void _skip_beats(uint64_t now);
        /** @brief Filter a beat's scheduling jitter and publish it into the context. */
        void _record_jitter(uint32_t jitter_us);
        /** @brief Compute the beat's CPU duty cycle (active fraction of the elapsed
         *  beat) from the actual slept time and publish it. */
        void _record_load(uint64_t now_us);
        /** @brief Sample the achieved tick rate once per second and publish it.
         *  @p now_ms is the loop's (dilated) uptime; on the simulator the window
         *  is measured in real time instead, so the rate stays meaningful under
         *  BOTANIK_TIME_SCALE. */
        void _record_tps(uint64_t now_ms);
        /** @brief Close the work window: record work duration and, at slice boundaries,
         *  scan the stack high-water mark; publish both into the context. */
        BOTANIK_NO_ASAN void _sample_work(uint64_t now_us);

        /** @brief Determine the stack high-water (RTOS watermark on OPTA, a top-down
         *  sentinel-run scan of the painted region on the sim) and update the metrics. */
        BOTANIK_NO_ASAN void _measure_stack_usage();
        /** @brief Paint the free stack below this frame with the sentinel (simulator only). */
        BOTANIK_NO_ASAN void _paint_below();

        /** @} */
    };

#ifdef TARGET_SIMULATOR
    /**
     * @brief Best-effort: lock all process memory (current + future) into RAM to avoid
     *        page-fault latency. Sim-only (POSIX), process-wide via mlockall(), call once.
     *
     * Spawn the emulator thread *before* this: arming MCL_FUTURE first makes
     * pthread_create lock the new thread's full stack and trip RLIMIT_MEMLOCK (EAGAIN).
     * Call after the log sinks are registered (so the failure warning isn't dropped). On
     * failure (usually lack of privileges) the process keeps running.
     *
     * @param logger Logger for failure warnings (optional).
     */
    void lock_process_memory(Logs::Logger* logger = nullptr);

    /**
     * @brief Best-effort: switch the *calling thread* to SCHED_FIFO and pin it to a core.
     *        Sim-only (POSIX).
     *
     * Policy and affinity are per-thread on Linux, so call it from each thread that needs
     * realtime (main loop on one core, emulator on another, mirroring the OPTA's cores).
     * On failure (usually lack of privileges) the thread keeps running.
     *
     * @param cpu_core_id Core to pin the calling thread to.
     * @param logger      Logger for failure warnings (optional).
     */
    void make_thread_realtime(int cpu_core_id, Logs::Logger* logger = nullptr);
#endif

} }

#endif // SCHEDULER_SCHEDULER_H
