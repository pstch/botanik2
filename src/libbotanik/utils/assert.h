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
 *  @note This header costs its includers nothing but <cstdint>, and
 *  assert.cpp costs itself nothing either: the report goes out through a
 *  function pointer, so the lowest utility in the tree names no logger. It is
 *  logs/ that knows how to write one - see Logs::route_asserts(). */

namespace Botanik {

    /** @brief Where a failed assertion is reported. Installed once at bring-up;
     *  null until then, and a failure before that is counted and dropped. */
    using AssertSink = void (*)(const char* expr, const char* file, int line);

    /** @brief Route assertion failures to @p sink (null disables reporting). */
    void set_assert_sink(AssertSink sink);

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
