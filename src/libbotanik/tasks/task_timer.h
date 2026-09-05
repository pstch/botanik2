#ifndef TASKS_TASK_TIMER_H
#define TASKS_TASK_TIMER_H

/**
 * @file task_timer.h
 * @brief TaskTimer - the one "is it due, how late, how many did we drop" rule.
 *
 * Multi-rate dispatch is the same decision wherever it is made: a control
 * program coming due in the scan cycle, a component that need not run on every
 * beat. One rule, shared, so the lateness handling cannot drift between them.
 *
 * No clock of its own: the caller passes the beat timestamp in, exactly as the
 * function blocks take dt_ms, so this is testable without one.
 * @note The rationale behind this file is @ref timer_split, @ref timer_offset.
 */

#include <cstdint>

namespace Botanik { namespace Tasks {

    /** @brief The offset that puts member @p index of a set out of step with
     *  the rest: one @p step_ms per index, wrapped into @p period_ms.
     *
     *  Wrapped because an offset at or past the period would skip a whole
     *  activation rather than shift one. A period of 0 (every beat) has no
     *  interval to shift inside and gets no offset.
     *
     *  Two callers derive the same thing: the beat's task registry (by rank
     *  within a group) and the scan cycle (by index in the site's program
     *  table). See @ref timer_offset. */
    constexpr uint32_t stagger_offset(uint32_t index, uint32_t step_ms,
                                      uint32_t period_ms) {
        return (period_ms == 0) ? 0 : (index * step_ms) % period_ms;
    }

    /** @brief Per-task scheduling state: when it last ran, and how often.
     *
     * RAM-resident (the task descriptor it pairs with is flash-resident and
     * const), so the NSDMIs are fine here. */
    class TaskTimer {

        /** @name State
         *  @{ */

        uint64_t _last_run_ms = 0;   ///< when it last ran (meaningless until _runs > 0)
        uint32_t _runs        = 0;   ///< completed runs; 0 = never
        /** @brief One-shot extra delay on the first interval, spent once the
         *  second activation lands. See @ref timer_offset. */
        uint32_t _offset_ms   = 0;

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
         *                       due late runs ONCE; drops are never caught up
         *                       (@ref dev_tasks).
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
            // The offset stretches the FIRST interval only (spent in ran(), so
            // it never distorts the rate). @see timer_offset
            if (period_ms != 0 && elapsed < (uint64_t)period_ms + _offset_ms) return false;

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

        /** @brief Record that the task ran at @p now_ms. The only mutator.
         *  Spends the offset on the second activation, so the rate is nominal
         *  from there on. */
        void ran(uint64_t now_ms) {
            _last_run_ms = now_ms;
            _runs++;
            if (_runs >= 2) _offset_ms = 0;
        }


        /** @brief Put this task out of step with others of the same period, by
         *  delaying its first interval by @p offset_ms. Set it before the first
         *  run; keep it under the period. See @ref timer_offset. */
        void set_offset(uint32_t offset_ms) { _offset_ms = offset_ms; }

        /** @} */
    };

}}  // namespace Botanik::Tasks

#endif // TASKS_TASK_TIMER_H
