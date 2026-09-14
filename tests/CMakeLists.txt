# Unit tests (doctest) - host-only, SIMULATOR build.
# ============================================================================
# Added only from the SIMULATOR branch of the top-level CMakeLists. doctest is a
# single vendored header (tests/vendor/doctest.h).

add_executable(botanik_tests
    doctest_main.cpp
    test_alarms.cpp
    test_console.cpp
    test_console_units.cpp
    test_editor.cpp
    test_editor_wire.cpp
    test_editor_fuzz.cpp
    test_shell_wire.cpp
    test_modbus.cpp
    test_modbus_framing.cpp
    test_modbus_tcp.cpp
    test_modbus_plan.cpp
    test_modbus_map.cpp
    test_modbus_image.cpp
    test_modbus_master.cpp
    test_modbus_server.cpp
    test_io_local.cpp
    test_hal_net.cpp
    test_resolver.cpp
    test_mqtt.cpp
    test_ntp.cpp
    test_pages.cpp
    test_shell.cpp
    test_shell_report.cpp
    test_sun.cpp
)

# vendor/ for doctest.h; src/ for the library and the simulator's site payloads.
target_include_directories(botanik_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${BOTANIK_ROOT}/src
)

# The suite compiles the selected site's header, so it links the site too: the
# IO value stores live in it.
target_link_libraries(botanik_tests botanik_site)

# The committed goldens. Only test_alarms.cpp reads them, so this stays a
# test-target define.
target_compile_definitions(botanik_tests PRIVATE
    BOTANIK_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

# The doctest binary is registered twice; the label tells them apart:
#
#   doctest      - every case in one process. Unlabelled, so the gate runs it.
#   <case name>  - one entry per TEST_CASE, labelled `unit;unit:doctest`, so
#                  `ctest -R` and an IDE test panel can address a single case.
#
# The gate excludes `unit`: one process is the configuration that catches a case
# corrupting state for the ones after it. A bare `ctest` runs both.
add_test(NAME doctest COMMAND botanik_tests)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
include(doctest)
doctest_discover_tests(botanik_tests)
# `unit;unit:doctest`, set over the discovered list: TEST_PROPERTIES flattens a
# two-label list. unit:<binary> is what the gate re-runs when that binary fails.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/doctest-labels.cmake
    "if(DEFINED botanik_tests_TESTS)\n"
    "  set_tests_properties(\${botanik_tests_TESTS} PROPERTIES LABELS \"unit;unit:doctest\")\n"
    "endif()\n")
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
             ${CMAKE_CURRENT_BINARY_DIR}/doctest-labels.cmake)


# `make check` is the pre-push gate. Its DEPENDS build what the suite needs
# (`make test` and `ctest` build nothing), then it runs the suite.
#
# The warning gate is the build itself: the library compiles -Werror
# (BOTANIK_WERROR), so building botanik_tests fails on any library warning.
# Sanitizers and clang-tidy are sweeps; see `check-full`.
#
# -j defaults to the host core count (override with -DBOTANIK_CTEST_JOBS=N).
# That is the cap because each pyte sim wants a core to itself.
set(BOTANIK_CTEST_JOBS ${_nproc} CACHE STRING
    "Parallel job count for the ctest gate (make check)")
# The run itself is gate.cmake: `-LE unit` first, then the per-case entries of
# each binary that failed, which says whether a case fails alone or only in
# company.
# Excluding by label rather than naming what to run means a newly added test
# joins the gate by default.
add_custom_target(check
    COMMAND ${CMAKE_COMMAND} -DCTEST=${CMAKE_CTEST_COMMAND}
            -DJOBS=${BOTANIK_CTEST_JOBS}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/gate.cmake
    DEPENDS botanik_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Pre-push gate: warning-clean build + unit tests"
    VERBATIM
)

# Every example, built as its own consumer project and joined to this tree's
# CTest. See cmake/examples.cmake. The gate builds them before it runs.
include(${BOTANIK_ROOT}/cmake/examples.cmake)
add_dependencies(check examples)

# `make check-alloff` is the library with every optional feature off, the only
# build that compiles the file-level guards. No site: a nested project, since a
# site and its features are chosen once per build.
add_custom_target(check-alloff
    COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${BOTANIK_ROOT}
            -B ${CMAKE_BINARY_DIR}/alloff -DBOTANIK_TARGET=SIMULATOR
            -DBOTANIK_SITE=NONE
            -DBOTANIK_FEATURE_CONSOLE=OFF -DBOTANIK_FEATURE_LOGGING=OFF
            -DBOTANIK_FEATURE_PLC=OFF -DBOTANIK_FEATURE_HOME_PAGE=OFF
            -DBOTANIK_FEATURE_SHELL_PAGE=OFF
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/alloff
            --target botanik --parallel ${_nproc}
    USES_TERMINAL
    COMMENT "Sweep: the library with every optional feature off (build/alloff/)"
    VERBATIM
)

