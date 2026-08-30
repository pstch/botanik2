/**
 * @file st_config.cpp
 * @brief HAND-WRITTEN - compiles matiec's CONFIGURATION translation unit.
 *
 * Its own TU because that is how matiec lays a configuration out: one file
 * per resource plus this one, each pulling POUS.c for itself. What we want
 * from it is `config_init__`, `config_run__` and `common_ticktime__` - the
 * base period matiec computed from the TASK intervals, which is the number
 * StRuntime registers at.
 */

#include "st_bank.h"
#include "resolve_ci.h"
#include "accessor.h"
/* Generated C, and it says so at the top of every file. Silenced HERE rather
 * than in CMake: a per-source flag can land before a target's own options and
 * be undone by them, where a pragma cannot. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wshadow"
#include "generated/gh_mlb41.c"
#pragma GCC diagnostic pop
