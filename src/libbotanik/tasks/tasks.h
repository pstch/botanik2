#ifndef TASKS_TASKS_H
#define TASKS_TASKS_H

/**
 * @file tasks.h
 * @brief The task table: rate groups, beat phases, and the registry that
 *        dispatches them.
 *
 * Two independent attributes, and conflating them is the mistake to avoid.
 * **Group** is how often a task runs. **Phase** is where in the beat it sits.
 * Neither derives from the other: the field-bus client is fast-rate and split
 * across two phases, the expiry sweep is background-rate and input-phase.
 *
 * Rate is a tuning knob; phase is a correctness property, because it encodes the
 * data flow (@ref dev_scan). So a group must never imply an order, or retuning a
 * rate would silently reorder the cycle - which is why dispatch is phase-major
 * and the group only answers "is it due".
 *
 * There is deliberately no priority: preemption does not exist here (one
 * cooperative loop, every step runs to completion), and the only other reading
 * would be load-shedding, which must not shed a control program automatically.
 */

#include <cstdint>                              // uint8_t/uint32_t/uint64_t

#include "libbotanik/config/scheduler.h"  // Config::LOOP_* / TASK_* / GROUP_*

#include "libbotanik/tasks/task_timer.h"    // Tasks::TaskTimer

namespace Botanik { namespace Tasks {

    /** @brief Rate classes. A task names one instead of carrying a raw period,
     *  so a rate is retuned in one place for everything that shares it. */
    enum GroupId {
        GROUP_FAST = 0,    ///< every beat: the field bus, the log drain, the pumps
        GROUP_MAIN,        ///< the operator-facing rate (currently unused)
        GROUP_SLOW,        ///< dwell and config: health, parameters
        GROUP_BACKGROUND,  ///< housekeeping measured in seconds
        GROUP_COUNT
    };

    /** @brief Where in the beat a task runs. The order is the data flow, fixed
     *  here and not registerable: commit the field, run the programs, collect
     *  what they drove, then report. */
    enum Phase {
        PHASE_INPUT = 0,   ///< field commit, serial RX, expiry
        PHASE_CONFIG,      ///< parameter commit, before anything reads config
        PHASE_SERVICE,     ///< health, network - background state machines
        PHASE_CONTROL,     ///< the control programs
        PHASE_OUTPUT,      ///< collect driven outputs, hand them to the field
        PHASE_PRESENT,     ///< log drain, render, serial TX
        PHASE_COUNT
    };

    /** @brief Period of @p g in ms. Config-driven so a site retunes a whole
     *  rate class at once. */
    uint32_t group_period_ms(GroupId g);
    /** @brief Display name of @p g ("fast", "slow"...). */
    const char* group_name(GroupId g);
    /** @brief Display name of @p p ("input", "control"...). */
    const char* phase_name(Phase p);

    /** @brief What one task costs, for the TASKS page and the stats command.
     *
     *  Per TASK, not per group: "the slow group cost 3 ms" does not say which of
     *  its members to look at.
     *
     *  It also carries what the task did NOT run: TaskTimer drops a late task's
     *  missed activations rather than catching them up, and this count is the
     *  only trace that it happened. */
    struct TaskCost {
        uint32_t us_last  = 0;   ///< duration of the most recent run
        uint32_t us_max   = 0;   ///< all-time peak (diagnostic only)
        uint64_t us_total = 0;   ///< accumulated, for the mean
        uint32_t runs     = 0;   ///< completed runs
        /** @brief Activations dropped since boot, never caught up. A count, not
         *  a rate: the question is "has this task ever been skipped", which a
         *  decaying figure cannot answer. */
        uint32_t missed   = 0;

        /** @brief Fold one run's duration in, plus the activations that were
         *  dropped on the way to it (Tasks::TaskTimer::due()). */
        void record(uint32_t us, uint16_t dropped = 0) {
            us_last   = us;
            us_total += us;
            runs++;
            missed   += dropped;
            if (us > us_max) us_max = us;
        }
        /** @brief Mean run duration in us; 0 before the first run. */
        uint32_t us_mean() const {
            return (runs == 0) ? 0 : (uint32_t)(us_total / runs);
        }
    };