# `make check-full` is the periodic sweep: the checks below as DEPENDS, then the
# whole gate again under ASan+UBSan in its own tree under build/asan/. Too slow
# to pay per edit; run it before a push that touches memory handling. It drives
# the sub-tree's own `check`, so it cannot drift from this gate.
add_custom_target(check-full
    COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${BOTANIK_ROOT}
            -B ${CMAKE_BINARY_DIR}/asan -DBOTANIK_TARGET=SIMULATOR
            -DCMAKE_BUILD_TYPE=DEBUG -DBOTANIK_SANITIZE=address+undefined
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/asan
            --target check --parallel ${_nproc}
    USES_TERMINAL
    COMMENT "Sweep: clang-tidy + the full gate under ASan+UBSan (build/asan/)"
    VERBATIM
)
# clang-tidy is a DEPENDS so a finding stops the sweep before the instrumented
# rebuild. The target only exists when it was found and the generator is Make.
if(TARGET clang-tidy)
    add_dependencies(check-full clang-tidy)
endif()

# check-generated is declared by cmake/codegen.cmake, beside the generator.
if(TARGET check-generated)
    add_dependencies(check-full check-generated)
endif()


# The cross build, because a link error is invisible to every host target:
# Arduino.h declares the whole wiring API and libmbed.a defines none of it, so a
# HAL reaching for a call the build does not compile fails at link time on the
# OPTA alone, with `make check` green throughout. Skipped where arm-none-eabi
# and the Mbed core are not installed.
find_program(_arm_gxx arm-none-eabi-g++)
if(_arm_gxx)
    # The SITE is passed: this tree persists between runs, and a cache left by
    # an earlier configuration would otherwise cross-build a different site
    # from the one the gate just tested, silently.
    add_custom_target(check-opta
        COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${BOTANIK_ROOT}
                -B ${BOTANIK_ROOT}/build-opta -DBOTANIK_TARGET=OPTA
                -DBOTANIK_SITE=${BOTANIK_SITE} -DBOTANIK_SITE_DIR=${BOTANIK_SITE_DIR}
        COMMAND ${CMAKE_COMMAND} --build ${BOTANIK_ROOT}/build-opta
                --parallel ${_nproc}
        USES_TERMINAL
        COMMENT "Sweep: the OPTA cross build, which is where a link error lives"
        VERBATIM)
    add_dependencies(check-full check-opta)
else()
    message(STATUS "check-full: no arm-none-eabi-g++, skipping the OPTA cross build")
endif()

add_dependencies(check-full check-alloff)

# `make coverage` builds a third tree, instrumented for gcov, runs both suites
# against it and prints the per-area line coverage; build/coverage.json is the
# machine-readable half. It reports and never fails, since what it is for is
# noticing a fall.
#
# Both suites: the doctest binary never exercises network/, and the pyte screen
# tests are the only thing driving the shell over a real PTY.
#
# Its own tree, like check-alloff's: --coverage changes every object, and folding
# it into build/ would make an ordinary `make check` pay for instrumentation.
find_program(GCOV_EXE NAMES gcov)
if(GCOV_EXE AND _python3 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${BOTANIK_ROOT}
                -B ${CMAKE_BINARY_DIR}/coverage -DBOTANIK_TARGET=SIMULATOR
                -DCMAKE_BUILD_TYPE=DEBUG
                -DCMAKE_CXX_FLAGS=--coverage -DCMAKE_EXE_LINKER_FLAGS=--coverage
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/coverage
                --parallel ${_nproc}
        # Discard the previous run's counts, or a deleted case still reads as
        # covered: gcov ACCUMULATES into an existing .gcda.
        COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_BINARY_DIR}/coverage
                -P ${BOTANIK_ROOT}/cmake/coverage-reset.cmake
        # A failing test is `check`'s business; the numbers are still worth
        # having, so the report runs either way.
        COMMAND ${CMAKE_COMMAND} -E env ${CMAKE_CTEST_COMMAND} -LE unit -E ^example:
                --test-dir ${CMAKE_BINARY_DIR}/coverage || ${CMAKE_COMMAND} -E true
        COMMAND ${_python3} ${BOTANIK_ROOT}/scripts/coverage.py
                --gcov ${GCOV_EXE}
                --tree ${CMAKE_BINARY_DIR}/coverage
                --src ${BOTANIK_ROOT}
                --out ${CMAKE_BINARY_DIR}/coverage.json
        USES_TERMINAL
        COMMENT "Sweep: line coverage of libbotanik, both suites (build/coverage/)"
        VERBATIM
    )
    add_dependencies(check-full coverage)
else()
    message(STATUS "check-full: no gcov or python3, skipping the coverage report")
endif()

# pyte screen-assertion tests (Python) - host-only, joined to the same CTest
# ============================================================================
# These drive botanik_sim over a PTY and assert its rendered screen, against
# this tree's sim, which is in ALL, so under check-full they are instrumented
# too. See botanik_add_pyte_tests() in cmake/testkit.cmake.
botanik_add_pyte_tests(botanik ${CMAKE_CURRENT_SOURCE_DIR}/pyte)
# `check` names it explicitly, so the gate does not depend on ALL having been
# built first. CTest never builds anything, and CMake's built-in `test` target
# cannot be given dependencies.
add_dependencies(check botanik_sim)
