# The generator, as part of every build. Included from the top-level
# CMakeLists once the site is resolved, for both targets and whether botanik
# is the project or a consumer's subdirectory.
#
# Runs at CONFIGURE time, because configure reads what it writes (config.gen.h,
# rows.gen.h, the site's .cpp list). Every input is a configure dependency, so
# editing a table or an ST file re-runs CMake on the next build, and the stamp
# decides whether anything is regenerated. See cmake/codegen-lib.cmake.
#
#   make codegen           the same, on demand
#   make matiec            build the pinned compiler into vendor/matiec/build/
#   make check-generated   regenerate every example into a scratch copy and diff

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

# Globbed with CONFIGURE_DEPENDS as well, so a NEW table or ST file re-runs
# CMake too, not only an edit to a known one.
file(GLOB _codegen_globbed CONFIGURE_DEPENDS
     "${BOTANIK_ROOT}/scripts/sitegen/*.py" "${BOTANIK_ROOT}/templates/*.j2")
if(_codegen_site)
    file(GLOB _codegen_globbed CONFIGURE_DEPENDS "${_codegen_site}/tables/*.csv")
    file(GLOB_RECURSE _codegen_globbed CONFIGURE_DEPENDS "${_codegen_site}/*.st")
endif()
botanik_codegen_inputs(_codegen_pairs "${BOTANIK_ROOT}" "")
if(_codegen_site)
    botanik_codegen_inputs(_codegen_site_pairs "${BOTANIK_ROOT}" "${_codegen_site}")
    list(APPEND _codegen_pairs ${_codegen_site_pairs})
endif()
foreach(_p IN LISTS _codegen_pairs)
    string(REGEX REPLACE "^[^|]*\\|" "" _f "${_p}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_f}")
endforeach()

botanik_codegen("${BOTANIK_ROOT}" "" "${_codegen_python}")
if(_codegen_site)
    botanik_codegen("${BOTANIK_ROOT}" "${_codegen_site}" "${_codegen_python}")
endif()

# CMake's builtin `codegen` target drives it, and so does every build, as a
# dependency of the library: ~30 ms of hashing when current. The output is never
# written, so the command runs each time.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/codegen.always
    COMMAND ${CMAKE_COMMAND} -DROOT=${BOTANIK_ROOT} -DSITE=${_codegen_site}
            -DPYTHON=${_codegen_python} -P ${BOTANIK_ROOT}/cmake/codegen-lib.cmake
    CODEGEN
    VERBATIM)
add_custom_target(botanik_codegen DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/codegen.always)
add_dependencies(botanik botanik_codegen)

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
                    -P ${BOTANIK_ROOT}/cmake/codegen-lib.cmake
            VERBATIM)
    endforeach()
endif()
