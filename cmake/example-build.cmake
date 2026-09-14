# Configures and builds one example as its own project, then writes the CTest
# entries botanik's tree includes for it. Run by the example-<name> targets.
#
#   -DNAME=<name> -DSRC=<examples/name> -DBIN=<build tree> -DROOT=<botanik>
#   -DGENERATOR=<gen> -DOPTIONS=<|-list> -DCTEST=<ctest>
#   -DDOCTEST_ADD=<doctestAddTests.cmake>
#
# An example must build against ROOT, the checkout under test: ref and nosite
# link extern/botanik to it, and gh_mlb41 finds the checkout enclosing it. One
# that resolves another botanik fails here rather than testing the wrong one.

string(REPLACE "|" ";" OPTIONS "${OPTIONS}")

# Configured once; the build re-runs CMake itself when a CMakeLists moves.
if(NOT EXISTS "${BIN}/CMakeCache.txt")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -G ${GENERATOR} -S ${SRC} -B ${BIN} ${OPTIONS}
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        file(REMOVE "${BIN}/CMakeCache.txt")
        message(FATAL_ERROR "example ${NAME}: configure failed")
    endif()
endif()

file(STRINGS "${BIN}/CMakeCache.txt" _root REGEX "^BOTANIK_ROOT:INTERNAL=")
string(REGEX REPLACE "^BOTANIK_ROOT:INTERNAL=" "" _root "${_root}")
file(REAL_PATH "${_root}" _root)
if(NOT _root STREQUAL ROOT)
    message(FATAL_ERROR "example ${NAME} builds against ${_root}, not this "
        "checkout (${ROOT})")
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
