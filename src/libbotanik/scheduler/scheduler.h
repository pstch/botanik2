#ifndef SCHEDULER_SCHEDULER_H
#define SCHEDULER_SCHEDULER_H

/**
 * @file scheduler.h
 * @brief LoopScheduler — non-blocking fixed-period loop driver with overrun
 *        handling. Per-iteration CPU-load and stack-usage metrics are delegated
 *        to PerfMonitor (utils/perf), which it drives and publishes via the
 *        SchedulerContext.
 */

#include <cstdint>

#include "libbotanik_config.h"

#include "libbotanik/utils/perf.h"
#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Logs { class Logger; } }

namespace Botanik { namespace Scheduler {

    /** @brief Stable metrics snapshot consumed by AnsiTerminal for the status bar
     *  and the `stats` command. Inherits the PerfMonitor's PerfSnapshot fields
     *  directly (cpu_load_ema, stack_bytes_recent, ...) and adds the scheduler's
     *  own tick/overrun counters, so consumers read both through one struct. */
    struct SchedulerContext : Utils::PerfSnapshot {
        /** @brief Total ticks executed. */
        uint32_t tick_count      = 0;
        /** @brief Cumulative beats skipped due to overruns. */
        uint32_t overruns        = 0;
        /** @brief Loop "now" (uptime ms), republished by tick() each time the work
         *  window opens. The single shared timestamp the PLC sync layer and the
         *  terminal input timing read, so a sketch no longer carries its own. */
        uint32_t timestamp_ms    = 0;
    };

    /** @class LoopScheduler
     *  @brief TODO */     
    class LoopScheduler {
    private:
        /** @name State
         *  @{ */

        /** @brief Target loop period (ms). */
        uint32_t        _period_ms     = Config::TASK_PERIOD_MS;
        /** @brief Uptime anchor of the next scheduled tick. */
        uint64_t        _next_tick_ms  = 0;
        /** @brief Performance instrumentation (work timing + stack usage). */
        Utils::PerfMonitor _perf;
        /** @brief Logger for startup and periodic reports (may be null). */
        Logs::Logger*   _logger        = nullptr;
        /** @brief Timestamp of the last overrun warning. */
        uint64_t        _overrun_log_last_ms = 0;
        /** @brief Beats skipped since that warning. */
        uint32_t        _overrun_pending     = 0;
        /** @brief Startup message emitted on first tick(). */
        bool            _started       = false;
        /** @brief Published metrics snapshot. */
        SchedulerContext _context      = {};

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
         * @brief Resets the scheduler anchor point to the current platform uptime.
         */
        void reset();

        /** @} */

        /** @name Tick
         *  @{ */

        /**
         * @brief Non-blocking check. Evaluates if it is time to run the task.
         *
         * When it releases a tick it also paints the free stack (via the owned
         * PerfMonitor) just before returning, so the loop work that runs next
         * dirties the painted region in proportion to its actual depth — the
         * caller no longer has to do this itself.
         *
         * @warning always_inline: this must fold into the caller's frame so the
         * paint_stack() it ends with captures the loop body's SP, not a separate
         * tick() frame's. That is why the body lives here in the header and the
         * cold helpers (_skip_beats / _log_startup) stay out of line — only the
         * hot path that reaches the paint needs to be inlined into user code.
         *
         * @return true exactly ONCE per period when it is time to execute.
         */
        inline __attribute__((always_inline)) bool tick() {
            // log startup message on first tick
            if (!_started) { _started = true; _log_startup(); }
            // measure current time (us, for jitter); derive ms from the same reading
            // so the jitter math below can never underflow against the time grid
            // (a separate uptime_ms() read can straddle the millisecond boundary and
            // land just under _next_tick_ms*1000, wrapping the unsigned subtraction).
            uint64_t now_us = HAL::uptime_us();
            uint64_t now_ms = now_us / 1000;
            int64_t remaining_ms = (int64_t)_next_tick_ms - (int64_t)now_ms;
            // measure past work + stack usage
            _perf.sample();
            // yield/sleep until next tick
            if (remaining_ms > 0) {
                // sleep until grid point minus one millisecond
                if (remaining_ms > 1) HAL::sleep_ms((uint32_t)(remaining_ms - 1));
                // return false so that work can be deferred
                // NOTE: this implements a busy loop started 1 ms before the grid point
                return false;
            }
            // publish the perf snapshot into the context (PerfSnapshot base subobject)
            static_cast<Utils::PerfSnapshot&>(_context) = _perf.snapshot();
            // publish the loop "now" so consumers share a single timestamp per tick
            _context.timestamp_ms = (uint32_t)now_ms;
            // open work window
            _perf.work_begin();
            // skip missed beats (and log overruns)
            _skip_beats(now_ms);
            // measure lateness against time grid
            _perf.record_jitter((uint32_t)(now_us - _next_tick_ms * 1000));
            // advance time grid
            _next_tick_ms += _period_ms;
            // increment tick count
            ++_context.tick_count;
            // paint the free stack for the loop work's high-water scan. Kept last so
            // the painted ceiling sits at the caller's SP, just above the work that
            // runs once we return; both this and paint_stack() are always_inline so
            // &sp_marker lands in the loop body's frame, not a PerfMonitor frame.
            _perf.paint_stack();
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
        /** @brief Emit the "scheduler started" log entry (called once, on the first tick()). */
        void _log_startup();

        /** @} */
    };

#ifdef TARGET_SIMULATOR
    /**
     * @brief Best-effort: lock memory into RAM, switch to SCHED_FIFO, pin to a CPU core.
     *
     * Simulator-only (POSIX). Tightens the simulator's scheduling so its timing
     * approximates the real PLC. Each step that fails (typically for lack of
     * privileges) is logged as a warning and the process keeps running, instead
     * of aborting. Call after the log sinks are registered so the warnings are
     * not dropped.
     *
     * @param cpu_core_id Core to pin the process to.
     * @param logger      Logger for failure warnings (optional).
     */
    void make_process_realtime(int cpu_core_id, Logs::Logger* logger = nullptr);
#endif

} }

#endif // SCHEDULER_SCHEDULER_H