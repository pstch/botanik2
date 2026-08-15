/**
 * @file doctest_main.cpp
 * @brief Entry point for the doctest unit-test binary.
 *
 * The single TU that compiles doctest's implementation and provides @c main().
 * Keep it free of test cases: a change to a suite would otherwise recompile the
 * whole framework implementation.
 *
 * @see @ref cpptests for the suite overview and how to run it.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include <cstdlib>

int main(int argc, char** argv) {
    // The suite builds schedulers it never kicks, and an armed watchdog would
    // re-exec the test binary out from under doctest. The sim's own default is
    // ON: a running unit wants the real thing.
    setenv("BOTANIK_WATCHDOG", "0", /*overwrite=*/0);
    return doctest::Context(argc, argv).run();
}
