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
 * changes, then runs on_step(). Sites/subsystems customize only through the
 * protected hooks. healthy() is a common liveness query (default true).
 *
 * @section component_rate Rate
 *
 * A component may run slower than the beat: set_period(ms), and step() sits out
 * the beats in between. The default period of 0 means every beat, so a component
 * that never sets one is unaffected. The loop keeps calling step() every beat
 * either way - the fixed loop ORDER in core/firmware.h is load-bearing and stays
 * written out as code; only the RATE moves into the component.
 *
 * Anything a hook does that is per-unit-time must therefore read dt_ms() rather
 * than assume the beat period, or it silently rescales itself when its period
 * changes. See docs/dev/scheduler.md for what each component's rate should be
 * and what sets it.
 *
 * Dependency-light on purpose: Logs::Logger is only forward-declared here (so
 * even Logger can derive Component); announce()'s body lives in component.cpp,
 * the one TU that pulls the full Logger + BOOT topic. In DEBUG builds announce()
 * logs one BOOT line the first time a component is stepped, confirming it reached
 * the loop (a constructed-but-never-stepped component stays silent); it compiles
 * to nothing with DEBUG undefined.
 */

#include <cstdint>

#include "../scheduler/task_timer.h"

// Forward-declared, NOT included: keeps Component free of the Logger include so
// Logger itself can derive it (the body of announce() lives in component.cpp).
namespace Botanik { namespace Logs { class Logger; } }

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
         *  @brief What makes a component multi-rate. The loop still calls step()
         *  every beat; the component decides whether it is due, so the fixed
         *  loop ORDER (which is load-bearing - see core/firmware.h) stays
         *  written out as code and only the RATE becomes a property of the
         *  component.
         *  @{ */

        /** @brief Run interval in ms; 0 (the default) means every beat, so a
         *  component that never calls set_period() behaves exactly as before. */
        uint32_t             _period_ms = 0;
        Scheduler::TaskTimer _timer;              ///< the due/late/dropped rule
        uint32_t             _dt_ms  = 0;         ///< real ms since the last run
        uint16_t             _missed = 0;         ///< activations dropped

        /** @} */

    protected:
        /** @brief The group serial step() last acted on. Resolving it costs a
         *  linear name scan of the parameter table, and step() already paid for
         *  it this beat - so a hook asking "is my group configured?" reads this
         *  instead of scanning again. */
        uint32_t applied_serial() const { return _applied_serial; }

        /** @brief ACTUAL ms since this component last ran, never its nominal
         *  period. An accessor rather than an on_step() argument for the same
         *  reason applied_serial() is one: the hook that wants it reads it, and
         *  every component that does not stays untouched.
         *
         *  Anything holding a per-tick budget or integrating over time must use
         *  this rather than assuming the beat, or it silently rescales itself
         *  the moment its period moves. */
        uint32_t dt_ms() const { return _dt_ms; }
        /** @brief Activations dropped since the last run; 0 normally. Late runs
         *  happen ONCE - see Scheduler::TaskTimer. */
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
         *
         *  Without it every period is phase-locked to boot (everything first
         *  runs on the same beat and seeds from it), so a rate cuts the mean
         *  work per beat and leaves the PEAK untouched - and the peak is what
         *  decides the shortest beat the loop can hold. Set it beside the
         *  period, before the loop starts; keep it under the period. */
        void set_offset(uint32_t offset_ms) { _timer.set_offset(offset_ms); }

        /** @brief Advance one beat with the cycle timestamp. Non-virtual so the
         *  order can't be reordered: due check first, then on the first step
         *  setup() and announce(); then, if param_serial() changed from the
         *  applied value, on_param_changed(); then on_step().
         *
         *  The due check gates the whole body, so a component with a period does
         *  not poll its config serial on the beats it sits out either. It never
         *  gates the FIRST step: TaskTimer is always due on its first call, so
         *  setup() still runs on the first beat whatever the period. */
        void step(uint64_t now_ms) {
            if (!_timer.due(now_ms, _period_ms, _dt_ms, _missed)) return;
            _timer.ran(now_ms);
            if (!_started) { _started = true; setup(now_ms); announce(); }
            uint32_t serial = param_serial();
            // Fire on any change from the applied serial. _applied_serial starts
            // at 0, so an unset block (serial 0) never fires at boot, but a
            // config later cleared back to 0 does (a real reconfiguration).
            if (serial != _applied_serial) {
                _applied_serial = serial;
                on_param_changed(now_ms);
            }
            on_step(now_ms);
        }

        /** @brief Liveness query polled by reporting / a future scheduler.
         *  Default true; a component overrides it where it has a real signal. */
        virtual bool healthy() const { return true; }

        /** @} */

    protected:
        /** @name Hooks (implemented by the component)
         *  @{ */

        /** @brief One-shot BOOT/DEBUG announce, run once from step()'s first-call
         *  block (out-of-line: see component.cpp). Compiled out (bar the flag)
         *  when DEBUG is undefined. */
        void announce();

        /** @brief One-time init on the first step(), with the cycle timestamp.
         *  Default: nothing. */
        virtual void setup(uint64_t now_ms) { (void)now_ms; }
        /** @brief Advance the component's state one tick. Default: nothing. */
        virtual void on_step(uint64_t now_ms) { (void)now_ms; }
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
