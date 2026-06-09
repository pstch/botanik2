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
     *  and the `stats` command. Performance figures live in @c perf; the scheduler
     *  adds only its own tick/overrun counters. */
    struct SchedulerContext {
        Utils::PerfSnapshot perf;          // CPU-load and stack-usage metrics
        uint32_t tick_count      = 0;
        uint32_t overruns        = 0;      // cumulative beats skipped due to overruns
    };

    class LoopScheduler {
    private:
        // Configuration & state
        uint32_t    _period_ms              = Config::TASK_PERIOD_MS;
        uint64_t    _next_tick_ms           = 0;
        // Performance instrumentation (work timing + stack usage).
        Utils::PerfMonitor _perf;
        Logs::Logger*   _logger        = nullptr;
        uint32_t        _tick_count    = 0;
        uint32_t        _overrun_count = 0;
        uint64_t        _overrun_log_last_ms = 0;  // timestamp of the last overrun warning
        uint32_t        _overrun_pending     = 0;  // beats skipped since that warning
        bool            _started       = false;  // startup message emitted on first tick()
        SchedulerContext _context      = {};

        /** @brief Advance the next-tick anchor to skip missed beats when the loop falls behind. */
        void _skip_beats(uint64_t now);
        /** @brief Emit the "scheduler started" log entry (called once, on the first tick()). */
        void _log_startup();

    public:
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

        /**
         * @brief Non-blocking check. Evaluates if it is time to run the task.
         * @return true exactly ONCE per period when it is time to execute.
         */
        bool tick();

        /**
         * @brief Performance monitor driving the CPU-load and stack-usage metrics.
         *
         * The main loop must call perf().paint_stack() at user-code depth right
         * after tick() returns true, so the subsequent work dirties the painted
         * stack region in proportion to its actual depth.
         */
        Utils::PerfMonitor& perf() { return _perf; }

        /** @brief Stable metrics snapshot for external consumers (e.g. status bar). */
        SchedulerContext* context() { return &_context; }

    };

} }

#endif // SCHEDULER_SCHEDULER_H