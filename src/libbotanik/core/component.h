// REVIEW_35: WIP
#ifndef LIBBOTANIK_CORE_COMPONENT_H
#define LIBBOTANIK_CORE_COMPONENT_H

/**
 * @file component.h
 * @brief Component - uniform lifecycle base for the long-lived main objects.
 *
 * Work reaches the beat by REGISTRATION: register_tasks() hands the registry one
 * entry per point the component acts at, each naming its own phase and rate
 * group, and registering also runs setup().
 * @note A component does not fetch its own configuration: it is HANDED one
 *       through set_config(). See vars/bindings.h.
 * @note A component has no rate of its own: that belongs to its TASK, and the
 *       elapsed time is Tasks::Tick::dt_ms. See docs/dev/tasks.md.
 */

#include <cstdint>

#include "libbotanik/core/setup.h"
#include "libbotanik/tasks/phase.h"

// Forward-declared, NOT included: keeps Component free of the Logger include so
// Logger itself can derive it (the body of announce() lives in component.cpp).
namespace Botanik { namespace Logs { class Logger; } }
// Forward-declared, not included: a component names the registry to hand it
// its tasks, and must not acquire the whole of tasks/ to do so.
namespace Botanik { namespace Tasks { class Registry; } }

namespace Botanik {

    /** @brief Uniform setup/config/health lifecycle base (see file header). */
    class Component {
    protected:
        /** @name Wiring (accessible to the derived component)
         *  @{ */

        /** @brief The table this component registers in. Borrowed, and the
         *  reason register_tasks() takes no argument: there is one table, and a
         *  component was told which at construction. */
        Tasks::Registry* _tasks;
        Logs::Logger* _logger;            ///< announce + derived logging (may be null)
        const char*   _name;              ///< announce label
        bool          _started = false;   ///< first-step latch (setup + announce)

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Built with the table it will register in and the log it
         *  speaks through. @see SetupContext */
        Component(const SetupContext& ctx, const char* name)
            : _tasks(ctx.tasks), _logger(ctx.logger), _name(name) {}
        virtual ~Component() {}

        /** @brief The announce label, for anything naming this component to a
         *  human. Borrowed; every constructor takes one. */
        const char* name() const { return _name; }

        /** @brief Run this component's one-time init: on_setup() then
         *  announce(). IDEMPOTENT - the registry calls it per registered task,
         *  and step() calls it for a component the registry does not own.
         *  @note Only once the log sinks are registered: announce(), and
         *  anything setup() logs, goes nowhere before that. */
        void setup(uint64_t now_ms) {
            if (_started) return;
            _started = true;
            on_setup(now_ms);
        }
        /** @brief Whether setup() has run. */
        bool started() const { return _started; }

        /** @brief The BOOT/DEBUG roll-call line for ONE registered task (see
         *  component.cpp). Called by the registry, once per task, after
         *  setup(). Compiled out when DEBUG is undefined.
         *  @note Group and phase as plain bytes: `core/` sits above `tasks/`
         *  and does not take its enums into this header. */
        void announce_task(const char* task, uint8_t group, uint8_t phase);

        /** @brief Put this component's work in the beat: one entry per point it
         *  acts at, each with its own name, rate group and phase. No argument -
         *  the table came with the SetupContext. Default: none. */
        virtual void register_tasks() {}

        /** @brief Run @p phase's hook with the lifecycle the registry gives a
         *  registered task: setup(), then the hook. For a component the task
         *  table does not own. No due check: the rate belongs to the task.
         *  @warning Keep equal to Registry::run_phase()'s per-task body. */
        void step(uint64_t now_ms, Tasks::Phase phase) {
            setup(now_ms);          // idempotent: a no-op once we are set up
            switch (phase) {
                case Tasks::PHASE_INPUT:   on_input(now_ms);   break;
                case Tasks::PHASE_CONFIG:  on_config(now_ms);  break;
                case Tasks::PHASE_SERVICE: on_service(now_ms); break;
                case Tasks::PHASE_CONTROL: on_control(now_ms); break;
                case Tasks::PHASE_OUTPUT:  on_output(now_ms);  break;
                case Tasks::PHASE_PRESENT: on_present(now_ms); break;
                default: break;
            }
        }

        /** @brief Step every phase hook for @p now_ms, in beat order. */
        void step(uint64_t now_ms) {
            for (uint8_t p = 0; p < (uint8_t)Tasks::PHASE_COUNT; p++)
                step(now_ms, (Tasks::Phase)p);
        }

        /** @brief Liveness query polled by reporting. Default true; a component
         *  overrides it where it has a real signal. */
        virtual bool healthy() const { return true; }

        /** @} */

    protected:
        /** @name Hooks (implemented by the component)
         *  @{ */



        /** @brief One-time init, run by setup(). Default: nothing. */
        virtual void on_setup(uint64_t now_ms) { (void)now_ms; }
        /** @name Per-phase work
         *
         *  One hook per phase; override the one whose place in the DATA FLOW
         *  matches the work (@ref dev_tasks). A component spanning phases
         *  overrides several and registers once per phase, so each half is
         *  separately attributable on the TASKS page. Default: nothing.
         *  @{ */
        virtual void on_input(uint64_t now_ms)   { (void)now_ms; }
        virtual void on_config(uint64_t now_ms)  { (void)now_ms; }
        virtual void on_service(uint64_t now_ms) { (void)now_ms; }
        virtual void on_control(uint64_t now_ms) { (void)now_ms; }
        virtual void on_output(uint64_t now_ms)  { (void)now_ms; }
        virtual void on_present(uint64_t now_ms) { (void)now_ms; }
        /** @} */
        /** @} */
    };

}

#endif // LIBBOTANIK_CORE_COMPONENT_H
