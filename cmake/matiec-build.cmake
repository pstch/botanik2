# Build the pinned matiec into vendor/matiec/build/. Run with -P, by codegen
# when a site with laws needs regenerating, and by the `matiec` target.
#
#   SRC  - the matiec source tree (vendor/matiec)
#   JOBS - parallel make (default 4)
#
# Once per checkout, shared by every build tree: iec2c is a host tool, and
# build/iec2c.version records which pin it was built from. The sources are
# copied into build/src first: matiec has no VPATH build, and autoreconf writes
# into whatever tree it runs in. The compiler is autoconf's own choice, since
# an OPTA tree's compiler is arm-none-eabi-gcc.

set(BIN "${SRC}/build")
if(NOT JOBS)
    set(JOBS 4)
endif()

if(NOT EXISTS "${SRC}/configure.ac")
    message(FATAL_ERROR "no matiec source at ${SRC}")
endif()
file(MAKE_DIRECTORY "${BIN}")
file(LOCK "${BIN}" DIRECTORY TIMEOUT 900)

file(READ "${SRC}/UPSTREAM" _pin)
if(EXISTS "${BIN}/iec2c" AND EXISTS "${BIN}/iec2c.version")
    file(READ "${BIN}/iec2c.version" _built)
    if(_built STREQUAL _pin)
        return()
    endif()
endif()

foreach(_tool flex bison autoreconf make)
    find_program(_matiec_${_tool} ${_tool})
    if(NOT _matiec_${_tool})
        message(FATAL_ERROR "matiec needs ${_tool} to build")
    endif()
endforeach()

set(_work "${BIN}/src")

function(_matiec_run WHAT)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${_work}"
                    RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "matiec: ${WHAT} failed (${_rc}) in ${_work}\n${_err}")
    endif()
endfunction()

message(STATUS "matiec: building the pinned iec2c (once per checkout)")
file(REMOVE_RECURSE "${_work}")
file(COPY "${SRC}/" DESTINATION "${_work}" REGEX "^${SRC}/build$" EXCLUDE)

# Upstream ships configure.ac and no configure, so this is not optional.
_matiec_run("autoreconf -i" autoreconf -i)
_matiec_run("configure" "${_work}/configure" --quiet)
_matiec_run("make" make "-j${JOBS}")

if(NOT EXISTS "${_work}/iec2c")
    message(FATAL_ERROR "matiec: built, but no iec2c under ${_work}")
endif()
file(COPY "${_work}/iec2c" DESTINATION "${BIN}")
file(WRITE "${BIN}/iec2c.version" "${_pin}")
message(STATUS "matiec: built ${BIN}/iec2c")
