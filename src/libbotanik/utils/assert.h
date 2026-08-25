// REVIEW_35: DONE
#ifndef UTILS_ASSERT_H
#define UTILS_ASSERT_H

#include <cstdint>

/** @file assert.h
 *  @brief BOTANIK_ASSERT - debug-only assertions that log, then trap or
 *         continue.
 *
 *  This macro is compiled out entirely in release builds, it's only used when
 *  DEBUG is defined. It counts the failure, logs a CRITICAL entry under the
 *  ASSERT topic, traps if a debugger is attached, and keeps running - the same
 *  on the simulator as on hardware. The CRITICAL entry is the whole report.
 *
 *  @note This header costs its includers nothing but <cstdint>: Logs::Logger is
 *  forward-declared and assert_fail()'s body is in assert.cpp. Naming logger.h
 *  here would drag core/ and tasks/ into every file that asserts. */

namespace Botanik {

    namespace Logs { class Logger; }

    /** @brief Route assertion failures to @p logger (null disables logging).
     *  Called once during firmware setup. */
    void set_assert_logger(Logs::Logger* logger);

    /** @brief Assertion-failure handler (see BOTANIK_ASSERT). */
    void assert_fail(const char* expr, const char* file, int line);

}

// BOTANIK_ASSERT macro

#ifdef DEBUG
    #define BOTANIK_ASSERT(cond) \
        do { if (!(cond)) ::Botanik::assert_fail(#cond, __FILE__, __LINE__); } while (0)
#else
    #define BOTANIK_ASSERT(cond) ((void)0)
#endif

#endif // UTILS_ASSERT_H
