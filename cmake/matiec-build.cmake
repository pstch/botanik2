# Build the pinned matiec. Driven with -P by the `matiec` target.
#
# The sources are COPIED into ${BIN}/src first: matiec has no VPATH build, and
# building in place would leave object files and generated Makefiles in
# st/vendor/, where `git status` would carry them for ever.
#
#   SRC  - the matiec source tree (BOTANIK_MATIEC_SRC)
#   BIN  - where iec2c lands; ${BIN}/src is the scratch copy
#   JOBS - parallel make
#
# The compiler is autoconf's own choice, deliberately: CMAKE_C_COMPILER is
# arm-none-eabi-gcc in an OPTA tree and this is a HOST tool.

set(_work "${BIN}/src")

function(_matiec_run WHAT)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${_work}"
                    RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "matiec: ${WHAT} failed (${_rc}) in ${_work}\n${_err}")
    endif()
endfunction()

file(REMOVE_RECURSE "${_work}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SRC}" "${_work}")

# Upstream ships configure.ac and no configure, so this is not optional.
_matiec_run("autoreconf -i" autoreconf -i)
_matiec_run("configure" "${_work}/configure" --quiet)
_matiec_run("make" make "-j${JOBS}")

if(NOT EXISTS "${_work}/iec2c")
    message(FATAL_ERROR "matiec: built, but no iec2c under ${_work}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${_work}/iec2c" "${BIN}/iec2c")

# Provenance, for when a check-st diff has to be explained.
if(EXISTS "${SRC}/UPSTREAM")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy
                    "${SRC}/UPSTREAM" "${BIN}/iec2c.version")
endif()
message(STATUS "matiec: built ${BIN}/iec2c")