    /** @brief One scheduled unit: a name, a rate, a slot in the beat, and the
     *  call that advances it.
     *
     *  The dispatch is a plain function pointer plus a `self`, not a Component*,
     *  so this stays free of every subsystem it drives - including Vars, which
     *  must not become a dependency here. add_component() wraps the common
     *  case. */
    struct Task {
        const char* name     = nullptr;   ///< shown in the TASKS table
        GroupId     group    = GROUP_FAST;
        Phase       phase    = PHASE_SERVICE;
        void      (*run)(void* self, uint64_t now_ms) = nullptr;
        /** @brief One-time init, run by Registry::setup() before the loop starts.
         *  Optional: null for a task with nothing to set up. */
        void      (*setup)(void* self, uint64_t now_ms) = nullptr;
        void*       self     = nullptr;
        TaskTimer   timer;                ///< period from the group, own offset
        TaskCost    cost;
    };

    /** @class Registry
     *  @brief The task table: registration, phase-major dispatch, and the
     *         per-task numbers the HMI reads back.
     *
     *  Fixed capacity, no allocation. Registration happens during bring-up and
     *  the table is frozen once the loop starts, which is what lets the TASKS
     *  page hold a stable row order across redraws. */
    class Registry {

        /** @name State
         *  @{ */

        Task     _tasks[Config::MAX_TASKS];
        uint8_t  _count = 0;
        /** @brief Registrations refused for want of capacity. Surfaced, never
         *  silently dropped: a task missing from the beat must not have to be
         *  noticed by its absence. */
        uint8_t  _overflow = 0;

        /** @} */

    public:
        /** @name Registration
         *  @{ */

        /**
         * @brief Register @p name to run @p run at @p group's rate, in @p phase.
         *
         * The offset keeping this task off the same beat as its group-mates is
         * derived: each new member of a group is placed one beat further along,
         * wrapped into the group's period (@ref timer_offset).
         *
         * @return false if the table is full (counted in overflow()).
         */
        bool add(const char* name, GroupId group, Phase phase,
                 void (*run)(void* self, uint64_t now_ms), void* self,
                 void (*setup)(void* self, uint64_t now_ms) = nullptr);

        /** @brief Register a Component: step() per activation, setup() once. */
        template <typename C>
        bool add_component(const char* name, GroupId group, Phase phase, C& c) {
            return add(name, group, phase,
                       [](void* s, uint64_t now) { static_cast<C*>(s)->step(now); },
                       &c,
                       [](void* s, uint64_t now) { static_cast<C*>(s)->setup(now); });
        }

        /** @} */

        /** @name Dispatch
         *  @{ */

        /**
         * @brief Run every registered task's one-time init, phase-major, before
         *        the loop starts.
         *
         * Explicit, not lazy on a component's first step(): the parameter load
         * alone is tens of milliseconds of flash inside a 10 ms budget, which
         * would read as a boot overrun.
         *
         * Phase-major, like run_phase(), so init obeys the same data-flow order
         * the beat does - parameters are loaded before anything reads config.
         *
         * @warning Call this LATE in BotanikFirmware::setup(), after the log
         * sinks are registered: it is what runs each component's announce(), and
         * HealthMonitor's POST logs its verdict from here.
         */
        void setup(uint64_t now_ms);

        /** @brief Run every task in @p phase whose group has come due, in
         *  registration order, timing each one into its TaskCost. */
        void run_phase(Phase phase, uint64_t now_ms);

        /** @} */

        /** @name Readback (the TASKS page, the stats command)
         *  @{ */

        uint8_t     count() const { return _count; }
        const Task& task(uint8_t i) const { return _tasks[i < _count ? i : 0]; }
        uint8_t     overflow() const { return _overflow; }

        /** @} */
    };

}}  // namespace Botanik::Tasks

#endif // TASKS_TASKS_H
