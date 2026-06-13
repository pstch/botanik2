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
    };

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
         * @return true exactly ONCE per period when it is time to execute.
         */
        bool tick();

        /** @} */

        /** @name Metrics access
         *  @{ */

        /**
         * @brief Paint the free stack for the per-iteration stack-usage scan.
         *
         * The main loop must call this at user-code depth right after tick()
         * returns true, so the subsequent work dirties the painted region in
         * proportion to its actual depth. Forwards to the owned PerfMonitor;
         * kept inline so the painted ceiling reflects the caller's stack pointer.
         */
        inline void paint_stack() { _perf.paint_stack(); }

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