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
    test_console_units.cpp
    test_editor.cpp
    test_editor_wire.cpp
    test_editor_fuzz.cpp
    test_shell_wire.cpp
    test_health.cpp
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
    test_ntp.cpp
    test_pages.cpp
    test_shell.cpp
    test_shell_vars.cpp
    test_shell_report.cpp
    test_site.cpp
    test_sun.cpp
    test_vars.cpp
    test_site_sense.cpp
    test_site_actuators.cpp
    test_site_irrigation.cpp
    test_site_scenarios.cpp
    test_site_water_scenarios.cpp
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


# `make check` is the one-shot pre-push gate. Its DEPENDS build what the suite
# needs (`make test`/`ctest` build nothing), then it runs the suite.
#
# The warning gate is the build itself: the library compiles -Werror
# (BOTANIK_WERROR), so building botanik_tests fails on any library warning.
# Sanitizers and clang-tidy are sweeps, not per-change checks: see `check-full`.
#
# -j defaults to the host core count (override with -DBOTANIK_CTEST_JOBS=N),
# which is the cap because each pyte sim wants a core to itself.
# _nproc comes from the root, which needs it for st/ too.
set(BOTANIK_CTEST_JOBS ${_nproc} CACHE STRING
    "Parallel job count for the ctest gate (make check)")
# The run itself is gate.cmake: `-LE unit` first, then the per-case entries only
# when doctest failed. Excluding by LABEL rather than naming what to run means a
# newly added test joins the gate by default.
add_custom_target(check
    COMMAND ${CMAKE_COMMAND} -DCTEST=${CMAKE_CTEST_COMMAND}
            -DJOBS=${BOTANIK_CTEST_JOBS}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/gate.cmake
    DEPENDS botanik_tests
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
            -DBOTANIK_FEATURE_SYSTEM_PAGE=ON
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_SOURCE_DIR}/build-bare-ui
            --target botanik_sim --parallel ${_nproc}
    # A third profile: the NETWORK stack with no variable model. It is not a
    # combination anyone ships, but it is the one that catches a `#if VARS`
    # forgotten around something the network reaches - which is what happened
    # to the config binds, and neither of the two profiles above could see it
    # because neither compiles network/.
    COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${CMAKE_SOURCE_DIR}
            -B ${CMAKE_SOURCE_DIR}/build-bare-net -DBOTANIK_TARGET=SIMULATOR
            -DBOTANIK_SITE=bare
            -DBOTANIK_FEATURE_NETWORK=ON -DBOTANIK_FEATURE_NTP=ON
            -DBOTANIK_FEATURE_MQTT=ON
            -DBOTANIK_FEATURE_VARS=OFF -DBOTANIK_FEATURE_PLC=OFF
            -DBOTANIK_FEATURE_HOME_PAGE=OFF
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_SOURCE_DIR}/build-bare-net
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
# ...and the same for the ST toolchain, which needs flex and bison. check-st
# asserts the COMMITTED generated C is what the pinned matiec produces: the
# offline-artifact analogue of check-bare, and the only thing that would ever
# notice a POU regenerated by a different compiler.
if(TARGET check-st)
    add_dependencies(check-full check-st)
endif()
# The bare profile is part of the sweep: cheap next to an instrumented rebuild,
# and it is the only thing that compiles a feature's OFF path.
# The generated sources are COMMITTED and never built, which is the whole of
# the generator's contract - and nothing held the two together. A contributor
# who edits a table and forgets to regenerate gets a green `make check` and a
# firmware without their row. This regenerates into a scratch copy of the
# generator's INPUTS and diffs, so it never touches the working tree.
#
# The PINNED matiec first (vendor/matiec, built by `make matiec`), because a
# check made against whatever is on PATH would call whichever developer
# regenerated second wrong. $IEC2C next, the override tables.py itself honours;
# then PATH. Skipped where neither it nor python3 is present, rather than
# failing the sweep for a tool a contributor has no reason to install.
find_program(_python3 python3)
if(BOTANIK_PINNED_IEC2C)
    set(_iec2c "${BOTANIK_PINNED_IEC2C}")
elseif(DEFINED ENV{IEC2C} AND EXISTS "$ENV{IEC2C}")
    set(_iec2c "$ENV{IEC2C}")
else()
    find_program(_iec2c iec2c)
