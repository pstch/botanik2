# Unit tests (doctest) — host-only, SIMULATOR build.
# ============================================================================
# These tests link against the SIMULATOR-target `botanik` library and run on the
# host, so they are only wired up from the SIMULATOR branch of the top-level
# CMakeLists. doctest ships as a single vendored header (tests/vendor/doctest.h);
# there is nothing to fetch or build.

# The suite compiles the selected site's header (test_sizes.cpp pins its data
# struct), so it must link the site's own sources too - the IO value stores live
# in one of them.
add_executable(botanik_tests
    ${BOTANIK_SITE_SOURCES}
    doctest_main.cpp
    test_text.cpp
    test_time.cpp
    test_sun.cpp
    test_sizes.cpp
    test_logger.cpp
    test_alarms.cpp
    test_blocks.cpp
    test_scalar.cpp
    test_model.cpp
    test_scan.cpp
    test_alarms_render.cpp
    test_terminal_present.cpp
    test_editor_bytes.cpp
    test_filters.cpp
    test_line_buffer.cpp
    test_parser.cpp
    test_shell.cpp
    test_serial.cpp
    test_writer.cpp
    test_params.cpp
    test_storage.cpp
    test_resolver.cpp
    test_sun_tracker.cpp
    test_network.cpp
    test_hal_net.cpp
    test_notifier.cpp
    test_mqtt_log.cpp
    test_hal_memory.cpp
)

# vendor/ for doctest.h; src/ for the library and the simulator's site payloads.
target_include_directories(botanik_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(botanik_tests botanik)
# test_sizes.cpp pins the selected site's payload via BOTANIK_SITE_HEADER, derived
# from the PUBLIC BOTANIK_SITE define (libbotanik_site.h) inherited from botanik.

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
#
# The gate runs ctest in parallel (-j): the doctest cases and, now, the per-test
# pyte entries are independent, so a multi-core host clears the suite far faster.
# Defaults to the host core count; override with -DBOTANIK_CTEST_JOBS=N. Defaulting
# -j to the core count keeps each dilated pyte sim on its own core (they run best
# undisturbed) rather than piling more sims onto the box than it can run at once.
include(ProcessorCount)
ProcessorCount(_nproc)
if(_nproc EQUAL 0)
    set(_nproc 4)
endif()
set(BOTANIK_CTEST_JOBS ${_nproc} CACHE STRING
    "Parallel job count for the ctest gate (make check)")
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j ${BOTANIK_CTEST_JOBS}
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

        # Configuring the sub-tree is its own step, keyed on the CMake files: it
        # used to run on every source change, ahead of the build, for a tree that
        # was already configured. The sub-build's own `make` still re-runs CMake by
        # itself when its inputs change, so this only has to create the tree once.
        # LTO off: the whole-program link dominated this build, and the tests need
        # a sim that isn't sanitizer-slow, not one that is optimally inlined.
        add_custom_command(
            OUTPUT ${_pyte_sim_dir}/CMakeCache.txt
            COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${CMAKE_SOURCE_DIR}
                    -B ${_pyte_sim_dir} -DBOTANIK_TARGET=SIMULATOR
                    -DCMAKE_BUILD_TYPE=RELEASE -DBOTANIK_SANITIZE=OFF
                    -DBOTANIK_PYTE_TESTS=OFF -DBOTANIK_LTO=OFF
            DEPENDS ${CMAKE_SOURCE_DIR}/CMakeLists.txt
                    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
            COMMENT "Configuring the release botanik_sim tree for the pyte tests"
            VERBATIM)

        # --parallel is explicit: `cmake --build` starts a fresh make that inherits
        # no jobserver from the outer one, so without it this sub-build compiles
        # single-file-at-a-time no matter what -j the outer build was given.
        add_custom_command(
            OUTPUT ${_pyte_sim_bin}
            COMMAND ${CMAKE_COMMAND} --build ${_pyte_sim_dir} --target botanik_sim
                    --parallel ${_nproc}
            DEPENDS ${_pyte_sim_srcs} ${_pyte_sim_dir}/CMakeCache.txt
            COMMENT "Building release botanik_sim for the pyte tests"
            VERBATIM)
        add_custom_target(pyte-sim DEPENDS ${_pyte_sim_bin})

        # One CTest entry per pytest *file*, not per test function: a pytest
        # process spends ~1.5 s importing pytest before it runs anything, which
        # dwarfs a screen test's own ~0.4 s, so per-function entries paid that
        # toll 25 times over. Per-file keeps useful failure granularity and
        # parallelism at a fraction of the startup tax - and needs no
        # --collect-only probe at configure time.
        file(GLOB _pyte_test_files CONFIGURE_DEPENDS ${_pyte_dir}/test_*.py)
        foreach(_file IN LISTS _pyte_test_files)
            # WORKING_DIRECTORY is the test dir so pytest loads its conftest (the
            # --sim option) regardless of the invocation's CWD.
            get_filename_component(_name ${_file} NAME)
            add_test(NAME "pyte:${_name}"
                COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider
                        ${_name} --sim ${_pyte_sim_bin}
                WORKING_DIRECTORY ${_pyte_dir})
            # ctest launches the most expensive test first, but only once it has
            # timings of its own - and it never gets usable ones here, since
            # doctest case names contain spaces and its cost file is
            # whitespace-delimited. Declaring the cost keeps a screen test from
            # starting after 200 sub-second unit tests on a fresh tree, which is
            # where the -j slots go idle waiting for the tail.
            set_tests_properties("pyte:${_name}" PROPERTIES COST 10)
        endforeach()
        # ctest/make test don't build; `check` does — make it build the sim too.
        add_dependencies(check pyte-sim)

        # Opt-in golden regeneration (NOT part of check/test, so the gate never
        # rewrites expectations). Review the diff before committing. Regen runs at a
        # deliberately moderate scale (below the assert suite's default) so each
        # golden captures a low-drift snapshot - the assertions mask volatile state
        # either way, but a calm capture is easier to eyeball. Override on the
        # command line if a capture races (see tests/pyte/README.md on time dilation).
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
