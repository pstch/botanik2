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
 *  @section assert_deps Why this header includes nothing
 *
 *  An assertion is the one thing any file may want, so this header must cost
 *  its includers nothing (<cstdint> aside).
 *  Logs::Logger is only forward-declared and
 *  assert_fail()'s body lives in assert.cpp, the one TU that pulls the full
 *  Logger and the HAL (the arrangement core/component.h uses). Naming logger.h
 *  here would drag the logging subsystem, and through it core/ and tasks/, into
 *  every file that asserts; hal/ sits below the logger and cannot afford even
 *  this, so it falls back to plain assert(). */

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
