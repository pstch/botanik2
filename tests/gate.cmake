# The pre-push gate's run logic, driven by `make check`.
# ============================================================================
# Fast path: `-LE unit` runs the aggregate doctest entry plus the pyte files -
# every test exactly once, in the configuration that also catches a case
# corrupting state for the ones after it.
#
# Slow path, only on a doctest failure: re-run the per-case entries so each
# failing case is named as its own CTest result. It costs ~14 s against ~0.6 s,
# which is why it is not what the gate runs first.
#
# Driven with -DCTEST=<ctest> -DJOBS=<n>, from the build directory.

set(_run --output-on-failure -j ${JOBS})

execute_process(COMMAND ${CTEST} -LE unit ${_run} RESULT_VARIABLE _rc)
if(_rc EQUAL 0)
    return()
endif()

# Which half failed? Re-running the aggregate alone is under a second and says.
execute_process(COMMAND ${CTEST} -R "^doctest$"
                RESULT_VARIABLE _doctest_rc OUTPUT_QUIET ERROR_QUIET)

if(NOT _doctest_rc EQUAL 0)
    message("")
    message("=== doctest failed; re-running the cases one process each to name them ===")
    # Worth stating, because the result can look like a contradiction: a case
    # that only fails IN COMPANY - one leaving state behind for a later one, the
    # class a shared process exists to catch - passes when run on its own. So an
    # all-green run here does not overturn the failure above; it identifies it as
    # interference between cases rather than any single case being wrong.
    message("    An all-green run below means INTERFERENCE between cases, not a")
    message("    flake: run ./tests/botanik_tests directly to reproduce it.")
    message("")
    execute_process(COMMAND ${CTEST} -L unit ${_run})
endif()

message(FATAL_ERROR "gate failed")
