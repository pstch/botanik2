#ifndef SCHEDULER_SCHEDULER_H
#define SCHEDULER_SCHEDULER_H

/**
 * @file scheduler.h
 * @brief LoopScheduler - non-blocking fixed-period loop driver with overrun
 *        handling and built-in performance instrumentation: scheduling jitter,
 *        CPU duty cycle, per-beat work duration, and stack high-water usage. The
 *        reusable metric filters and the published PerfContext live in
 *        utils/filters.h; everything is published through the SchedulerContext.
 */

#include <cstdint>

#include "libbotanik_config.h"

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

    /** @brief CPU-load / work-duration / stack-usage metrics, each exposed smoothed
     *  (EMA), as a recent windowed peak, and as an all-time peak. The base of
     *  SchedulerContext; LoopScheduler writes every field in place. */
    struct PerfContext {
        /** @brief moving average of CPU load (duty cycle, fraction of the period) */
        float    cpu_load_ema       = 0.0f;
        /** @brief windowed peak work duration, microseconds */
        uint32_t cpu_loop_us_recent = 0;
        /** @brief all-time peak work duration, microseconds */
        uint32_t cpu_loop_us_max    = 0;

        /** @brief moving average of stack usage (fraction of stack depth) */
        float    stack_usage_ema    = 0.0f;
        /** @brief windowed peak used stack, bytes */
        uint32_t stack_bytes_recent = 0;
        /** @brief all-time peak used stack, bytes */
        uint32_t stack_bytes_max    = 0;
        /** @brief total thread stack allocation, bytes */
        uint32_t stack_depth        = 0;

        /** @brief measured loop rate, ticks executed per second. Nominal is
         *  1000/TASK_PERIOD_MS; dips below it when the loop falls behind. */
        uint32_t ticks_per_second   = 0;
    };

    /** @brief Stable metrics snapshot consumed by AnsiTerminal for the status bar
     *  and the `stats` command. Inherits the CPU/stack fields (PerfContext) and adds
     *  the scheduler's jitter and tick/overrun fields, so consumers read them all
     *  through one struct via LoopScheduler::context(). */
    struct SchedulerContext : PerfContext {
        /** @brief moving average of scheduling jitter (fraction of tick interval) */
        float    cpu_jitter_ema       = 0.0f;
        /** @brief windowed peak tick scheduling jitter, microseconds */
        uint32_t cpu_jitter_us_recent = 0;
        /** @brief all-time peak tick scheduling jitter, microseconds */
        uint32_t cpu_jitter_us_max    = 0;
        /** @brief Total ticks executed. */
        uint32_t tick_count      = 0;
        /** @brief Cumulative beats skipped due to overruns. */
        uint32_t overruns        = 0;
        /** @brief Loop "now" (uptime ms), republished by tick() each time the work
         *  window opens. The single shared timestamp the PLC sync layer and the
         *  terminal input timing read, so a sketch no longer carries its own.
         *  @warning 32-bit, so it wraps every ~49 days. Every consumer must compare
         *  it with wrap-safe unsigned subtraction `(uint32_t)(now - then) > delta`,
         *  never `now > then + delta` - which is the convention throughout. */
        uint32_t timestamp_ms    = 0;
    };

    /** @class LoopScheduler
     *  @brief Non-blocking fixed-period loop driver: releases one tick per period,
     *         absorbs overruns, and measures its own per-beat performance (jitter,
     *         CPU duty cycle, work duration, stack usage), publishing everything
     *         into the SchedulerContext. */
    class LoopScheduler {
    private:
        /** @name State
         *  @{ */

        // tick() sleeps until 1 ms before the grid point and busy-waits the last
        // ms, so a period under 2 ms would be all spin.
        static_assert(Config::TASK_PERIOD_MS >= 2, "TASK_PERIOD_MS must be >= 2 (sleep/spin split)");

        /** @brief Target loop period (ms). */
        uint32_t        _period_ms     = Config::TASK_PERIOD_MS;
        /** @brief Uptime anchor of the next scheduled tick. */
        uint64_t        _next_tick_ms  = 0;
        /** @brief Logger for startup and periodic reports (may be null). */
        Logs::Logger*   _logger        = nullptr;
        /** @brief Timestamp of the last overrun warning. */
        uint64_t        _overrun_log_last_ms = 0;
        /** @brief Beats skipped since that warning. */
        uint32_t        _overrun_pending     = 0;
        /** @brief Beats skipped on THIS released tick (0 normally); weights the metric
         *  windows so a skipped-beat catch-up ages them by wall-time, not by one sample. */
        uint32_t        _missed_this_beat    = 0;
        /** @brief Startup message emitted on first tick(). */
        bool            _started       = false;
        /** @brief Published metrics snapshot. */
        SchedulerContext _context      = {};

        /** @} */

        /** @name Scheduling-jitter metrics
         *  @{ */

        /** @brief EMA of scheduling jitter (as a fraction of the tick interval). */
        Utils::ExpMovingAverage _jitter;
        /** @brief Windowed peak scheduling jitter, microseconds. */
        Utils::MetricWindow     _jitter_window;
        /** @brief All-time peak scheduling jitter, microseconds. */
        uint32_t                _jitter_us_max = 0;

        /** @} */

        /** @name CPU-load (duty cycle) metrics
         *  @brief Load is the active fraction of each beat: (elapsed - slept)/elapsed,
         *  so it counts the work plus the loop/scheduler overhead and the busy-wait,
         *  and excludes only the real sleep. Both terms are measured from actual
         *  uptime deltas (not the requested sleep), so an overrunning sleep or a
         *  skipped beat self-normalizes instead of inflating the figure.
         *  @{ */

        /** @brief EMA of CPU duty cycle. */
        Utils::ExpMovingAverage _cpu_load;
        /** @brief Time actually slept so far during the beat being assembled, us. */
        uint64_t                _slept_us = 0;
        /** @brief uptime_us() of the previous release, the duty-cycle window's start. */
        uint64_t                _last_release_us = 0;

        /** @} */

        /** @name Work-duration metrics
         *  @{ */

        /** @brief True between a tick release and the matching sample at the next tick. */
        bool              _measuring     = false;
        /** @brief uptime_us() captured when the work window opened. */
        uint64_t          _work_start_us = 0;
        /** @brief All-time peak work duration, microseconds. */
        uint32_t          _cpu_loop_us_max = 0;
        /** @brief Windowed peak work duration, microseconds. */
        Utils::MetricWindow _cpu_loop_us_window;

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
        Utils::ExpMovingAverage _stack_usage;
        /** @brief All-time peak used stack, bytes. */
        uint32_t          _stack_bytes_max = 0;
        /** @brief Per-slice stack high-water peaks; their max is the "recent" figure. */
        uint32_t          _stack_slices[Config::LOAD_METRIC_BUCKETS] = {};
        /** @brief Next _stack_slices slot to overwrite. */
        uint32_t          _stack_head        = 0;
        /** @brief Ticks accumulated into the current slice. */
        uint32_t          _stack_slice_tick  = 0;
        /** @brief Repaint the stack on the next released tick. */
        bool              _repaint_pending   = true;
        /** @brief Bounds of the region _paint_below() last painted (simulator only),
         *  read back by _measure_stack_usage(). Null until the first paint. */
        uint8_t*          _paint_top         = nullptr;
        uint8_t*          _paint_bot         = nullptr;

        /** @} */

        /** @name Tick-rate metric
         *  @{ */

        /** @brief Uptime anchor (ms) of the current one-second tick-rate window. */
        uint64_t          _tps_anchor_ms     = 0;
        /** @brief tick_count snapshot taken at the window anchor. */
        uint32_t          _tps_anchor_count  = 0;
        /** @brief Whether the tick-rate window is open (a flag, not an anchor==0 sentinel:
         *  the clock is legitimately 0 for the first ~1 ms). */
        bool              _tps_open          = false;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /**
         * @brief Construct a new non-blocking Loop Scheduler
         * @param period_ms The target period of the loop in milliseconds.
         * @param logger    Optional logger for startup and periodic load reports.
         */
        LoopScheduler(uint32_t period_ms = Config::TASK_PERIOD_MS, Logs::Logger* logger = nullptr);

        /**
         * @brief Resets the scheduler anchor point to the current platform uptime and
         *        clears all metrics and filters.
         */
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
            // log startup message on first tick
            if (!_started) { _started = true; _log_startup(); }
            // measure current time (us, for jitter); derive ms from the same reading
            // so the jitter math below can never underflow against the time grid
            // (a separate uptime_ms() read can straddle the millisecond boundary and
            // land just under _next_tick_ms*1000, wrapping the unsigned subtraction).
            uint64_t now_us = HAL::uptime_us();
            uint64_t now_ms = now_us / 1000;
            // close the previous beat's work window (work duration + stack scan)
            if (_measuring) { _measuring = false; _sample_work(now_us); }
            int64_t remaining_ms = (int64_t)_next_tick_ms - (int64_t)now_ms;
            // yield/sleep until next tick
            if (remaining_ms > 0) {
                // sleep until grid point minus one millisecond, counting the *actual*
                // time slept toward the beat's idle total (the last ~1 ms is
                // busy-waited, i.e. active CPU). Timing the sleep rather than trusting
                // the request keeps the duty cycle honest when sleep_ms() overruns.
                if (remaining_ms > 1) {
                    uint64_t sleep_start_us = HAL::uptime_us();
                    HAL::sleep_ms((uint32_t)(remaining_ms - 1));
                    _slept_us += HAL::uptime_us() - sleep_start_us;
                }
                // return false so that work can be deferred
                // NOTE: this implements a busy loop started 1 ms before the grid point
                return false;
            }
            // publish the loop "now" so consumers share a single timestamp per tick
            _context.timestamp_ms = (uint32_t)now_ms;
            // skip missed beats (and log overruns)
            _skip_beats(now_ms);
            // record this beat's scheduling jitter and CPU duty cycle. Clamp the
            // grid-relative lateness at 0: _skip_beats keeps now >= the grid point
            // so it shouldn't underflow, but one wrapped sample would poison the
            // all-time max for the whole uptime, so defend against it.
            uint64_t grid_us = _next_tick_ms * 1000;
            _record_jitter(now_us > grid_us ? (uint32_t)(now_us - grid_us) : 0);
            _record_load(now_us);
            // advance time grid
            _next_tick_ms += _period_ms;
            // increment tick count and sample the achieved tick rate
            ++_context.tick_count;
            _record_tps(now_ms);
            // open the work window for the beat about to run
            _work_start_us = now_us;
            _measuring     = true;
            // Repaint the free stack for the high-water scan, at slice boundaries only.
            // Simulator only: on hardware the RTOS watermarks the thread stack itself,
            // and repainting it would clobber that watermark (see _measure_stack_usage).
            if (_repaint_pending) {
                _repaint_pending = false;
#ifdef TARGET_SIMULATOR
                _paint_below();
#endif
            }
            // return true so that work can start
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
        /** @brief Emit the "scheduler started" log entry (called once, on the first tick()). */
        void _log_startup();

        /** @brief Determine the stack high-water (RTOS watermark on OPTA, a top-down
         *  sentinel-run scan of the painted region on the sim) and update the metrics. */
        BOTANIK_NO_ASAN void _measure_stack_usage();
        /** @brief Paint the free stack below this frame with the sentinel (simulator only). */
        BOTANIK_NO_ASAN void _paint_below();

        /** @} */
    };

