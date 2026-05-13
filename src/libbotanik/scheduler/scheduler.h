#ifndef SCHEDULER_SCHEDULER_H
#define SCHEDULER_SCHEDULER_H

#include <cstdint>

#include "libbotanik_config.h"

namespace Botanik { namespace Logs { class Logger; } }

namespace Botanik { namespace Scheduler {

    /** @brief Stable metrics snapshot consumed by AnsiTerminal for the status bar. */
    struct SchedulerContext {
        float    cpu_load        = 0.0f;   // instantaneous, fraction 0..1
        float    cpu_load_ema    = 0.0f;
        uint32_t stack_usage     = 0;      // instantaneous, bytes
        float    stack_usage_ema = 0.0f;   // fraction 0..1
        uint32_t stack_depth     = 0;      // total thread stack allocation in bytes
        uint32_t tick_count      = 0;
    };

    class LoopScheduler {
    private:
        // Configuration & state
        uint32_t    _period_ms              = Config::TASK_PERIOD_MS;
        uint64_t    _next_tick_ms           = 0;
        // Timing state to measure the user's external code block
        bool        _is_working             = false;
        uint64_t    _work_start_ms          = 0;
        // CPU usage metrics
        uint32_t    _last_busy_ms           = 0;
        float       _cpu_load               = 0.0f;
        float       _cpu_load_ema           = 0.0f;
        // Memory usage metrics
        uint32_t    _stack_depth       = 0;
        uintptr_t   _stack_base        = 0;
        uint32_t    _stack_usage       = 0;
        float       _stack_usage_ema   = 0;
        Logs::Logger*   _logger        = nullptr;
        uint32_t        _tick_count    = 0;
        SchedulerContext _context      = {};

        /** @brief Record elapsed work time and update CPU load metrics. */
        void _measure_work(uint64_t now);
        /** @brief Sample the current stack pointer and update stack usage metrics. */
        void _measure_stack_usage();
        /** @brief Paint the free stack region (below current SP) with STACK_PAINT_BYTE. */
        void _paint_free_stack();
        /** @brief Advance the next-tick anchor to skip missed beats when the loop falls behind. */
        void _skip_beats(uint64_t now);
#ifdef TARGET_SIMULATOR
        /** @brief Busy-wait to simulate the OPTA MCU's minimum per-cycle load. */
        void _simulate_busy(uint64_t tick_start_ms);
#endif

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

        uint32_t get_period() const { return _period_ms; }

        float get_cpu_load() const { return _cpu_load; }
        float get_cpu_load_ema() const { return _cpu_load_ema; }

        uint32_t get_stack_depth() const { return _stack_depth; }
        uint32_t get_stack_usage() const { return _stack_usage; }
        float get_stack_usage_ema() const { return _stack_usage_ema; }

        /** @brief Stable metrics snapshot for external consumers (e.g. status bar). */
        SchedulerContext* context() { return &_context; }

    };

} }

#endif // SCHEDULER_SCHEDULER_H