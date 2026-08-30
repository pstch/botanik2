#ifndef TASKS_TASKS_H
#define TASKS_TASKS_H

/**
 * @file tasks.h
 * @brief The task table: periods, beat phases, and the registry that
 *        dispatches them.
 * @note GROUP is how often a task runs, PHASE where in the beat it sits, and
 *       neither derives from the other. Rate is a tuning knob; phase is a
 *       correctness property, so dispatch is phase-major and the period only
 *       answers "is it due". No priority: see docs/dev/tasks.md.
 */

#include <cstdint>

#include "libbotanik/config/scheduler.h"

#include "libbotanik/tasks/phase.h"
#include "libbotanik/tasks/task_timer.h"

// Forward-declared, never included. `Component` is the TYPE OF THE ERASED
// SELF-POINTER, which is what makes an (owner, handler) pair checkable at the
// registration site; the registry never dereferences it, so `tasks/` stays below
// `core/`. `Plc::Program` is named only to keep add_program() honest about what
// it registers.
namespace Botanik { class Component; }
namespace Botanik { namespace Plc { class Program; } }

namespace Botanik { namespace Tasks {

    /** @brief What a handler is told about the activation it is running in.
     *  @note A handler that integrates over time must read @c dt_ms rather than
     *  assume its nominal period. @c missed is what TaskTimer dropped on the way
     *  here; activations are never caught up. */
    struct Tick {
        uint64_t now_ms = 0;
        uint32_t dt_ms  = 0;
        uint16_t missed = 0;
        /** @brief The task's FIRST activation since it last had nothing to go
         *  on: at bring-up, and for a program on the beat control authority came
         *  back. IEC 61131-3's first-scan flag, generalised - a handler that
         *  ramps or latches has no "last time" here, and this is where anything
         *  gone stale gets dropped. */
        bool     first  = false;
    };

    /** @brief A task due every beat. Spelled rather than written as a
     *  number, because "as often as there is" is not a tuning choice. */
    static const uint32_t EVERY_BEAT = 0;
    /** @brief Display name of @p p ("input", "control"...). */
    const char* phase_name(Phase p);

    /** @brief Run @p c's Component::setup(), the one-time init behind every
     *  registration. Defined in core/component.cpp: `tasks/` must not acquire
     *  `core/` to call one method on it. */
    void component_setup(Component* c, uint64_t now_ms);


    /** @brief What one task costs, for the TASKS page and the stats command. Per
     *  TASK, not per group.
     *  @note Also carries what the task did NOT run: a dropped activation is
     *  never caught up, so this count is the only trace it happened. */
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
     *  @note ONE dispatch shape - an owning Component plus a named handler
     *  taking a Tick - so run_phase() has nothing to branch on. Anything wanting
     *  work in the beat is a Component, or wraps itself in one. */
    struct Task {
        const char* name     = nullptr;   ///< shown in the TASKS table
        Phase       phase    = PHASE_SERVICE;

        /** @name The dispatch
         *  A component supplies the name, period, phase and a named handler
         *  that casts @c owner back to its own type. @{ */
        Component* owner   = nullptr;
        void     (*handler)(Component* owner, const Tick& tick) = nullptr;
        /** @brief Non-null iff this entry is a control PROGRAM: PHASE_CONTROL by
         *  construction, gated by control authority. The same object as
         *  @c owner, typed, because the registry IS the program list. */
        Plc::Program* program = nullptr;
        /** @brief Whether the next activation is a first scan (Tick::first).
         *  True at registration - nothing has run yet - and raised again on a
         *  program when control authority returns. */
        bool       first_activation = true;
        /** @brief The operator's standing choice differs from the stored copy.
         *  Setting and saving are two steps, as they are for a parameter.
         *  @note The choice ITSELF lives on the Program - see
         *  Plc::Program::disable_requested(). One question, one answer. */
        /** @} */
        /** @brief How often it runs, in ms. Stated by whoever registers it:
         *  a rate is a property of the work, not of a class the work was
         *  sorted into. `EVERY_BEAT` resolves to TASK_PERIOD_MS. */
        uint32_t    period_ms = 0;
        TaskTimer   timer;                ///< period from period_ms, own offset
        TaskCost    cost;
    };

    /** @class Registry
     *  @brief The task table: registration, phase-major dispatch, and the
     *         per-task numbers the HMI reads back.
     *  @note Fixed capacity, no allocation. Frozen once the loop starts, which
     *  is what lets the TASKS page hold a stable row order. */
    class Registry {

        /** @name State
         *  @{ */

