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
    test_health.cpp
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

# `make check-full` is the periodic SWEEP: clang-tidy, then the whole gate again
# under ASan+UBSan in a tree of its own under build/asan/. Both are too slow to pay
# per edit; run this before a push that touches memory handling.
#
# It drives the sub-tree's own `check`, so it is the same gate by construction
# rather than a second list of test commands that could drift from this one.
# clang-tidy runs in THIS tree, on the flags an editor sees, and as a DEPENDS - so
# a finding stops the sweep before it pays for an instrumented rebuild.
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

# pyte screen-assertion tests (Python) - host-only, joined to the same CTest
# ============================================================================
# These drive botanik_sim over a PTY and assert its rendered screen (see
# pyte/README.md). Their deps (pytest + pyte) are a MANUAL developer step - the
# build never installs anything. We register the suite only when the interpreter
# can already import them, so a tree without the venv still gets a green
# `make check`. Point CMake at the venv with -DPython3_EXECUTABLE=... .
#
# They run against THIS tree's own botanik_sim, which is in ALL already - so in
# build/asan (see check-full) the screen tests run instrumented too.
option(BOTANIK_PYTE_TESTS "Register the pyte screen-assertion tests" ON)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(BOTANIK_PYTE_TESTS AND Python3_Interpreter_FOUND)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import pytest, pyte"
        RESULT_VARIABLE _pyte_import_rc OUTPUT_QUIET ERROR_QUIET)
    if(_pyte_import_rc EQUAL 0)
        set(_pyte_dir ${CMAKE_CURRENT_SOURCE_DIR}/pyte)

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
                        ${_name} --sim $<TARGET_FILE:botanik_sim>
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
        # `check` names it explicitly, so the gate does not depend on ALL having
        # been run first. CTest itself never builds anything, and CMake's built-in
        # `test` target cannot be given dependencies.
        add_dependencies(check botanik_sim)

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
