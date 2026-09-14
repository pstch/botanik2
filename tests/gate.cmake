# The pre-push gate's run logic, driven by `make check`.
# ============================================================================
# `-LE unit` runs the whole-binary entries - the library's doctest binary, each
# example:<name> - plus the pyte files: every test once, in the one-process
# configuration that catches a case corrupting state for the ones after it.
#
# Only for a binary that failed does it re-run that binary's per-case entries
# (label unit:<binary>), each in a fresh process. Not for the NAMES - doctest
# already prints the failing case with its file and line - but to tell a case
# that fails on its own from one that only fails in company.
#
# Driven with -DCTEST=<ctest> -DJOBS=<n>, from the build directory.

set(_run --output-on-failure -j ${JOBS})

execute_process(COMMAND ${CTEST} -LE unit ${_run} RESULT_VARIABLE _rc)
if(_rc EQUAL 0)
    return()
endif()

# CTest's own record of what just failed, one `<index>:<name>` per line.
set(_binaries)
if(EXISTS Testing/Temporary/LastTestsFailed.log)
    file(STRINGS Testing/Temporary/LastTestsFailed.log _failed)
    foreach(_line IN LISTS _failed)
        string(REGEX REPLACE "^[0-9]+:" "" _name "${_line}")
        if(_name STREQUAL "doctest")
            list(APPEND _binaries doctest)
        elseif(_name MATCHES "^example:([^:]+)$")
            list(APPEND _binaries ${CMAKE_MATCH_1})
        endif()
    endforeach()
endif()

foreach(_bin IN LISTS _binaries)
    execute_process(COMMAND ${CTEST} -N -L "^unit:${_bin}$"
                    OUTPUT_VARIABLE _listing ERROR_QUIET)
    if(NOT _listing MATCHES "Total Tests: [1-9]")
        continue()
    endif()
    if(_bin STREQUAL "doctest")
        set(_reproduce "./tests/botanik_tests")
    else()
        set(_reproduce "the ${_bin}_tests binary under examples/${_bin}/")
    endif()
    message("")
    message("=== ${_bin} failed; re-running its cases one process each ===")
    # A case that only fails in company passes on its own, so an all-green run
    # below does not overturn the failure above.
    message("    An all-green run below usually means INTERFERENCE between cases,")
    message("    not a flake: run ${_reproduce} directly to reproduce it.")
    if(_bin STREQUAL "doctest")
        message("    The exception is the socket-backed cases (test_modbus_server,")
        message("    test_modbus_master): those bind a PID-derived 127/8 address, so")
        message("    a recycled PID can meet a previous run's lingering sockets. If")
        message("    ONLY those failed and nothing else did, re-run before digging.")
    endif()
    message("")
    execute_process(COMMAND ${CTEST} -L "^unit:${_bin}$" ${_run})
endforeach()

message(FATAL_ERROR "gate failed")
