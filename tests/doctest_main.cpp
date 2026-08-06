/**
 * @file doctest_main.cpp
 * @brief Entry point for the doctest unit-test binary.
 *
 * This is the single translation unit that compiles doctest's implementation
 * and provides @c main(); every other test TU just includes @c doctest.h. Keep
 * it free of test cases so a change to a suite never forces a recompile of the
 * (large) framework implementation.
 *
 * @see @ref cpptests for the suite overview and how to run it.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"                  // DOCTEST_CONFIG_IMPLEMENT lives here - the framework's main()
