# The ST toolchain
# =============================================================================
# matiec is a DEVELOPER TOOL and never a build step: its output is committed
# under src/sites/<site>/generated/. What is pinned here is the COMPILER, so two
# machines regenerating produce the same bytes. Nothing pinned it before, and a
# different matiec silently rewrites every POU.
#
# Nothing here enters ALL and nothing is required: without flex and bison the
# targets are simply not declared, as `clang-tidy` and `check-opta` are not. A
# tree without them still gets a green `make check`.
#
# LICENCE: matiec is GPLv3. None of it reaches the firmware - we run the BINARY,
# and the IEC library its output is compiled against is OURS (st/lib/ieclib.txt,
# passed as -I). st/vendor/matiec carries no lib/ at all. See st/README.md.

set(BOTANIK_MATIEC_SRC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/matiec" CACHE PATH
    "matiec source tree (default: the pinned copy under st/vendor/)")

set(_matiec_bin "${CMAKE_BINARY_DIR}/matiec")
set(_iec2c      "${_matiec_bin}/iec2c")

# Plain find_program, as DOXYGEN and CLANG_TIDY are: FindFLEX/FindBISON bring a
# pile of variables for generating parsers, which is matiec's own business.
find_program(FLEX       flex)
find_program(BISON      bison)
find_program(AUTORECONF autoreconf)
find_program(GIT_EXE    git)
find_package(Python3 COMPONENTS Interpreter QUIET)

if(NOT EXISTS "${BOTANIK_MATIEC_SRC}/configure.ac")
    message(STATUS "matiec source absent - no ST toolchain targets "
                   "(expected ${BOTANIK_MATIEC_SRC})")
elseif(NOT FLEX OR NOT BISON OR NOT AUTORECONF)
    message(STATUS "flex/bison/autoreconf not all found - no ST toolchain targets")
elseif(NOT Python3_Interpreter_FOUND OR NOT GIT_EXE)
    message(STATUS "python3 or git not found - no ST toolchain targets")
else()

    # OUTPUT-based, so it builds once per tree and never again.
    # `rm -rf build/matiec` forces it, the same move as `rm -rf build/clang-tidy`.
    add_custom_command(OUTPUT "${_iec2c}"
        COMMAND ${CMAKE_COMMAND}
                "-DSRC=${BOTANIK_MATIEC_SRC}" "-DBIN=${_matiec_bin}"
                "-DJOBS=${_nproc}"
                -P "${CMAKE_SOURCE_DIR}/cmake/matiec-build.cmake"
        DEPENDS "${CMAKE_SOURCE_DIR}/cmake/matiec-build.cmake"
                "${BOTANIK_MATIEC_SRC}/configure.ac"
        COMMENT "Building the pinned matiec (rm -rf ${_matiec_bin} to force)"
        USES_TERMINAL VERBATIM)
    add_custom_target(matiec DEPENDS "${_iec2c}")

    # Regenerates the matiec half of the site's generated/, IN PLACE. Opt-in and
    # in no gate, exactly like pytest-update: it rewrites committed files, so
    # review the diff. The other tables.py subcommands need no compiler.
    add_custom_target(st-update
        COMMAND ${CMAKE_COMMAND} -E env "IEC2C=${_iec2c}"
                ${Python3_EXECUTABLE} scripts/tables.py plant ${BOTANIK_SITE}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS "${_iec2c}"
        USES_TERMINAL
        COMMENT "Regenerating ${BOTANIK_SITE}'s ST output - review the diff"
        VERBATIM)

    # Closes the loop the way check-bare does for the OFF paths: regenerate with
    # the PINNED matiec into a COPY of the site, then diff against what is
    # committed. A check never writes into the tree it checks.
    #
    # It catches three things nothing else does: a generated file hand-edited, a
    # library .st changed without a regeneration, and a matiec that is not the
    # pinned one. The DIFF is the version assertion, and a stronger one than
    # comparing SHAs: it also catches the same commit built against a different
    # bison.
    set(_st_check "${CMAKE_BINARY_DIR}/st-check")
    add_custom_target(check-st
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_st_check}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_site_dir}" "${_st_check}/site"
        COMMAND ${CMAKE_COMMAND} -E env "IEC2C=${_iec2c}"
                "BOTANIK_SITE_DIR=${_st_check}/site"
                ${Python3_EXECUTABLE} scripts/tables.py plant
        # --no-index so the build tree's ignore rules do not apply, and whole
        # directories so a file matiec STOPPED emitting is caught too.
        COMMAND ${GIT_EXE} diff --no-index --exit-code --stat --
                "${_site_dir}/generated" "${_st_check}/site/generated"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS "${_iec2c}"
        USES_TERMINAL
        COMMENT "Sweep: the committed ST output vs the pinned matiec's"
        VERBATIM)
endif()
