// REVIEW_35: DONE
#ifndef UTILS_ASSERT_H
#define UTILS_ASSERT_H

/** @file assert.h
 *  @brief BOTANIK_ASSERT - debug-only assertions that log, then trap or
 *         continue.
 *
 *  This macro is compiled out entirely in release builds, it's only used when
 *  DEBUG is defined. It logs a CRITICAL entry under the ASSERT topic, then :
 *   - if there is a debugger, traps into the attached debugger
 *   - if we are on the simulator, abort()s on the simulator
 *   - otherwise, just keeps running. */

#include <cstdlib>                      // abort

#include "libbotanik/hal/platform.h"    // HAL::Debug::breakpoint / under_debugger
#include "libbotanik/logs/logger.h"     // Logs::Logger

// Assertion handling (and logging)
// ===========================================================================

namespace Botanik {

    /** @brief Get the global assertion logger. */
    inline Logs::Logger*& get_assert_logger() {
        static Logs::Logger* logger = nullptr;
        return logger;
    }

    /** @brief Set the global assertion logger.  */
    inline void set_assert_logger(Logs::Logger* logger) {
        get_assert_logger() = logger;
    }

    /** @brief Assertion-failure handler (see BOTANIK_ASSERT). */
    inline void assert_fail(const char* expr, const char* file, int line) {
        // Get the assertion logger
        Logs::Logger* logger = get_assert_logger();

        // Log a message if we have a logger
        if (logger != nullptr)
            logger->critical(Logs::ASSERT, "assertion failed:\n%s (%s:%d)",
                             expr, file, line);

        // Break for the debugger.
        if (HAL::Debug::under_debugger()) {
            HAL::Debug::breakpoint();
            return;
        }

        // If on the simulator, abort the program.
#ifdef TARGET_SIMULATOR
        abort();
#endif

        // If on hardware, keep running.
        return;
    }

}

// BOTANIK_ASSERT macro
// ===========================================================================

#ifdef DEBUG
    #define BOTANIK_ASSERT(cond) \
        do { if (!(cond)) ::Botanik::assert_fail(#cond, __FILE__, __LINE__); } while (0)
#else
    #define BOTANIK_ASSERT(cond) ((void)0)
#endif

#endif // UTILS_ASSERT_H
