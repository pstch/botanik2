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
 *
 * @section timer_offset Why there is an offset
 *
 * Every timer seeds itself on its first run, and everything runs on the first
 * beat, so without an offset every period is phase-locked to boot: two 100 ms
 * components come due on the same beat forever, and a 1000 ms one joins them
 * every second. Rates cut the MEAN work per beat and leave the PEAK exactly
 * where it was - one beat that still does the lot - and the peak is what sets
 * the shortest beat the loop can hold (see docs/dev/scheduler.md).
 *
 * The offset is a one-shot delay on the first interval only: a task with
 * offset 30 waits period+30 for its second activation and period thereafter, so
 * it runs 30 ms out of step with an unoffset task of the same period, for good.
 * Keep it under the period, and prefer a multiple of the beat (an offset the
 * beat grid cannot express just rounds up to the next beat).
 */

#include <cstdint>

namespace Botanik { namespace Scheduler {

    /** @brief The offset that puts member @p index of a set out of step with
     *  the rest: one @p step_ms per index, wrapped into @p period_ms.
     *
     *  Wrapped because an offset at or past the period would skip a whole
     *  activation rather than shift one, and a set with more members than beats
     *  in its period runs out of distinct slots (they then share, which is no
     *  worse than the phase-locked default). A period of 0 - every beat - has no
     *  interval to shift inside and gets no offset.
     *
     *  One rule, because there are two callers deriving the same thing: the
     *  beat's task registry (by rank within a group) and the scan cycle (by
     *  index in the site's program table). See @ref timer_offset. */
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
            // The offset stretches the FIRST interval only, which is what puts
            // this task permanently out of step with an unoffset one of the same
            // period. It is spent in ran(), so it never distorts the rate.
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

        /** @brief Move the reference point to @p now_ms WITHOUT counting a
         *  run, so the next activation is a full period away.
         *
         *  For a gap the caller deliberately did not step across - the scan
         *  cycle while control authority is withheld - where handing the task
         *  the whole outage as dt would be worse than dropping it. A task that
         *  has never run is left alone: its first activation is due immediately
         *  by design, and spending it here would delay bring-up by a period.
         *
         *  Distinct from ran() precisely because nothing ran: counting it would
         *  inflate runs() and make the rate readings lie. */
        void reseed(uint64_t now_ms) {
            if (_runs > 0) _last_run_ms = now_ms;
        }

        /** @brief Put this task out of step with others of the same period, by
         *  delaying its first interval by @p offset_ms. Set it before the first
         *  run; keep it under the period. See @ref timer_offset. */
        void set_offset(uint32_t offset_ms) { _offset_ms = offset_ms; }

        /** @} */
    };

}}  // namespace Botanik::Scheduler

#endif // SCHEDULER_TASK_TIMER_H
