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
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"    // DOCTEST_CONFIG_IMPLEMENT lives here
