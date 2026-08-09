# Unit tests (doctest) - host-only, SIMULATOR build.
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
    test_alarms.cpp
    test_console.cpp
    test_logging.cpp
    test_modbus_master.cpp
    test_modbus_rtu.cpp
    test_modbus_tcp.cpp
    test_network.cpp
    test_plc.cpp
    test_scan.cpp
    test_site.cpp
    test_storage.cpp
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
# test_sizes.cpp pins the selected site's payload via BOTANIK_SITE_HEADER, derived
# from the PUBLIC BOTANIK_SITE define (libbotanik_site.h) inherited from botanik.

# The doctest binary is registered TWICE, and the label is what tells them apart:
#
#   doctest        - one entry running every case in one process. UNLABELLED, so
#                    the gate runs it. ~0.6 s.
#   <case name>    - one entry per TEST_CASE, labelled `unit`, so `ctest -R`, the
#                    VS Code Testing panel and an IDE debugger can each address a
#                    single case. ~14 s for the set, which is why the gate skips
#                    them (`ctest -LE unit`, see the check target).
#
# The cost is per-PROCESS, not per-test: 491 cases run in ~0.6 s together and
# ~14 s apart, all of it process startup and static-table init paid 491 times.
# Running them together is also the configuration that catches a case corrupting
# state for the ones after it - a per-case process hides that by construction
# (which is how an unbound Bank walking garbage pointers hid; see its value-init).
#
# So the gate gets the fast, stricter one, and per-case addressing stays for the
# times you actually want it: a bare `ctest` runs both.
add_test(NAME doctest COMMAND botanik_tests)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
include(doctest)
doctest_discover_tests(botanik_tests PROPERTIES LABELS unit)

# `make check` is the one-shot pre-push gate. Its dependencies build everything
# the suite needs (`make test`/`ctest` do not), then the command runs the suite
# with failures echoed inline:
#   - botanik_tests : the doctest binary (links the normal library)
#
# The warning gate is no longer a dependency here: the library compiles -Werror
# directly (BOTANIK_WERROR, top-level CMakeLists), so building botanik_tests at
# all already fails on any warning in a library source. That retired the
# botanik_strict mirror and the 63 duplicate TU compiles it cost every run.
#
# Sanitizers are no longer in this gate either - see `check-full` below.
#
# clang-tidy is deliberately NOT here. It is a sweep, not a per-change check: it
# re-lints on any touched header, so it dominated a gate meant to run after every
# edit, and its bugprone-* findings are the kind you review in a batch rather
# than react to one at a time. Run it on its own when that batch is due:
#     make clang-tidy        # incremental, one stamp per TU
#     make clang-tidy-all    # the whole tree, ignoring stamps
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
# The run itself is gate.cmake: `-LE unit` first, and the per-case entries only
# when doctest failed and there is something to attribute. Excluding by LABEL
# rather than naming what to run means a newly added test joins the gate by
# default - it has to be labelled `unit` to be left out.
add_custom_target(check
    COMMAND ${CMAKE_COMMAND} -DCTEST=${CMAKE_CTEST_COMMAND}
            -DJOBS=${BOTANIK_CTEST_JOBS}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/gate.cmake
    DEPENDS botanik_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Pre-push gate: warning-clean build + unit tests"
    VERBATIM
)

# `make check-full` is the SWEEP: everything deliberately kept out of the everyday
# `check` because it is too slow to pay per edit, in one target.
#
#   1. clang-tidy  - the bugprone-* checks (incremental, one stamp per TU)
#   2. the whole gate again under ASan+UBSan, in a tree of its own
#
# Both were exiled for the same reason and on the same reasoning, so they belong
# behind the same command: clang-tidy re-lints on any touched header, and
# instrumentation more than doubles compile CPU (the measured detail is on
# BOTANIK_SANITIZE in the top-level CMakeLists). Run this before a push that
# touches memory handling, and periodically otherwise.
#
# clang-tidy runs in THIS tree, not the sanitized one: it lints the compile flags
# an editor and clangd actually see, and -fsanitize= changes none of its findings
# while doubling the compile it does to get them.
#
# It drives the sub-tree's OWN `check`, so it is the same gate by construction -
# doctest and pyte, gate.cmake's failure attribution, all of it - rather than a
# second list of test commands that could drift from this one.
#
# It builds in a tree of ITS OWN under build/asan/, so the default tree keeps its
# uninstrumented objects and the two never invalidate each other.
#
# One asymmetry worth knowing: the pyte HALF is not instrumented, and cannot be.
# Its sim is built by a nested sub-build that pins RELEASE and BOTANIK_SANITIZE=OFF
# unconditionally, because those tests need a sim that is not sanitizer-slow. So
# this target instruments everything that can be: the unit suite runs under ASan +
# UBSan, and the screen tests run as they always do.
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
# Guarded: the clang-tidy target only exists when clang-tidy was found at configure
# time and the generator is Make (see the top-level CMakeLists). Without the guard
# a tree without it fails to configure rather than simply skipping the lint. As a
# DEPENDS it runs BEFORE the commands above, so a lint finding stops the sweep
# before it pays for an instrumented rebuild.
if(TARGET clang-tidy)
    add_dependencies(check-full clang-tidy)