        Task     _tasks[Config::MAX_TASKS];
        uint8_t  _count = 0;
        /** @brief Registrations refused for want of capacity. Surfaced, never
         *  silently dropped: a task missing from the beat must not have to be
         *  noticed by its absence. */
        uint8_t  _overflow = 0;

        /** @brief Whether the program entries may run. Starts false: control
         *  authority starts withheld, so nothing can drive the field on the way
         *  through bring-up. A plain bool, deliberately - `tasks/` must not
         *  acquire Vars to ask who decided it. */

        /** @brief Claim a slot and fill the part both add() forms share, so the
         *  offset rule cannot drift between them. Null when the table is full
         *  (counted in overflow()). */
        Task* _claim(const char* name, uint32_t period_ms, Phase phase);

        /** @brief Table index of each PROGRAM entry, in registration order.
         *  Filled by add_program(), which is where the dense program index is
         *  assigned - so program(), the enable flags and the writer id all read
         *  one numbering rather than each re-deriving it. */
        uint8_t  _prog[Config::MAX_TASKS];
        uint8_t  _prog_count = 0;

        /** @brief The @p i'th program's entry, or null past the last. */
        Task*       _program_task(uint8_t i);
        const Task* _program_task(uint8_t i) const;

        /** @} */

    public:
        /** @name Registration
         *  @{ */

        /** @brief Register one piece of @p owner's work: @p handler, at @p
         *  @p period_ms, in @p phase. The component states phase and rate itself;
         *  the caller adds WHICH INSTANCE this is.
         *  @note Registering also sets @p owner up: on_setup() and announce() run
         *  once, however many phases it registered in. */
        bool add(Component& owner, void (*handler)(Component*, const Tick&),
                 const char* name, uint32_t period_ms, Phase phase);

        /** @brief The same, dispatching straight to @p owner's own hook for @p
         *  phase. What a component wants when its handler would be nothing but
         *  a cast back to itself - which was 15 of them.
         *  @note No first_activation flag reaches the hook: Component's per-phase
         *  hooks take now_ms alone. A task that needs the tick registers a
         *  handler. */
        bool add(Component& owner, const char* name, uint32_t period_ms, Phase phase);

        /** @brief Register one-time init for @p owner, run by setup() before the
         *  loop starts, in registration order. Never dispatched in the beat. */
        bool add_setup(Component& owner, void (*handler)(Component*, const Tick&),
                       const char* name);

        /** @brief Register a control program. No phase: it runs in PHASE_CONTROL
         *  or not at all, gated by control authority.
         *  @note Takes a Plc::Program, not a Component, so a non-program cannot
         *  be registered as one and quietly acquire the gate. */
        bool add_program(Plc::Program& program,
                         void (*handler)(Component*, const Tick&),
                         const char* name, uint32_t period_ms);

        /** @} */

        /** @name Dispatch
         *  @{ */

        /**
         * @brief Run every registered task's one-time init, phase-major, before
         *        the loop starts. Explicit rather than lazy: the parameter load
         *        alone would read as a boot overrun inside a 10 ms budget.
         * @warning Call this LATE in BotanikFirmware::setup(), after the log
         *          sinks exist: it runs each component's announce(), and
         *          HealthMonitor's POST logs its verdict from here.
         */
        void setup(uint64_t now_ms);

        /** @brief Run every task in @p phase that has come due, in
         *  registration order, timing each into its TaskCost. A PROGRAM entry
         *  additionally needs control authority. */
        void run_phase(Phase phase, uint64_t now_ms);


        /** @brief Whether program entries are currently allowed to run. */

        /** @} */

        /** @name Program lookup
         *  @{ */

        /** @brief Program index of @p name (the name it registered under), or
         *  -1. @p len 0 means NUL-terminated. */
        int16_t find_program(const char* name, uint16_t len = 0) const;

        /** @} */

        /** @name Readback (the TASKS page, the stats command)
         *  @{ */

        uint8_t     count() const { return _count; }

        /** @brief The @p i'th registered PROGRAM, or null past the last. The
         *  registry is where they live, so lateness and writer-naming walk this
         *  rather than a site-side table. */
        Plc::Program* program(uint8_t i) const;
        /** @brief How many programs are registered. */
        uint8_t       program_count() const;
        const Task& task(uint8_t i) const { return _tasks[i < _count ? i : 0]; }
        uint8_t     overflow() const { return _overflow; }

        /** @} */

    private:
    };

}}  // namespace Botanik::Tasks

#endif // TASKS_TASKS_H
