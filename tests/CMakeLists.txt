# Unit tests (doctest) - host-only, SIMULATOR build.
# ============================================================================
# Wired up only from the SIMULATOR branch of the top-level CMakeLists. doctest is
# a single vendored header (tests/vendor/doctest.h): nothing to fetch or build.

# The suite compiles the selected site's header, so it links the site's own
# sources too - the IO value stores live in one of them.
#
# -Wshadow on those, as the top-level does for the sim: a source property is
# per-directory, so the target defined here needs its own.
set_source_files_properties(${BOTANIK_SITE_SOURCES}
                            PROPERTIES COMPILE_FLAGS "-Wshadow")

add_executable(botanik_tests
    ${BOTANIK_SITE_SOURCES}
    doctest_main.cpp
    test_alarms.cpp
    test_console.cpp
    test_health.cpp
    test_logging.cpp
    test_modbus_master.cpp
    test_modbus_rtu.cpp
    test_modbus_tcp.cpp
    test_network.cpp
    test_plc.cpp
    test_program.cpp
    test_scan.cpp
    test_sizes.cpp
    test_site.cpp
    test_site_programs.cpp
    test_site_scenarios.cpp
    test_sun.cpp
    test_tasks.cpp
    test_utils.cpp
    test_vars.cpp
)

