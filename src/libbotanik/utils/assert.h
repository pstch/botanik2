// REVIEW_35: DONE
#ifndef UTILS_ASSERT_H
#define UTILS_ASSERT_H

#include <cstdint>                      // uint16_t

/** @file assert.h
 *  @brief BOTANIK_ASSERT - debug-only assertions that log, then trap or
 *         continue.
 *
 *  This macro is compiled out entirely in release builds, it's only used when
 *  DEBUG is defined. It logs a CRITICAL entry under the ASSERT topic, then :
 *   - if there is a debugger, traps into the attached debugger
 *   - if we are on the simulator, abort()s on the simulator
 *   - otherwise, just keeps running.
 *
 *  @section assert_deps Why this header includes nothing
 *
 *  An assertion is the one thing any file may want, so this header must cost
 *  its includers nothing (<cstdint> aside, for the expected-failure counter).
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

    /** @brief The logger assertion failures are routed to, or null. */
    Logs::Logger* get_assert_logger();

    /** @brief Assertion-failure handler (see BOTANIK_ASSERT). */
    void assert_fail(const char* expr, const char* file, int line);

    /** @brief While one of these is alive, an assertion failure is COUNTED
     *  rather than fatal.
     *
     *  For a test that drives a refusal on purpose. A refused registration has
     *  two halves and both are the contract: it refuses gracefully - returns
     *  false, counts, keeps running, so the field gets a degraded unit and not a
     *  dead one - and it asserts, so a sim or a test run cannot walk past it.
     *  Testing the first half means suspending the second, and saying so.
     *
     *  Nests, so a fixture holding one does not disarm a case's own. */
    class ExpectedAsserts {
        uint16_t _at_entry;
    public:
        ExpectedAsserts();
        ~ExpectedAsserts();
        /** @brief How many fired since this object was made. */
        uint16_t count() const;
    };

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
