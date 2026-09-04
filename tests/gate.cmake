# The pre-push gate's run logic, driven by `make check`.
# ============================================================================
# Fast path: `-LE unit` runs the aggregate doctest entry plus the pyte files -
# every test once, in the configuration that catches a case corrupting state for
# the ones after it. Slow path, only on a doctest failure: re-run the per-case
# entries (~14 s against ~0.6 s) so each failing case is named on its own.
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
    # A case that only fails IN COMPANY passes on its own, so an all-green run
    # below does not overturn the failure above.
    message("    An all-green run below usually means INTERFERENCE between cases,")
    message("    not a flake: run ./tests/botanik_tests directly to reproduce it.")
    message("    The exception is the socket-backed cases (test_modbus_server,")
    message("    test_modbus_master): those bind a PID-derived 127/8 address, so")
    message("    a recycled PID can meet a previous run's lingering sockets. If")
    message("    ONLY those failed and nothing else did, re-run before digging.")
    message("")
    execute_process(COMMAND ${CTEST} -L unit ${_run})
endif()

message(FATAL_ERROR "gate failed")
