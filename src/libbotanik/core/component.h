// REVIEW_35: WIP
#ifndef LIBBOTANIK_CORE_COMPONENT_H
#define LIBBOTANIK_CORE_COMPONENT_H

/**
 * @file component.h
 * @brief Component - uniform lifecycle base for the long-lived main objects.
 *
 * Work reaches the beat by REGISTRATION: register_tasks() hands Tasks::Registry
 * one entry per point the component acts at, each naming its own phase and rate
 * group; registering also runs setup(). The registry calls poll_config() before
 * every handler, and the handler dispatches the phase hook. Subsystems customize
 * through the protected hooks; healthy() is a liveness query (default true).
 *
 * @section component_rate Rate
 *
 * A component has no rate of its own: how often work runs belongs to its TASK,
 * and the elapsed time is Tasks::Tick::dt_ms.
 *
 * In DEBUG builds announce() logs one BOOT line on first setup().
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
        /** @name Config-serial dispatch
         *  @{ */

        /** @brief Last serial acted on (0 = none yet, matches an unset config
         *  block, so a never-configured block never triggers a change). */
        uint32_t      _applied_serial = 0;

        /** @brief Beat poll_config() last ran for, so a component registered in
         *  several phases re-reads its group once per beat. UINT64_MAX = none
         *  yet, a value uptime_ms() cannot reach. */
        uint64_t      _beat_ms = UINT64_MAX;

        /** @} */

    protected:
        /** @brief Group serial poll_config() last acted on. A hook asking "is my
         *  group configured?" reads this rather than paying the name scan
         *  again. */
        uint32_t applied_serial() const { return _applied_serial; }

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
         *  announce().
         *
         *  **Idempotent**: the task registry calls it for every registered task
         *  during BotanikFirmware::setup(), and step() calls it for a component
         *  the registry does not own (one stepped by another component, e.g. the
         *  network stack's children).
         *
         *  @note Only call it once the log sinks are registered. announce(), and
         *  anything setup() logs (HealthMonitor's POST), goes nowhere before
         *  that. */
        void setup(uint64_t now_ms) {
            if (_started) return;
            _started = true;
            on_setup(now_ms);
            announce();
        }
        /** @brief Whether setup() has run. */
        bool started() const { return _started; }

        /** @brief Put this component's work in the beat: one entry per point
         *  it acts at, each with its own name, rate group and phase.
         *
         *  No argument - the table came with the SetupContext. Default: none.
         *  @see design/scan-cycle-rework.md */
        virtual void register_tasks() {}

        /** @brief Poll the watched config serial and dispatch on_param_changed()
         *  when it has moved.
         *
         *  Once per BEAT, not per task: a component registered in three phases
         *  must not re-read its group three times, and `_beat_ms` is the same
         *  guard step() uses, so the two ways in agree.
         *
         *  The registry calls this before every handler. It has to: a
         *  registration dispatches the phase hook DIRECTLY, so nothing that
         *  step() used to do around that hook happens by itself. That is how
         *  every config-driven component silently stopped reconfiguring - the
         *  network handler sat in NET_IDLE forever waiting for a callback
         *  nothing made. */
        void poll_config(uint64_t now_ms) {
            if (now_ms == _beat_ms) return;
            _beat_ms = now_ms;
            const uint32_t serial = param_serial();
            // _applied_serial starts at 0, so an unset block (serial 0) never
            // fires at boot; a config later cleared back to 0 does, being a real
            // reconfiguration.
            if (serial == _applied_serial) return;
            _applied_serial = serial;
            on_param_changed(now_ms);
        }

        /** @brief Run @p phase's hook with the lifecycle the registry gives a
         *  registered task: setup(), poll_config(), then the hook.
         *
         *  For a component the task table does not own - one stepped by its
         *  owner (the network stack's children, the sim's Modbus check) or by a
         *  test. No due check: the rate belongs to the task.
         *
         *  Keep equal to Tasks::Registry::run_phase()'s per-task body; when the
         *  two drifted, every config-driven component stopped reconfiguring. */
        void step(uint64_t now_ms, Tasks::Phase phase) {
            setup(now_ms);          // idempotent: a no-op once we are set up
            poll_config(now_ms);
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

        /** @brief Step every phase hook for @p now_ms, in beat order.
         *
         *  Resets the beat guard first: each call is a fresh beat by definition,
         *  and without that two steps inside one millisecond would take the
         *  second for the same beat and never fire on_param_changed(). */
        void step(uint64_t now_ms) {
            _beat_ms = UINT64_MAX;
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

        /** @brief One-shot BOOT/DEBUG announce, run by setup() (out-of-line: see
         *  component.cpp). Compiled out when DEBUG is undefined. */
        void announce();

        /** @brief One-time init, run by setup(). Default: nothing. */
        virtual void on_setup(uint64_t now_ms) { (void)now_ms; }
        /** @name Per-phase work
         *
         *  One hook per phase of the beat; override the one whose place in the
         *  DATA FLOW matches the work (@ref dev_tasks). A component that spans
         *  phases overrides several - the serial pumps read at PHASE_INPUT and
         *  write at PHASE_PRESENT - and is registered once per phase, so each
         *  half is separately attributable on the TASKS page.
         *
         *  Default: nothing.
         *  @{ */
        virtual void on_input(uint64_t now_ms)   { (void)now_ms; }
        virtual void on_config(uint64_t now_ms)  { (void)now_ms; }
        virtual void on_service(uint64_t now_ms) { (void)now_ms; }
        virtual void on_control(uint64_t now_ms) { (void)now_ms; }
        virtual void on_output(uint64_t now_ms)  { (void)now_ms; }
        virtual void on_present(uint64_t now_ms) { (void)now_ms; }
        /** @} */
        /** @brief Serial of the watched config block (0 = none / not
         *  config-driven). Default: 0. */
        virtual uint32_t param_serial() const { return 0; }
        /** @brief Reset / latch the new config on a watched-serial change.
         *  Default: nothing. */
        virtual void on_param_changed(uint64_t now_ms) { (void)now_ms; }

        /** @} */
    };

}

#endif // LIBBOTANIK_CORE_COMPONENT_H
