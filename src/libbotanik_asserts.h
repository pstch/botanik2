#ifndef LIBBOTANIK_ASSERTS_H
#define LIBBOTANIK_ASSERTS_H

/**
 * @file libbotanik_asserts.h
 * @brief BOTANIK_ASSERT - debug-only assertions that log, then trap or continue.
 *
 * In release builds (DEBUG undefined) the macro compiles out entirely. In debug
 * builds a failed assertion logs a CRITICAL entry under the ASSERT topic (when a
 * logger was registered via set_assert_logger), then:
 *   - traps into an attached debugger (gated on HAL::under_debugger(), because a
 *     bare breakpoint with no debugger HardFaults on the OPTA);
 *   - otherwise abort()s on the simulator (loud failure in a headless/CI run);
 *   - otherwise (hardware, no debugger) it has already logged, and continues.
 */

#include <cstdlib>   // abort

#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/logger.h"

namespace Botanik {

    /** @brief Logger that assertions report through. Function-local static keeps
     *  this header-only; set once at startup via set_assert_logger (may stay null). */
    inline Logs::Logger*& _assert_logger() {
        static Logs::Logger* logger = nullptr;
        return logger;
    }

    /** @brief Register the logger assertion failures are recorded through. */
    inline void set_assert_logger(Logs::Logger* logger) { _assert_logger() = logger; }

    /** @brief Assertion-failure handler (see BOTANIK_ASSERT). */
    inline void assert_fail(const char* expr, const char* file, int line) {
        Logs::Logger* logger = _assert_logger();
        if (logger != nullptr)
            logger->critical(PlcIO::ASSERT, "assertion failed: %s (%s:%d)", expr, file, line);
        if (HAL::under_debugger()) { HAL::debug_break(); return; }  // break for the debugger
#ifdef TARGET_SIMULATOR
        abort();                                                    // loud failure, no debugger
#endif
        // Hardware, no debugger: logged above; keep running rather than reboot.
    }

}

#ifdef DEBUG
    #define BOTANIK_ASSERT(cond) \
        do { if (!(cond)) ::Botanik::assert_fail(#cond, __FILE__, __LINE__); } while (0)
#else
    #define BOTANIK_ASSERT(cond) ((void)0)
#endif

#endif // LIBBOTANIK_ASSERTS_H
