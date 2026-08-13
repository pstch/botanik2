// REVIEW_35: WIP
#ifndef LIBBOTANIK_CORE_COMPONENT_H
#define LIBBOTANIK_CORE_COMPONENT_H

/**
 * @file component.h
 * @brief Component - uniform lifecycle base for the long-lived main objects.
 *
 * A Component is stepped once per beat (by the main loop or by another
 * component). The non-virtual step() fixes the order: it checks whether the
 * component is due, then on the first step runs setup() then announce(); every
 * due step it dispatches on_param_changed() when the watched config serial
 * changes, then runs the phase hook. Sites/subsystems customize only through the
 * protected hooks. healthy() is a common liveness query (default true).
 *
 * @section component_rate Rate
 *
 * A component may run slower than the beat: set_period(ms), and step() sits out
 * the beats in between (0, the default, means every beat). The loop calls step()
 * every beat either way - the fixed loop ORDER in firmware/firmware.h is
 * load-bearing; only the RATE lives here.
 *
 * Anything a hook does that is per-unit-time must therefore read dt_ms() rather
 * than assume the beat period, or it silently rescales itself when its period
 * changes. See docs/dev/scheduler.md for what sets each component's rate.
 *
 * In DEBUG builds announce() logs one BOOT line on a component's first step, so
 * a constructed-but-never-stepped component stays silent; without DEBUG it
 * compiles to nothing.
 */

#include <cstdint>                              // uint16_t/uint32_t/uint64_t/UINT64_MAX

#include "libbotanik/tasks/phase.h"         // Tasks::Phase
#include "libbotanik/tasks/task_timer.h"    // Tasks::TaskTimer

// Forward-declared, NOT included: keeps Component free of the Logger include so
// Logger itself can derive it (the body of announce() lives in component.cpp).
namespace Botanik { namespace Logs { class Logger; } }
// Forward-declared, not included: a component names the registry to hand it
// its tasks, and must not acquire the whole of tasks/ to do so.
namespace Botanik { namespace Tasks { class Registry; } }

namespace Botanik {

    /** @brief Uniform step()/config/health lifecycle base (see file header). */
    class Component {
        /** @name Config-serial dispatch
         *  @{ */

        /** @brief Last serial acted on (0 = none yet, matches an unset config
         *  block, so a never-configured block never triggers a change). */
        uint32_t      _applied_serial = 0;

        /** @}
         *  @name Rate
         *  @brief What makes a component multi-rate. @see component_rate
         *  @{ */

        /** @brief Run interval in ms; 0 (the default) means every beat. */
        uint32_t             _period_ms = 0;
        Tasks::TaskTimer _timer;              ///< the due/late/dropped rule
        uint32_t             _dt_ms  = 0;         ///< real ms since the last run
        uint16_t             _missed = 0;         ///< activations dropped

        /** @name This beat's verdict
         *
         *  The due check is per BEAT, not per phase: a component registered in
         *  two phases would otherwise consume its activation at the first and
         *  sit out the second. @see step()
         *  @{ */
        /** @brief The beat the verdict is for. UINT64_MAX means "none yet", a
         *  value uptime_ms() cannot reach, so no separate validity flag. */
        uint64_t             _beat_ms   = UINT64_MAX;
        bool                 _beat_due  = false;  ///< run the phase hooks this beat
        /** @} */

        /** @} */

    protected:
        /** @brief The group serial step() last acted on. Resolving it costs a
         *  linear name scan of the parameter table and step() already paid for
         *  it this beat, so a hook asking "is my group configured?" reads this
         *  instead of scanning again. */
        uint32_t applied_serial() const { return _applied_serial; }

        /** @brief ACTUAL ms since this component last ran, never its nominal
         *  period. Anything holding a per-tick budget or integrating over time
         *  must use this rather than assuming the beat, or it silently rescales
         *  itself the moment its period moves. */
        uint32_t dt_ms() const { return _dt_ms; }
        /** @brief Activations dropped since the last run; 0 normally. Late runs
         *  happen ONCE - see Tasks::TaskTimer. */
        uint16_t missed() const { return _missed; }

        /** @name Wiring (accessible to the derived component)
         *  @{ */

        Logs::Logger* _logger;            ///< announce + derived logging (may be null)
        const char*   _name;              ///< announce label
        bool          _started = false;   ///< first-step latch (setup + announce)

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        Component(Logs::Logger* logger, const char* name)
            : _logger(logger), _name(name) {}
        virtual ~Component() {}