# vendor/ for doctest.h; src/ for the library and the simulator's site payloads.
target_include_directories(botanik_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(botanik_tests botanik)

# Where the committed goldens live. Only test_alarms.cpp reads them, so this
# stays a test-target define rather than a project-wide one.
target_compile_definitions(botanik_tests PRIVATE
    BOTANIK_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

# test_sizes.cpp prints the footprint table and reaches the site through
# BOTANIK_SITE_HEADER, derived from the PUBLIC BOTANIK_SITE define
# (libbotanik_site.h) inherited from botanik. The pinning itself is
# libbotanik_sizes.h's, at COMPILE time; this only shows the numbers.

# The doctest binary is registered TWICE; the label tells them apart:
#
#   doctest      - every case in one process. UNLABELLED, so the gate runs it.
#   <case name>  - one entry per TEST_CASE, labelled `unit`, so `ctest -R` and an
#                  IDE test panel can address a single case.
#
# The gate excludes `unit` (~14 s against ~0.6 s, the cost being per-PROCESS
# startup paid once per case), and one process is also the configuration that
# catches a case corrupting state for the ones after it. A bare `ctest` runs both.
add_test(NAME doctest COMMAND botanik_tests)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
include(doctest)
doctest_discover_tests(botanik_tests PROPERTIES LABELS unit)

# The generated variable tables, against the CSVs they come from. The output is
# COMMITTED, not built - the tree has no Python build dependency and an Arduino
# sketch path could not run a generator - so this is the drift check, the same
# discipline the pyte goldens use.
#
# Conditional on Python at configure time, like clang-tidy and the pyte suite: a
# tree without it still builds and still passes the gate, it just cannot notice
# a CSV edited without regenerating.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(check-vars
        # The checker first, against tables that are deliberately wrong. Nobody
        # reviews 331 rows of generated C++ line by line, so a check that
        # silently stopped biting would be worse than no check at all.
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/test_vars_checks.py
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/vars_gen.py --check
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking the variable tables and the generated C++")
    set(BOTANIK_CHECK_VARS check-vars)

    # The two documents the tables can produce. Not in the gate: they are
    # deliverables, not assertions, and the PDF half needs reportlab.
    add_custom_target(vars-report
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/vars_report.py
                --schema
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/vars_report.py
                --site ${BOTANIK_SITE}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Variable reference (docs/integration) and the ${BOTANIK_SITE} report")

    # The third, and the only one that has to RUN something: it reports what the
    # control system did, out of the trace the scenario cases write while they
    # pass. DEPENDS the suite, because a stale binary would report a plant that
    # is no longer there.
    add_custom_target(scenario-report
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/scenario_report.py
                --site ${BOTANIK_SITE} --binary $<TARGET_FILE:botanik_tests>
        DEPENDS botanik_tests
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "What the ${BOTANIK_SITE} control system did, through its scenarios")
else()
    add_custom_target(check-vars
        COMMAND ${CMAKE_COMMAND} -E echo
                "python3 not found - the generated tables cannot be checked")
    set(BOTANIK_CHECK_VARS "")
    message(STATUS "python3 not found - `check` will not verify the generated tables")
endif()

# `make check` is the one-shot pre-push gate. Its DEPENDS build what the suite
# needs (`make test`/`ctest` build nothing), then it runs the suite.
#
# The warning gate is the build itself: the library compiles -Werror
# (BOTANIK_WERROR), so building botanik_tests fails on any library warning.
# Sanitizers and clang-tidy are sweeps, not per-change checks: see `check-full`.
#
# -j defaults to the host core count (override with -DBOTANIK_CTEST_JOBS=N),
# which is the cap because each pyte sim wants a core to itself.
include(ProcessorCount)
ProcessorCount(_nproc)
if(_nproc EQUAL 0)
    set(_nproc 4)
endif()
set(BOTANIK_CTEST_JOBS ${_nproc} CACHE STRING
    "Parallel job count for the ctest gate (make check)")
# The run itself is gate.cmake: `-LE unit` first, then the per-case entries only
# when doctest failed. Excluding by LABEL rather than naming what to run means a
# newly added test joins the gate by default.
add_custom_target(check
    COMMAND ${CMAKE_COMMAND} -DCTEST=${CMAKE_CTEST_COMMAND}
            -DJOBS=${BOTANIK_CTEST_JOBS}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/gate.cmake
    DEPENDS botanik_tests ${BOTANIK_CHECK_VARS}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Pre-push gate: warning-clean build + unit tests"
    VERBATIM
)

# `make check-bare` compiles the OTHER end of the feature profile: sites/bare,
# every optional flag off. Compile-only and site-specific, so it builds
# botanik_sim rather than the suite - the tests are written against a site that
# HAS a variable table.
#
# It exists because an OFF path nothing builds rots silently. When it was added,
# three already had: two modbus .cpp files closed their feature guard before the
# end of the file, and register_vars() was gated on CONSOLE rather than VARS.
add_custom_target(check-bare
    COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${CMAKE_SOURCE_DIR}
            -B ${CMAKE_SOURCE_DIR}/build-bare -DBOTANIK_TARGET=SIMULATOR
            -DBOTANIK_SITE=bare
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_SOURCE_DIR}/build-bare
            --target botanik_sim --parallel ${_nproc}
    # And the profile in between, which is the one a reusable-parts build
    # actually wants: a console, a shell and its log/task views, with no
    # variable model and no alarm page behind them.
    # The two extremes alone would not compile the `#if VARS` inside a page or a
    # shell command, only the file-level guard around it.
    COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${CMAKE_SOURCE_DIR}
            -B ${CMAKE_SOURCE_DIR}/build-bare-ui -DBOTANIK_TARGET=SIMULATOR
            -DBOTANIK_SITE=bare
            -DBOTANIK_FEATURE_CONSOLE=ON -DBOTANIK_FEATURE_LOGGING=ON
            -DBOTANIK_FEATURE_SHELL_PAGE=ON -DBOTANIK_FEATURE_LOGS_PAGE=ON
            -DBOTANIK_FEATURE_TASKS_PAGE=ON
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_SOURCE_DIR}/build-bare-ui
            --target botanik_sim --parallel ${_nproc}
    USES_TERMINAL
    COMMENT "Sweep: libbotanik with every optional feature off (build-bare/)"
    VERBATIM
)

# `make check-full` is the periodic SWEEP: clang-tidy, then the whole gate again
# under ASan+UBSan in a tree of its own under build/asan/. Too slow to pay per
# edit; run it before a push that touches memory handling. It drives the
# sub-tree's own `check`, so it cannot drift from this gate, and clang-tidy is a
# DEPENDS so a finding stops the sweep before the instrumented rebuild.
add_custom_target(check-full
    COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${CMAKE_SOURCE_DIR}
            -B ${CMAKE_BINARY_DIR}/asan -DBOTANIK_TARGET=SIMULATOR
            -DCMAKE_BUILD_TYPE=DEBUG -DBOTANIK_SANITIZE=address+undefined
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/asan
            --target check --parallel ${_nproc}
    USES_TERMINAL
    COMMENT "Sweep: clang-tidy + the full gate under ASan+UBSan (build/asan/)"
    VERBATIM
)
# Guarded: the target only exists when clang-tidy was found and the generator is Make.
if(TARGET clang-tidy)
    add_dependencies(check-full clang-tidy)
endif()
# The bare profile is part of the sweep: cheap next to an instrumented rebuild,
# and it is the only thing that compiles a feature's OFF path.
add_dependencies(check-full check-bare)

# pyte screen-assertion tests (Python) - host-only, joined to the same CTest
# ============================================================================
# These drive botanik_sim over a PTY and assert its rendered screen (see
# pyte/README.md). Installing pytest + pyte is a MANUAL developer step: the build
# installs nothing and registers the suite only when the interpreter can already
# import them, so a tree without the venv still gets a green `make check`. Point
# CMake at the venv with -DPython3_EXECUTABLE=... .
#
# They run against THIS tree's botanik_sim, which is in ALL - so in build/asan
# (see check-full) the screen tests run instrumented too.
option(BOTANIK_PYTE_TESTS "Register the pyte screen-assertion tests" ON)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(BOTANIK_PYTE_TESTS AND Python3_Interpreter_FOUND)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import pytest, pyte"
        RESULT_VARIABLE _pyte_import_rc OUTPUT_QUIET ERROR_QUIET)
    if(_pyte_import_rc EQUAL 0)
        set(_pyte_dir ${CMAKE_CURRENT_SOURCE_DIR}/pyte)

        # One CTest entry per test FILE: failures stay attributable without each
        # entry paying ~1.5 s of pytest import to run ~0.4 s of test.
        file(GLOB _pyte_test_files CONFIGURE_DEPENDS ${_pyte_dir}/test_*.py)
        foreach(_file IN LISTS _pyte_test_files)
            get_filename_component(_name ${_file} NAME)
            # WORKING_DIRECTORY is the test dir so pytest loads its conftest (the
            # --sim option) regardless of the invocation's CWD.
            add_test(NAME "pyte:${_name}"
                COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider
                        ${_name} --sim $<TARGET_FILE:botanik_sim>
                WORKING_DIRECTORY ${_pyte_dir})
            # ctest launches the most expensive test first, but only from
            # timings of its own, which it never gets here (doctest case names
            # contain spaces and its cost file is whitespace-delimited).
            # Declaring the cost keeps a screen test off the tail of the run.
            set_tests_properties("pyte:${_name}" PROPERTIES COST 10)
        endforeach()
        list(LENGTH _pyte_test_files _pyte_count)
        message(STATUS "pyte: registered ${_pyte_count} screen test files")
        # `check` names it explicitly, so the gate does not depend on ALL having
        # been built first. CTest itself NEVER builds anything, and CMake's
        # built-in `test` target cannot be given dependencies.
        add_dependencies(check botanik_sim)

        # Opt-in golden regeneration, NOT part of check/test so the gate never
        # rewrites expectations. Review the diff before committing. No time scale
        # to pick: the harness drives the clock, so a capture cannot race.
        add_custom_target(pytest-update
            COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider
                    ${_pyte_dir} --sim $<TARGET_FILE:botanik_sim> --update
            WORKING_DIRECTORY ${_pyte_dir}
            DEPENDS botanik_sim
            USES_TERMINAL
            COMMENT "Regenerating pyte golden screens - review the diff before committing")
    else()
        message(STATUS "pyte/pytest not importable - skipping Python screen tests")
    endif()
elseif(NOT BOTANIK_PYTE_TESTS)
    # quietly disabled
elseif(NOT Python3_Interpreter_FOUND)
    message(STATUS "Python3 interpreter not found - skipping Python screen tests")
endif()
