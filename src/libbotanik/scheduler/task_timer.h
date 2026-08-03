#ifndef SCHEDULER_TASK_TIMER_H
#define SCHEDULER_TASK_TIMER_H

/**
 * @file task_timer.h
 * @brief TaskTimer - the one "is it due, how late, how many did we drop" rule.
 *
 * Multi-rate dispatch is the same decision wherever it is made: a control
 * program coming due in the scan cycle, and (next) a component that need not run
 * on every beat. It lived in the scan driver, which made it the scan's rule
 * rather than the scheduler's; it is here so the second caller shares it instead
 * of reimplementing the lateness handling slightly differently.
 *
 * Two fields of state, no clock of its own: the caller passes the beat timestamp
 * in, exactly as the function blocks take dt_ms rather than reading a clock, so
 * this is testable without one.
 *
 * @section timer_split due() queries, ran() commits
 *
 * due() is const. A caller that decides not to run a due task - the scan cycle
 * while control is withheld - must not silently consume the activation, so
 * nothing moves until ran() says it did.
 */

#include <cstdint>

namespace Botanik { namespace Scheduler {

    /** @brief Per-task scheduling state: when it last ran, and how often.
     *
     * RAM-resident (the task descriptor it pairs with is flash-resident and
     * const), so the NSDMIs are fine here. */
    class TaskTimer {

        /** @name State
         *  @{ */

        uint64_t _last_run_ms = 0;   ///< when it last ran (meaningless until _runs > 0)
        uint32_t _runs        = 0;   ///< completed runs; 0 = never

        /** @} */

    public:
        /** @name Query
         *  @{ */

        /**
         * @brief Has @p period_ms elapsed since this task last ran?
         *
         * Due when @p period_ms is 0 (every beat), on the first ever call, or
         * once `now_ms - last_run_ms >= period_ms`.
         *
         * @param[in]  now_ms    the beat timestamp
         * @param[in]  period_ms run interval; 0 means every beat
         * @param[out] dt_ms     ACTUAL ms since this task last ran, never the
         *                       nominal period - a task that ran late must not
         *                       lose the time it was late by. The first run
         *                       reports @p period_ms instead: handing a timer
         *                       "everything since boot" would fire it at once.
         * @param[out] missed    activations dropped since the last run. A task
         *                       due late runs ONCE; missed activations are
         *                       dropped, never caught up, because a burst of
         *                       catch-up runs lands exactly when the system is
         *                       already behind.
         * @return true if the task should run now. Const - see @ref timer_split.
         */
        bool due(uint64_t now_ms, uint32_t period_ms,
                 uint32_t& dt_ms, uint16_t& missed) const {
            dt_ms  = 0;
            missed = 0;

            if (_runs == 0) {
                // First activation: run now, and report the nominal period.
                dt_ms = period_ms;
                return true;
            }

            uint64_t elapsed = now_ms - _last_run_ms;
            if (period_ms != 0 && elapsed < period_ms) return false;

            dt_ms = (elapsed > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)elapsed;
            if (period_ms != 0 && elapsed >= 2ull * period_ms) {
                uint64_t skipped = (elapsed / period_ms) - 1;
                missed = (skipped > 0xFFFF) ? 0xFFFF : (uint16_t)skipped;
            }
            return true;
        }

        /** @brief Completed runs; 0 until the first. */
        uint32_t runs() const { return _runs; }
        /** @brief When it last ran. Meaningless while runs() is 0. */
        uint64_t last_run_ms() const { return _last_run_ms; }

        /** @} */

        /** @name Commit
         *  @{ */

        /** @brief Record that the task ran at @p now_ms. The only mutator. */
        void ran(uint64_t now_ms) { _last_run_ms = now_ms; _runs++; }

        /** @} */
    };

}}  // namespace Botanik::Scheduler

#endif // SCHEDULER_TASK_TIMER_H