endif()

# pyte screen-assertion tests (Python) - host-only, joined to the same CTest
# ============================================================================
# These drive botanik_sim over a PTY and assert its rendered screen (see
# pyte/README.md). Their deps (pytest + pyte) are a MANUAL developer step - the
# build never installs anything. We register the suite only when the interpreter
# can already import them, so a tree without the venv still gets a green
# `make check`. Point CMake at the venv with -DPython3_EXECUTABLE=... .
#
# The suite always runs against a dedicated RELEASE, no-sanitizer sim built in a
# sub-tree: dilated time leaves a ~1ms wall budget per tick, which a debug+ASAN
# build can't meet. BOTANIK_PYTE_TESTS=OFF in that sub-build keeps this block
# from recursing.
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
        #
        # The OUTPUT is a stamp we touch ourselves, NOT the sub-tree's
        # CMakeCache.txt. CMake leaves that file's mtime alone when a re-configure
        # changes no cache value, so it stayed older than the CMakeLists it depends
        # on and make re-ran this step on EVERY build - ~6 s added to a no-op
        # `make check`, which is most of the ones a pre-push gate does.
        add_custom_command(
            OUTPUT ${_pyte_sim_dir}/configure.stamp
            COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${CMAKE_SOURCE_DIR}
                    -B ${_pyte_sim_dir} -DBOTANIK_TARGET=SIMULATOR
                    -DCMAKE_BUILD_TYPE=RELEASE -DBOTANIK_SANITIZE=OFF
                    -DBOTANIK_PYTE_TESTS=OFF -DBOTANIK_LTO=OFF
            COMMAND ${CMAKE_COMMAND} -E touch ${_pyte_sim_dir}/configure.stamp
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
            DEPENDS ${_pyte_sim_srcs} ${_pyte_sim_dir}/configure.stamp
            COMMENT "Building release botanik_sim for the pyte tests"
            VERBATIM)
        # In ALL, so an ordinary `make` produces it. CTest never builds anything -
        # `make test` and `ctest` only run - and CMake's built-in `test` target
        # cannot be given dependencies, so a sim outside the default build is one
        # the screen tests find missing: `make && make test` failed on a tree that
        # had just been built. Costs ~0.4 s on a no-op `make` (the sub-build's
        # dependency scan); after editing a library source it rebuilds the release
        # sim too, which is the price of `make test` meaning what it says.
        add_custom_target(pyte-sim ALL DEPENDS ${_pyte_sim_bin})

        # One CTest entry per test FILE. Per-function entries needed pytest's node
        # IDs, which meant asking --collect-only for them at configure time and
        # regex-parsing the reply - and each entry then paid ~1.5 s importing
        # pytest to run ~0.4 s of test. Per-file keeps failures attributable and
        # the -j slots busy for a fraction of that startup tax; measured 2.5x
        # faster on 4 cores. A glob needs no probe and no fallback branch.
        file(GLOB _pyte_test_files CONFIGURE_DEPENDS ${_pyte_dir}/test_*.py)
        foreach(_file IN LISTS _pyte_test_files)
            get_filename_component(_name ${_file} NAME)
            # WORKING_DIRECTORY is the test dir so pytest loads its conftest (the
            # --sim option) regardless of the invocation's CWD.
            add_test(NAME "pyte:${_name}"
                COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider
                        ${_name} --sim ${_pyte_sim_bin}
                WORKING_DIRECTORY ${_pyte_dir})
            # ctest launches the most expensive test first, but only once it has
            # timings of its own - and it never gets usable ones here, since
            # doctest case names contain spaces and its cost file is
            # whitespace-delimited. Declaring the cost keeps a screen test from
            # starting after a few hundred sub-second unit cases on a fresh tree,
            # which is where the -j slots go idle waiting for the tail.
            set_tests_properties("pyte:${_name}" PROPERTIES COST 10)
        endforeach()
        list(LENGTH _pyte_test_files _pyte_count)
        message(STATUS "pyte: registered ${_pyte_count} screen test files")
        # `check` names it explicitly too, so the gate does not depend on ALL
        # having been run first.
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
            COMMENT "Regenerating pyte golden screens - review the diff before committing")
    else()
        message(STATUS "pyte/pytest not importable - skipping Python screen tests")
    endif()
elseif(NOT BOTANIK_PYTE_TESTS)
    # quietly disabled (e.g. the release sub-build above)
elseif(NOT Python3_Interpreter_FOUND)
    message(STATUS "Python3 interpreter not found - skipping Python screen tests")
endif()
