# Unit tests (doctest) — host-only, SIMULATOR build.
# ============================================================================
# These tests link against the SIMULATOR-target `botanik` library and run on the
# host, so they are only wired up from the SIMULATOR branch of the top-level
# CMakeLists. doctest ships as a single vendored header (tests/vendor/doctest.h);
# there is nothing to fetch or build.

add_executable(botanik_tests
    doctest_main.cpp
    test_text.cpp
    test_time.cpp
    test_sun.cpp
    test_sizes.cpp
    test_logger.cpp
    test_alarms.cpp
    test_alarms_render.cpp
    test_filters.cpp
    test_line_buffer.cpp
    test_parser.cpp
    test_shell.cpp
    test_serial.cpp
    test_writer.cpp
    test_config_handler.cpp
    test_resolver.cpp
    test_sun_tracker.cpp
    test_network.cpp
)

# vendor/ for doctest.h; src/ for the library and the simulator's site payloads.
target_include_directories(botanik_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(botanik_tests botanik)
# test_sizes.cpp pins the selected site's payload via the same header the harness uses.
target_compile_definitions(botanik_tests PRIVATE BOTANIK_SITE_HEADER="${BOTANIK_SITE_HEADER}")

# Register with CTest, one entry per TEST_CASE. doctest_discover_tests runs the
# binary at build time with --list-test-cases and emits an add_test() per case,
# so `ctest` and VS Code's Testing panel show/run/debug each case individually
# (rather than the whole binary as a single pass/fail). The module + its helper
# are vendored next to doctest.h; no fetch or install needed.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
include(doctest)
doctest_discover_tests(botanik_tests)

# `make check` is the one-shot pre-push gate. Its dependencies build everything
# the suite needs (`make test`/`ctest` do not) plus the two static gates, then the
# command runs the suite with failures echoed inline:
#   - botanik_tests : the doctest binary (links the normal library)
#   - botanik_strict : warning-clean compile gate — the -Werror mirror fails the
#                      build on any compiler warning
#   - clang-tidy     : bugprone static analysis, findings promoted to errors
# clang-tidy is added conditionally since the target only exists when
# run-clang-tidy was found at configure time.
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS botanik_tests botanik_strict
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Pre-push gate: warning-clean build + clang-tidy + unit tests"
    VERBATIM
)
if(TARGET clang-tidy)
    add_dependencies(check clang-tidy)
endif()

# pyte screen-assertion tests (Python) — host-only, joined to the same CTest
# ============================================================================
# These drive botanik_sim over a PTY and assert its rendered screen (see
# pyte/README.md). Their deps (pytest + pyte) are a MANUAL developer step — the
# build never installs anything. We register the suite only when the interpreter
# can already import them, so a tree without the venv still gets a green
# `make check`. Point CMake at the venv with -DPython3_EXECUTABLE=... .
#
# The suite always runs against a dedicated RELEASE, no-sanitizer sim built in a
# sub-tree: dilated time leaves a ~1ms wall budget per tick, which a debug+ASAN
# build can't meet (the overrun warning is silenced via BOTANIK_NO_XRUN_WARNING,
# but the sluggishness and other timing skew remain). BOTANIK_PYTE_TESTS=OFF in
# that sub-build keeps this block from recursing.
option(BOTANIK_PYTE_TESTS "Register the pyte screen-assertion tests" ON)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(BOTANIK_PYTE_TESTS AND Python3_Interpreter_FOUND)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import pytest, pyte"
        RESULT_VARIABLE _pyte_import_rc OUTPUT_QUIET ERROR_QUIET)
    if(_pyte_import_rc EQUAL 0)
        set(_pyte_dir ${CMAKE_CURRENT_SOURCE_DIR}/pyte)
        set(_pyte_sim_dir ${CMAKE_BINARY_DIR}/pyte-sim)
        set(_pyte_sim_bin ${_pyte_sim_dir}/botanik_sim)

        # Build the release sim the tests run against (rebuilt incrementally when
        # sources change, via the dependency on the OUTPUT below).
        file(GLOB_RECURSE _pyte_sim_srcs CONFIGURE_DEPENDS
             ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.c
             ${CMAKE_SOURCE_DIR}/src/*.h)
        add_custom_command(
            OUTPUT ${_pyte_sim_bin}
            COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${CMAKE_SOURCE_DIR}
                    -B ${_pyte_sim_dir} -DBOTANIK_TARGET=SIMULATOR
                    -DCMAKE_BUILD_TYPE=RELEASE -DBOTANIK_SANITIZE=OFF
                    -DBOTANIK_PYTE_TESTS=OFF
            COMMAND ${CMAKE_COMMAND} --build ${_pyte_sim_dir} --target botanik_sim
            DEPENDS ${_pyte_sim_srcs}
            COMMENT "Building release botanik_sim for the pyte tests"
            VERBATIM)
        add_custom_target(pyte-sim DEPENDS ${_pyte_sim_bin})

        # One CTest entry running the whole pytest suite. The test dir is passed
        # explicitly so pytest loads its conftest (and thus the --sim/--update
        # options) regardless of the invocation's CWD.
        add_test(NAME pyte
            COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider
                    ${_pyte_dir} --sim ${_pyte_sim_bin}
            WORKING_DIRECTORY ${_pyte_dir})
        # ctest/make test don't build; `check` does — make it build the sim too.
        add_dependencies(check pyte-sim)

        # Opt-in golden regeneration (NOT part of check/test, so the gate never
        # rewrites expectations). Review the diff before committing. PYTE_TIME_SCALE
        # (read by conftest.py) runs the sim clock faster than the assert suite's
        # default so a regen finishes sooner; override it on the command line if a
        # capture races (see tests/pyte/README.md on time dilation).
        set(BOTANIK_PYTE_UPDATE_SCALE 6 CACHE STRING
            "Sim time-scale used when regenerating pyte goldens (pytest-update)")
        add_custom_target(pytest-update
            COMMAND ${CMAKE_COMMAND} -E env PYTE_TIME_SCALE=${BOTANIK_PYTE_UPDATE_SCALE}
                    ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider
                    ${_pyte_dir} --sim ${_pyte_sim_bin} --update
            WORKING_DIRECTORY ${_pyte_dir}
            DEPENDS pyte-sim
            USES_TERMINAL
            COMMENT "Regenerating pyte golden screens — review the diff before committing")
    else()
        message(STATUS "pyte/pytest not importable — skipping Python screen tests")
    endif()
elseif(NOT BOTANIK_PYTE_TESTS)
    # quietly disabled (e.g. the release sub-build above)
elseif(NOT Python3_Interpreter_FOUND)
    message(STATUS "Python3 interpreter not found — skipping Python screen tests")
endif()
