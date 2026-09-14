# Configures and builds one example as its own project, then writes the CTest
# entries botanik's tree includes for it. Run by the example-<name> targets.
#
#   -DNAME=<name> -DSRC=<examples/name> -DBIN=<build tree> -DROOT=<botanik>
#   -DFARM=<dir or empty> -DGENERATOR=<gen> -DOPTIONS=<|-list> -DCTEST=<ctest>
#   -DDOCTEST_ADD=<doctestAddTests.cmake>
#
# FARM is set for an example whose extern/botanik is not this checkout (a site
# repository carried as a submodule): the example is built from FARM, a
# directory of links to SRC's entries plus extern/botanik -> ROOT, so it tests
# this checkout and codegen still writes into the example's own sources.

string(REPLACE "|" ";" OPTIONS "${OPTIONS}")

if(FARM)
    file(REMOVE_RECURSE "${FARM}")
    file(MAKE_DIRECTORY "${FARM}/extern")
    file(GLOB _entries LIST_DIRECTORIES true RELATIVE "${SRC}" "${SRC}/*" "${SRC}/.*")
    foreach(_e IN LISTS _entries)
        if(_e MATCHES "^(\\.|\\.\\.|extern|build.*|\\.git|compile_commands\\.json)$")
            continue()
        endif()
        file(CREATE_LINK "${SRC}/${_e}" "${FARM}/${_e}" SYMBOLIC)
    endforeach()
    file(CREATE_LINK "${ROOT}" "${FARM}/extern/botanik" SYMBOLIC)
    set(_source "${FARM}")
else()
    set(_source "${SRC}")
endif()

# Configured once; the build re-runs CMake itself when a CMakeLists moves.
if(NOT EXISTS "${BIN}/CMakeCache.txt")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -G ${GENERATOR} -S ${_source} -B ${BIN} ${OPTIONS}
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        file(REMOVE "${BIN}/CMakeCache.txt")
        message(FATAL_ERROR "example ${NAME}: configure failed")
    endif()
endif()

# No --parallel: under make this joins the calling make's jobserver, so the
# examples share the -j of the build that runs them.
execute_process(COMMAND ${CMAKE_COMMAND} --build ${BIN} RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "example ${NAME}: build failed")
endif()

# The whole entry runs the example's own ctest: its test binary in one process,
# and whatever else it registers (its pyte screens).
set(_cases "${BIN}/botanik-cases.cmake")
file(WRITE "${_cases}.new"
    "add_test([==[example:${NAME}]==] \"${CTEST}\" --test-dir \"${BIN}\" --output-on-failure --no-tests=error)\n")

# One entry per case of <name>_tests, when the example has one.
file(GLOB_RECURSE _bins "${BIN}/${NAME}_tests")
list(FILTER _bins EXCLUDE REGEX "/CMakeFiles/")
if(_bins)
    list(GET _bins 0 _bin)
    set(TEST_EXECUTABLE "${_bin}")
    set(TEST_PREFIX "example:${NAME}:")
    set(TEST_WORKING_DIR "${BIN}")
    set(TEST_LIST "example_${NAME}_cases")
    set(CTEST_FILE "${BIN}/botanik-cases-per-case.cmake")
    include("${DOCTEST_ADD}")
    # Labelled here rather than through TEST_PROPERTIES, which flattens a
    # two-label list into separate arguments.
    file(APPEND "${_cases}.new"
        "include(\"${CTEST_FILE}\")\n"
        "set_tests_properties(\${example_${NAME}_cases} PROPERTIES LABELS \"unit;unit:${NAME}\")\n")
endif()
file(RENAME "${_cases}.new" "${_cases}")