#ifdef TARGET_SIMULATOR
    /**
     * @brief Best-effort: lock all of the process's memory (current and future)
     *        into RAM to avoid page-fault latency.
     *
     * Simulator-only (POSIX). Process-wide via mlockall(), so call it once - it
     * also covers threads spawned afterwards. Spawn the emulator thread *before*
     * this call, though: arming MCL_FUTURE first makes pthread_create lock the
     * new thread's full stack and trip RLIMIT_MEMLOCK (EAGAIN). On failure
     * (typically lack of privileges) a warning is logged and the process keeps
     * running. Call after the log sinks are registered so the warning is not
     * dropped.
     *
     * @param logger Logger for failure warnings (optional).
     */
    void lock_process_memory(Logs::Logger* logger = nullptr);

    /**
     * @brief Best-effort: switch the *calling thread* to SCHED_FIFO and pin it to
     *        a CPU core.
     *
     * Simulator-only (POSIX). On Linux scheduling policy and affinity are
     * per-thread, so this only affects the thread that calls it - call it from
     * each thread that needs realtime scheduling (the main loop on one core, the
     * emulator on another, mirroring the OPTA's independent cores). Each step
     * that fails (typically for lack of privileges) is logged as a warning and
     * the thread keeps running.
     *
     * @param cpu_core_id Core to pin the calling thread to.
     * @param logger      Logger for failure warnings (optional).
     */
    void make_thread_realtime(int cpu_core_id, Logs::Logger* logger = nullptr);
#endif

} }

#endif // SCHEDULER_SCHEDULER_H
