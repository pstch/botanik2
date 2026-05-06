#ifndef SCHEDULER_SCHEDULER_H
#define SCHEDULER_SCHEDULER_H

#include <cstdint>

#include "libbotanik_config.h"

namespace Botanik { namespace Scheduler {

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
        uint32_t    _stack_base        = 0;
        uint32_t    _stack_usage       = 0;
        float       _stack_usage_ema   = 0;

        /** @brief Record elapsed work time and update CPU load metrics. */
        void _measure_work(uint64_t now);
        /** @brief Sample the current stack pointer and update stack usage metrics. */
        void _measure_stack_usage();
        /** @brief Advance the next-tick anchor to skip missed beats when the loop falls behind. */
        void _skip_beats(uint64_t now);

    public:
        /**
         * @brief Construct a new non-blocking Loop Scheduler
         * @param period_ms The target period of the loop in milliseconds.
         */
        LoopScheduler(uint32_t period_ms = Config::TASK_PERIOD_MS);

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

    };

} }

#endif // SCHEDULER_SCHEDULER_H