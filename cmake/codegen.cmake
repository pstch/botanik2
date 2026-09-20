# The generator, as part of every build. Included from the top-level
# CMakeLists once the site is resolved, for both targets and whether botanik
# is the project or a consumer's subdirectory.
#
# One CODEGEN custom command per generated tree - the library's own generated
# set, and the site's generated/ - depending on every input the generator
# reads, and on the output it last wrote. The generated files themselves are committed sources, not OUTPUTs: a
# Makefile `make clean` deletes outputs, and a clean clone must build without
# the tools. So the OUTPUT is a stamp in the build tree, and the command only
# regenerates when the committed codegen.stamp no longer matches the inputs
# (see cmake/codegen-lib.cmake). When it matches, running it needs no tools.
# matiec is built by the generator itself, on first use, for a site with laws.
#
#   make codegen           CMake's builtin target: these commands, nothing else
#   make matiec            build the pinned compiler into vendor/matiec/build/
#   make check-generated   regenerate every example into a scratch copy and diff
#
# Sets _codegen_site_config, _codegen_site_rows and _codegen_site_sources for
# the CMakeLists, which cannot look for files a build has not written yet.

include(${CMAKE_CURRENT_LIST_DIR}/codegen-lib.cmake)

find_package(Python3 COMPONENTS Interpreter QUIET)
set(_codegen_python "")
if(Python3_Interpreter_FOUND)
    set(_codegen_python "${Python3_EXECUTABLE}")
endif()

set(_codegen_site "")
if(_site_dir AND EXISTS "${_site_dir}/site.toml")
    set(_codegen_site "${_site_dir}")
endif()

# A NEW table, ST file or template has to reach the DEPENDS below, which only a
# re-configure does: these globs make the build re-run CMake when one appears.
file(GLOB _codegen_globbed CONFIGURE_DEPENDS
     "${BOTANIK_ROOT}/scripts/sitegen/*.py" "${BOTANIK_ROOT}/templates/*.j2"
     "${BOTANIK_ROOT}/src/libbotanik/plc/functions/*.st"
     "${BOTANIK_ROOT}/src/libbotanik/plc/blocks/*.st")
if(_codegen_site)
    file(GLOB _codegen_globbed CONFIGURE_DEPENDS "${_codegen_site}/tables/*.csv")
    file(GLOB_RECURSE _codegen_globbed CONFIGURE_DEPENDS "${_codegen_site}/*.st")
endif()

set(_codegen_dir "${CMAKE_CURRENT_BINARY_DIR}/codegen")
set(_codegen_script "${BOTANIK_ROOT}/cmake/codegen-lib.cmake")

function(_botanik_codegen_command tree site done)
    botanik_codegen_inputs(_pairs "${BOTANIK_ROOT}" "${site}")
    set(_deps "${_codegen_script}")
    foreach(_p IN LISTS _pairs)
        string(REGEX REPLACE "^[^|]*\\|" "" _f "${_p}")
        list(APPEND _deps "${_f}")
    endforeach()
    # ...and its own output, so a hand edit is a reason to re-run. The script
    # touches ${done} last, so it always ends newer than what it just wrote.
    botanik_codegen_outputs(_outs "${BOTANIK_ROOT}" "${site}")
    list(APPEND _deps ${_outs})
    add_custom_command(OUTPUT "${done}"
        COMMAND ${CMAKE_COMMAND} -DROOT=${BOTANIK_ROOT} -DSITE=${site}
                -DPYTHON=${_codegen_python} -DDONE=${done}
                -P ${_codegen_script}
        DEPENDS ${_deps}
        CODEGEN
        COMMENT "Checking the generated sources of ${tree}"
        VERBATIM)
endfunction()

set(_codegen_done "${_codegen_dir}/library.done")
_botanik_codegen_command("the library" "" "${_codegen_dir}/library.done")
if(_codegen_site)
    _botanik_codegen_command("the site" "${_codegen_site}" "${_codegen_dir}/site.done")
    list(APPEND _codegen_done "${_codegen_dir}/site.done")
endif()

# Every target compiling a generated file links the library, so ordering the
# library after these orders all of them.
add_custom_target(botanik_codegen DEPENDS ${_codegen_done})
add_dependencies(botanik botanik_codegen)

# What the generator writes, named from the inputs rather than looked for on
# disk: the first build of an edited site has not written it yet. plant.cpp
# exists only for a site with laws, which programs.csv says, so a change to it
# re-runs CMake.
set(_codegen_site_config "")
set(_codegen_site_rows "")
set(_codegen_site_sources "")
if(_codegen_site)
    set(_codegen_site_config "${_codegen_site}/generated/config.gen.h")
    set(_codegen_site_rows "${_codegen_site}/generated/rows.gen.h")
    list(APPEND _codegen_site_sources "${_codegen_site}/generated/vars.gen.cpp")
    set(_codegen_programs "${_codegen_site}/tables/programs.csv")
    if(EXISTS "${_codegen_programs}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_codegen_programs}")
        # A header and at least one row.
        file(STRINGS "${_codegen_programs}" _codegen_rows REGEX "[^ \t,]" LIMIT_COUNT 2)
        list(LENGTH _codegen_rows _codegen_rows)
        if(_codegen_rows EQUAL 2)
            list(APPEND _codegen_site_sources "${_codegen_site}/generated/plant.cpp")
        endif()
    endif()
    foreach(_f IN LISTS _codegen_site_sources)
        if(NOT EXISTS "${_f}")
            set_source_files_properties("${_f}" PROPERTIES GENERATED TRUE)
        endif()
    endforeach()
endif()

add_custom_target(matiec
    COMMAND ${CMAKE_COMMAND} -DSRC=${BOTANIK_ROOT}/vendor/matiec
            -P ${BOTANIK_ROOT}/cmake/matiec-build.cmake
    USES_TERMINAL
    COMMENT "Building the pinned matiec into vendor/matiec/build/"
    VERBATIM)

# botanik's own sweep: the examples are botanik's to keep generated.
if(PROJECT_IS_TOP_LEVEL)
    add_custom_target(check-generated
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/genchk
        USES_TERMINAL
        COMMENT "Sweep: every example's committed generated sources match a fresh run"
        VERBATIM)
    file(GLOB _codegen_tomls ${BOTANIK_ROOT}/examples/*/src/site.toml)
    foreach(_t IN LISTS _codegen_tomls)
        get_filename_component(_s "${_t}" DIRECTORY)
        add_custom_command(TARGET check-generated POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DROOT=${BOTANIK_ROOT} -DSITE=${_s}
                    -DPYTHON=${_codegen_python} -DCHECK=${CMAKE_BINARY_DIR}/genchk
                    -P ${_codegen_script}
            VERBATIM)
    endforeach()
endif()
