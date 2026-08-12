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

#include "libbotanik/tasks/phase.h"         // Tasks::Phase
#include "libbotanik/tasks/task_timer.h"    // Tasks::TaskTimer

// Forward-declared, never included: the registry stores a Component* and hands
// it back to the handler without ever dereferencing it, so `tasks/` stays below
// `core/` and free of every subsystem it drives.
namespace Botanik { class Component; }

namespace Botanik { namespace Tasks {

    /** @brief Run @p self's Component::setup() - the one-time init behind every
     *  registration.
     *
     *  Defined in core/component.cpp, not here: `tasks/` only forward-declares
     *  Component and must not acquire `core/` to call one method on it. */
    void component_setup(void* self, uint64_t now_ms);

    /** @brief What a handler is told about the activation it is running in.
     *
     *  The rate lives on the TASK, so the task is the only thing that knows how
     *  long it has actually been - a handler that integrates over time must read
     *  @c dt_ms rather than assume its nominal period. @c missed is what
     *  TaskTimer dropped on the way here (activations are never caught up). */
    struct Tick {
        uint64_t now_ms = 0;
        uint32_t dt_ms  = 0;
        uint16_t missed = 0;
        /** @brief This is the task's FIRST activation since it last had nothing
         *  to go on: at bring-up, and - for a program - on the beat control
         *  authority came back.
         *
         *  IEC 61131-3's first-scan flag, generalised. A handler that ramps,
         *  latches or compares against "last time" has no last time here, so it
         *  must ask rather than read whatever a member was initialised with, and
         *  it is where anything gone stale gets dropped. */
        bool     first  = false;
    };

    /** @brief Rate classes. A task names one instead of carrying a raw period,
     *  so a rate is retuned in one place for everything that shares it. */
    enum GroupId {
        GROUP_FAST = 0,    ///< every beat: the field bus, the log drain, the pumps
        GROUP_MAIN,        ///< the operator-facing rate (currently unused)
        GROUP_SLOW,        ///< dwell and config: health, parameters
        GROUP_BACKGROUND,  ///< housekeeping measured in seconds
        GROUP_COUNT
    };

    /** @brief Period of @p g in ms. Config-driven so a site retunes a whole
     *  rate class at once. */
    uint32_t group_period_ms(GroupId g);
    /** @brief Display name of @p g ("fast", "slow"...). */
    const char* group_name(GroupId g);

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

        /** @name The registered form (see add())
         *
         *  A component registers its own work: it supplies the name, the rate
         *  group and the phase, and a file-local handler that casts @c owner
         *  back to its own type. Both forms coexist until the last legacy
         *  registration is gone. @{ */
        Component* owner   = nullptr;
        void     (*handler)(Component* owner, const Tick& tick) = nullptr;
        /** @brief A control program: PHASE_CONTROL by construction, and gated by
         *  control authority. */
        bool       is_program = false;
        /** @brief Whether the next activation is a first scan (Tick::first).
         *  True at registration - nothing has run yet - and raised again on a
         *  program when control authority returns. */
        bool       first_activation = true;
        /** @} */
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

        /** @brief Whether the program entries may run. Starts false: control
         *  authority starts withheld, so nothing can drive the field on the way
         *  through bring-up. A plain bool, deliberately - `tasks/` must not
         *  acquire Vars to ask who decided it. */
        bool     _programs_enabled = false;

        /** @brief Claim a slot and fill the part both add() forms share, so the
         *  offset rule cannot drift between them. Null when the table is full
         *  (counted in overflow()). */
        Task* _claim(const char* name, GroupId group, Phase phase);

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

        /** @brief Register one piece of @p owner's work: @p handler, at @p
         *  group's rate, in @p phase.
         *
         *  The component states the phase and the rate itself, beside the code
         *  they refer to; what the caller adds is WHICH INSTANCE this is, since
         *  the same class is constructed more than once.
         *
         *  Registering also gets @p owner set up: setup() runs its on_setup()
         *  and announce() before the loop starts, once however many phases it
         *  registered in. */
        bool add(Component& owner, void (*handler)(Component*, const Tick&),
                 const char* name, GroupId group, Phase phase);

        /** @brief Register one-time init for @p owner, run by setup() before the
         *  loop starts, in registration order. Never dispatched in the beat. */
        bool add_setup(Component& owner, void (*handler)(Component*, const Tick&),
                       const char* name);

        /** @brief Register a control program. No phase: a program runs in
         *  PHASE_CONTROL or not at all. */
        bool add_program(Component& owner, void (*handler)(Component*, const Tick&),
                         const char* name, GroupId group);

        /** @brief Register a Component in phase @p P: its hook for that phase
         *  per activation, setup() once.
         *
         *  The phase is a TEMPLATE argument because the dispatcher is a plain
         *  function pointer with no captures, and it has to carry the phase into
         *  Component::step(). A component spanning phases is registered once per
         *  phase, under its own name, so each half keeps its own TaskCost and
         *  stays attributable on the TASKS page. */
        template <Phase P, typename C>
        bool add_component(const char* name, GroupId group, C& c) {
            return add(name, group, P,
                       [](void* s, uint64_t now) { static_cast<C*>(s)->step(now, P); },
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
         *  registration order, timing each one into its TaskCost.
         *
         *  A PROGRAM entry additionally needs control authority; see
         *  set_programs_enabled(). */
        void run_phase(Phase phase, uint64_t now_ms);

        /** @brief Grant or withdraw control authority: the whole edge, in one
         *  call.
         *
         *  Withdrawing shuts the gate, and that is all it needs to do - a
         *  program that is not called cannot act on what it remembers, and
         *  nothing outside a program reads its state.
         *
         *  Granting opens it and, on every program entry, **reseeds the timer
         *  and raises Tick::first**. The reseed is why this is the registry's
         *  job: the timer is the TASK's. Without it the first activation back
         *  reports the whole outage as its `dt`, and a Ton holding 5 s handed an
         *  hour of it fires the instant control returns - the one thing a
         *  withdrawal exists to prevent. `first` is the other half: it tells the
         *  program that the plant moved without it, so whatever it remembered
         *  describes a plant that no longer exists.
         *
         *  Idempotent: only an actual change of state does anything. */
        void set_programs_enabled(bool enabled, uint64_t now_ms);

        /** @brief Whether program entries are currently allowed to run. */
        bool programs_enabled() const { return _programs_enabled; }

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