        /** @brief The announce label, for anything naming this component to a
         *  human. Borrowed; every constructor takes one. */
        const char* name() const { return _name; }

        /** @brief Set the run interval; 0 means every beat. Call it during
         *  wiring, before the loop starts.
         *  @note A period should be an integer multiple of the beat, or the
         *  component fires at the next beat boundary past it (a 100 ms period on
         *  a 30 ms beat runs every 120 ms). */
        void set_period(uint32_t period_ms) { _period_ms = period_ms; }
        /** @brief The configured run interval; 0 = every beat. */
        uint32_t period_ms() const { return _period_ms; }

        /** @brief Offset this component from others sharing its period, so the
         *  periodic work spreads across beats instead of piling onto one.
         *  Set it beside the period, before the loop starts; keep it under the
         *  period. @see timer_offset */
        void set_offset(uint32_t offset_ms) { _timer.set_offset(offset_ms); }

        /** @brief Treat this component as having just run at @p now_ms without
         *  running it, so its next activation is a full period away rather than
         *  immediate. For a gap the caller deliberately did not step it across:
         *  the scan cycle does this to every control program when control
         *  authority returns, since the whole outage as dt fires every timer a
         *  program holds. Does not count as a run - see Tasks::TaskTimer. */
        void reseed(uint64_t now_ms) { _timer.reseed(now_ms); }

        /** @brief Run this component's one-time init: on_setup() then
         *  announce().
         *
         *  **Idempotent**, which is what lets the two ways in coexist: the task
         *  registry calls it for every registered task during
         *  BotanikFirmware::setup(), and step() still calls it lazily for a
         *  component the registry does not own (one stepped by another
         *  component, e.g. the network stack's children).
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

        /** @brief Hand the registry this component's tasks: one per point in the
         *  beat it acts at, each with its own name, rate group and phase.
         *  Default: none. @see design/scan-cycle-rework.md */
        virtual void register_tasks(Tasks::Registry&) {}

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

        /** @brief Advance through a WHOLE beat: every phase hook, in phase order.
         *
         *  For a component the task table does not own - one stepped by another
         *  component, or by a test - where there is no single phase to name. A
         *  single-phase component runs its one hook and five no-ops; one that
         *  spans phases runs each half in beat order, which is what stepping it
         *  "for this beat" means.
         *
         *  This IS a fresh beat by definition, so it re-runs the lifecycle even
         *  when the clock has not moved since the last call. Without that, two
         *  steps inside one millisecond - a test changing a parameter and
         *  stepping again, an owner stepping its children twice - would take the
         *  second for the same beat and never fire on_param_changed(). */
        void step(uint64_t now_ms) {
            _beat_ms = UINT64_MAX;
            for (uint8_t p = 0; p < (uint8_t)Tasks::PHASE_COUNT; p++)
                step(now_ms, (Tasks::Phase)p);
        }

        /** @brief Advance one beat, in @p phase. Non-virtual so the order can't
         *  be reordered: due check first, then on the first step setup() and
         *  announce(); then, if param_serial() changed from the applied value,
         *  on_param_changed(); then the hook for @p phase.
         *
         *  The due check gates the whole body, so a component with a period does
         *  not poll its config serial on the beats it sits out either. It never
         *  gates the FIRST step: TaskTimer is always due on its first call, so
         *  setup() runs on the first beat whatever the period.
         *
         *  **A component may run in more than one phase**, and the lifecycle
         *  above is per BEAT, not per phase: the serial pumps take their bytes in
         *  at PHASE_INPUT and put them out at PHASE_PRESENT, and a due check run
         *  twice would consume the activation at the first and skip the second.
         *  So the first dispatch of a beat decides, and the rest of that beat
         *  reuses the verdict. */
        void step(uint64_t now_ms, Tasks::Phase phase) {
            if (now_ms != _beat_ms) {
                _beat_ms  = now_ms;
                _beat_due = _timer.due(now_ms, _period_ms, _dt_ms, _missed);
                if (_beat_due) {
                    _timer.ran(now_ms);
                    setup(now_ms);   // idempotent: a no-op once the registry set us up
                    uint32_t serial = param_serial();
                    // _applied_serial starts at 0, so an unset block (serial 0)
                    // never fires at boot; a config later cleared back to 0 does,
                    // being a real reconfiguration.
                    if (serial != _applied_serial) {
                        _applied_serial = serial;
                        on_param_changed(now_ms);
                    }
                }
            }
            if (!_beat_due) return;
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
