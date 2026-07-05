// REVIEW_35: WIP
#ifndef LIBBOTANIK_CORE_COMPONENT_H
#define LIBBOTANIK_CORE_COMPONENT_H

/**
 * @file component.h
 * @brief Component - uniform lifecycle base for the long-lived main objects.
 *
 * A Component is stepped once per beat (by the main loop or by another
 * component). The non-virtual step() fixes the order: on the first step it runs
 * setup() then announce(); every step it dispatches on_config_changed() when the
 * watched config serial changes, then runs on_step(). Sites/subsystems customize
 * only through the protected hooks. healthy() is a common liveness query (default
 * true) a future dynamic scheduler can poll.
 *
 * Dependency-light on purpose: Logs::Logger is only forward-declared here (so
 * even Logger can derive Component); announce()'s body lives in component.cpp,
 * the one TU that pulls the full Logger + BOOT topic. In DEBUG builds announce()
 * logs one BOOT line the first time a component is stepped, confirming it reached
 * the loop (a constructed-but-never-stepped component stays silent); it compiles
 * to nothing with DEBUG undefined.
 */

#include <cstdint>

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

        /** @} */

    protected:
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

        /** @brief Advance one beat with the cycle timestamp. Non-virtual so the
         *  order can't be reordered: first step runs setup() then announce();
         *  then, if config_serial() changed from the applied value,
         *  on_config_changed(); then on_step(). */
        void step(uint64_t now_ms) {
            if (!_started) { _started = true; setup(now_ms); announce(); }
            uint32_t serial = config_serial();
            // Fire on any change from the applied serial. _applied_serial starts
            // at 0, so an unset block (serial 0) never fires at boot, but a
            // config later cleared back to 0 does (a real reconfiguration).
            if (serial != _applied_serial) {
                _applied_serial = serial;
                on_config_changed(now_ms);
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
        virtual uint32_t config_serial() const { return 0; }
        /** @brief Reset / latch the new config on a watched-serial change.
         *  Default: nothing. */
        virtual void on_config_changed(uint64_t now_ms) { (void)now_ms; }

        /** @} */
    };

}

#endif // LIBBOTANIK_CORE_COMPONENT_H