endif()
set(_gen ${CMAKE_BINARY_DIR}/genchk)
set(_site src/sites/${BOTANIK_SITE}/generated)
if(_python3 AND _iec2c)
    add_custom_target(check-generated
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_gen}
        # The generator's INPUTS only: copying the source root would recurse
        # into this build directory and drag every object file with it.
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/scripts ${_gen}/scripts
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/src     ${_gen}/src
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/docs    ${_gen}/docs
        COMMAND ${CMAKE_COMMAND} -E env IEC2C=${_iec2c} ${CMAKE_COMMAND} -E chdir ${_gen}
                ${_python3} scripts/tables.py generate ${BOTANIK_SITE}
        COMMAND ${CMAKE_COMMAND} -E env IEC2C=${_iec2c} ${CMAKE_COMMAND} -E chdir ${_gen}
                ${_python3} scripts/tables.py config ${BOTANIK_SITE}
        COMMAND ${CMAKE_COMMAND} -E env IEC2C=${_iec2c} ${CMAKE_COMMAND} -E chdir ${_gen}
                ${_python3} scripts/tables.py plant ${BOTANIK_SITE}
        COMMAND ${CMAKE_COMMAND} -E env IEC2C=${_iec2c} ${CMAKE_COMMAND} -E chdir ${_gen}
                ${_python3} scripts/tables.py points ${BOTANIK_SITE}
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_SOURCE_DIR}/${_site}/vars.gen.cpp ${_gen}/${_site}/vars.gen.cpp
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_SOURCE_DIR}/${_site}/vars.gen.h   ${_gen}/${_site}/vars.gen.h
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_SOURCE_DIR}/${_site}/config.gen.h ${_gen}/${_site}/config.gen.h
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_SOURCE_DIR}/${_site}/plant.cpp    ${_gen}/${_site}/plant.cpp
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_SOURCE_DIR}/${_site}/POUS.c       ${_gen}/${_site}/POUS.c
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_SOURCE_DIR}/${_site}/rows.gen.h  ${_gen}/${_site}/rows.gen.h
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_SOURCE_DIR}/${_site}/enums.gen.h ${_gen}/${_site}/enums.gen.h
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_SOURCE_DIR}/${_site}/enums.st    ${_gen}/${_site}/enums.st
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_SOURCE_DIR}/src/libbotanik/plc/runtime/quality.gen.h
                ${_gen}/src/libbotanik/plc/runtime/quality.gen.h
        # health.gen.h is generated beside quality.gen.h and compiled in through
        # accessor.h; points.md was regenerated above and compared to nothing.
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_SOURCE_DIR}/src/libbotanik/plc/runtime/health.gen.h
                ${_gen}/src/libbotanik/plc/runtime/health.gen.h
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_SOURCE_DIR}/docs/reference/points.md
                ${_gen}/docs/reference/points.md
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_gen}
        USES_TERMINAL
        COMMENT "Sweep: the committed generated sources still match the tables"
        VERBATIM)
    # Build the pinned compiler first where there is one, so the diff is against
    # it rather than against whatever the developer happens to have.
    if(TARGET matiec)
        add_dependencies(check-generated matiec)
    endif()
    add_dependencies(check-full check-generated)
else()
    message(STATUS "check-full: no python3 or iec2c, skipping the generated-source check")
endif()

# ...and the CROSS build, because a link error is invisible to every host
# target. Arduino.h DECLARES the whole wiring API and libmbed.a defines none of
# it, so a HAL that reaches for a call the build does not compile fails at LINK
# time, on the OPTA alone - `make check` stays green throughout. Compile-only
# and only in the sweep: it needs arm-none-eabi and the Mbed core, so it is
# skipped where neither is installed rather than failing the sweep for it.
find_program(_arm_gxx arm-none-eabi-g++)
if(_arm_gxx)
    add_custom_target(check-opta
        COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${CMAKE_SOURCE_DIR}
                -B ${CMAKE_SOURCE_DIR}/build-opta -DBOTANIK_TARGET=OPTA
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_SOURCE_DIR}/build-opta
                --parallel ${_nproc}
        USES_TERMINAL
        COMMENT "Sweep: the OPTA cross build, which is where a link error lives"
        VERBATIM)
    add_dependencies(check-full check-opta)
else()
    message(STATUS "check-full: no arm-none-eabi-g++, skipping the OPTA cross build")
endif()

add_dependencies(check-full check-bare)

# `make coverage` builds a third tree, instrumented for gcov, runs BOTH suites
# against it and prints the per-area line coverage. It reports and never fails:
# a floor that fails a build teaches people to lower the floor, and what this is
# for is noticing a fall. build/coverage.json is the machine-readable half.
#
# Both suites, because neither alone is the gate: the doctest binary never
# exercises network/, and the pyte screen tests are the only thing that drives
# the shell over a real PTY. Measuring one of them names the wrong gaps.
#
# Its own tree, like check-bare's: --coverage changes every object, and folding
# it into build/ would make an ordinary `make check` pay for instrumentation.
find_program(GCOV_EXE NAMES gcov)
if(GCOV_EXE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${CMAKE_SOURCE_DIR}
                -B ${CMAKE_BINARY_DIR}/coverage -DBOTANIK_TARGET=SIMULATOR
                -DCMAKE_BUILD_TYPE=DEBUG
                -DCMAKE_CXX_FLAGS=--coverage -DCMAKE_EXE_LINKER_FLAGS=--coverage
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/coverage
                --parallel ${_nproc}
        # Discard the previous run's counts, or a deleted case still reads as
        # covered: gcov ACCUMULATES into an existing .gcda.
        COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_BINARY_DIR}/coverage
                -P ${CMAKE_SOURCE_DIR}/cmake/coverage-reset.cmake
        # Neither suite is allowed to stop the report: a failing test is `check`'s
        # business, and the numbers are still worth having.
        COMMAND ${CMAKE_COMMAND} -E env ${CMAKE_CTEST_COMMAND} -LE unit
                --test-dir ${CMAKE_BINARY_DIR}/coverage || ${CMAKE_COMMAND} -E true
        COMMAND ${CMAKE_COMMAND} -DGCOV=${GCOV_EXE}
                -DTREE=${CMAKE_BINARY_DIR}/coverage
                -DSRC=${CMAKE_SOURCE_DIR}
                -DOUT=${CMAKE_BINARY_DIR}/coverage.json
                -P ${CMAKE_SOURCE_DIR}/cmake/coverage.cmake
        USES_TERMINAL
        COMMENT "Sweep: line coverage of libbotanik, both suites (build/coverage/)"
        VERBATIM
    )
    add_dependencies(check-full coverage)
else()
    message(STATUS "check-full: no gcov, skipping the coverage report")
endif()

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
