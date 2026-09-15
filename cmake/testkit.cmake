# What a SITE repository tests its own site with.
#
# Available in every build that has a site, embedded or not, because a site
# repository never builds tests/ - the suite in there is botanik's own.
#
#   botanik_testkit    an INTERFACE target: doctest, the generic fixtures, the
#                      library and the selected site (botanik_site)
#   BOTANIK_TEST_MAIN  doctest's main, to compile beside your own cases
#
# In a site repository:
#
#   add_executable(site_tests ${BOTANIK_TEST_MAIN} tests/test_laws.cpp)
#   target_link_libraries(site_tests botanik_testkit)
#
# The fixtures it puts on the include path are tests/kit/, the ones written
# against no site in particular, and nothing else of botanik's suite:
#
#   site_bank.h         a bank over whatever table is selected
#   live_fixture.h      a running firmware and its ST scan (TestScan)
#   scenario_trace.h    a timeline a scenario case prints on failure
#   shell_rig.h         a command shell over the selected table
#   term_fixture.h      a terminal whose output a case can read back
#   ram_store.h         storage in RAM
#   recording_writer.h  a log writer that keeps what it was given
#
# ...and tests/kit/pyte/, the screen-test harness: harness.py (the fixtures a
# conftest imports), helpers.py, keys.py and normalize.py, on PYTHONPATH for
# every test botanik_add_pyte_tests() registers.
#
# A fixture that names a site's own rows belongs to that site.

add_library(botanik_testkit INTERFACE)
target_include_directories(botanik_testkit INTERFACE
    "${BOTANIK_ROOT}/tests/kit"
    "${BOTANIK_ROOT}/tests/vendor")
# The site's objects too: an object library's objects reach only the targets
# linking it directly, not those linking it through an interface.
target_link_libraries(botanik_testkit INTERFACE botanik_site)
target_sources(botanik_testkit INTERFACE $<TARGET_OBJECTS:botanik_site>)

set(BOTANIK_TEST_MAIN "${BOTANIK_ROOT}/tests/doctest_main.cpp"
    CACHE INTERNAL "doctest's main, for a site's own test binary")

# botanik_add_pyte_tests(<name> <dir>)
#
# One CTest entry per test_*.py in <dir>, against this tree's botanik_sim, with
# the library's harness on PYTHONPATH so <dir>'s conftest can import it. Also
# `pytest-update-<name>`, and `pytest-update` over every such directory.
#
# Registers nothing when pytest and pyte do not import: installing them is a
# developer step, and a tree without them still gets a green gate. Point CMake
# at a venv with -DPython3_EXECUTABLE=... .
option(BOTANIK_PYTE_TESTS "Register the pyte screen-assertion tests" ON)

function(botanik_add_pyte_tests name dir)
    if(NOT BOTANIK_PYTE_TESTS)
        return()
    endif()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT Python3_Interpreter_FOUND)
        message(STATUS "pyte: no Python3 interpreter, skipping ${name}")
        return()
    endif()
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import pytest, pyte"
        RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT _rc EQUAL 0)
        message(STATUS "pyte: pytest/pyte not importable, skipping ${name}")
        return()
    endif()

    get_filename_component(dir ${dir} ABSOLUTE)
    set(_harness "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/kit/pyte")
    get_filename_component(_harness ${_harness} ABSOLUTE)

    # One entry per FILE: failures stay attributable without each entry paying
    # ~1.5 s of pytest import to run ~0.4 s of test.
    file(GLOB _files CONFIGURE_DEPENDS ${dir}/test_*.py)
    foreach(_file IN LISTS _files)
        get_filename_component(_py ${_file} NAME)
        set(_test "pyte:${_py}")
        if(NOT name STREQUAL "botanik")
            set(_test "pyte:${name}:${_py}")
        endif()
        # WORKING_DIRECTORY is the test dir, so pytest loads that dir's conftest.
        add_test(NAME "${_test}"
            COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider
                    ${_py} --sim $<TARGET_FILE:botanik_sim>
            WORKING_DIRECTORY ${dir})
        # ctest orders by its own timings, which it never gets here: doctest
        # case names contain spaces and its cost file is whitespace-delimited.
        set_tests_properties("${_test}" PROPERTIES
            COST 10
            ENVIRONMENT "PYTHONPATH=${_harness}")
    endforeach()
    list(LENGTH _files _count)
    message(STATUS "pyte: registered ${_count} screen test files for ${name}")

    # Kept out of check/test so the gate never rewrites its own expectations.
    add_custom_target(pytest-update-${name}
        COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${_harness}"
                ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider
                ${dir} --sim $<TARGET_FILE:botanik_sim> --update
        WORKING_DIRECTORY ${dir}
        DEPENDS botanik_sim
        USES_TERMINAL
        COMMENT "Regenerating ${name}'s pyte goldens - review the diff before committing")
    if(NOT TARGET pytest-update)
        add_custom_target(pytest-update)
    endif()
    add_dependencies(pytest-update pytest-update-${name})
endfunction()
