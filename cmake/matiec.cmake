# The ST toolchain
# =============================================================================
# Included from the root, SIMULATOR side. matiec is a developer tool and never a
# build step: its output is committed under src/sites/<site>/generated/. What is
# pinned here is the compiler, so two machines regenerating produce the same
# bytes; a different matiec silently rewrites every POU.
#
# The check is tests/CMakeLists.txt's `check-generated`, which regenerates every
# committed artefact and diffs. This file only builds the compiler that check is
# made against; without one it compares against whatever is on PATH.
#
# Nothing here enters ALL and nothing is required: without flex and bison the
# target is not declared, as `clang-tidy` and `check-opta` are not.
#
# Licence: matiec is GPLv3 and none of it reaches the firmware. We run the
# binary, and the IEC library its output is compiled against is ours
# (src/libbotanik/plc/functions/ieclib.txt, passed as -I). vendor/matiec carries
# no lib/ at all.

set(BOTANIK_MATIEC_SRC "${BOTANIK_ROOT}/vendor/matiec" CACHE PATH
    "matiec source tree (default: the pinned copy under vendor/)")

set(_matiec_bin   "${CMAKE_BINARY_DIR}/matiec")
set(_matiec_iec2c "${_matiec_bin}/iec2c")

# Plain find_program, as DOXYGEN and CLANG_TIDY are. FindFLEX/FindBISON bring a
# pile of variables for generating parsers, which is matiec's own business.
find_program(FLEX       flex)
find_program(BISON      bison)
find_program(AUTORECONF autoreconf)
find_program(GIT_EXE    git)
find_package(Python3 COMPONENTS Interpreter QUIET)

if(NOT EXISTS "${BOTANIK_MATIEC_SRC}/configure.ac")
    message(STATUS "matiec source absent - no `matiec` target "
                   "(expected ${BOTANIK_MATIEC_SRC})")
elseif(NOT FLEX OR NOT BISON OR NOT AUTORECONF)
    message(STATUS "flex/bison/autoreconf not all found - no `matiec` target")
elseif(NOT Python3_Interpreter_FOUND OR NOT GIT_EXE)
    message(STATUS "python3 or git not found - no `matiec` target")
else()

    # OUTPUT-based, so it builds once per tree and never again.
    # `rm -rf build/matiec` forces it, the same move as `rm -rf build/clang-tidy`.
    add_custom_command(OUTPUT "${_matiec_iec2c}"
        COMMAND ${CMAKE_COMMAND}
                "-DSRC=${BOTANIK_MATIEC_SRC}" "-DBIN=${_matiec_bin}"
                "-DJOBS=${_nproc}"
                -P "${BOTANIK_ROOT}/cmake/matiec-build.cmake"
        DEPENDS "${BOTANIK_ROOT}/cmake/matiec-build.cmake"
                "${BOTANIK_MATIEC_SRC}/configure.ac"
        COMMENT "Building the pinned matiec (rm -rf ${_matiec_bin} to force)"
        USES_TERMINAL VERBATIM)
    add_custom_target(matiec DEPENDS "${_matiec_iec2c}")

    # What check-generated diffs against. Set only here, so a tree without the
    # toolchain leaves it empty and that check falls back to PATH. A plain set()
    # reaches tests/ because include() shares the root's scope, which is also why
    # every other name in this file is prefixed: an unprefixed one would override
    # whatever tests/CMakeLists.txt had resolved under the same name.
    set(BOTANIK_PINNED_IEC2C "${_matiec_iec2c}")

endif()
