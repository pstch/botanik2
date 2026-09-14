# Every example under examples/, built and tested as its own consumer project.
#
# Each example is configured in build/examples/<name>/ exactly as a site
# repository would be: preamble, project(), botanik_add_site(). That is the
# only build in which botanik is a subdirectory, so it is what catches a
# consumer-path bug.
#
#   make examples          build every example (`check` depends on it)
#   make example-<name>    build one
#
# Two kinds of CTest entry per example, like the library's own suite:
#
#   example:<name>          its own ctest, whole binaries. Unlabelled: the gate
#   example:<name>:<case>   one per case of <name>_tests, labelled
#                           `unit;unit:<name>`
#
# Before an example is built its entries do not exist yet, so a failing
# example:<name>:NOT_BUILT stands in for them.
#
# An example has at most one doctest binary, named <name>_tests. A submodule
# that is not initialised has no CMakeLists.txt and is skipped.

set(_examples_bin "${CMAKE_BINARY_DIR}/examples")
file(MAKE_DIRECTORY "${_examples_bin}")
file(WRITE "${_examples_bin}/not-built.cmake"
    "message(FATAL_ERROR \"example \${NAME} is not built: make -C <build> examples\")\n")

set(_examples_options -DBOTANIK_TARGET=SIMULATOR)
foreach(_var CMAKE_BUILD_TYPE BOTANIK_SANITIZE Python3_EXECUTABLE)
    if(DEFINED ${_var} AND NOT "${${_var}}" STREQUAL "")
        list(APPEND _examples_options "-D${_var}=${${_var}}")
    endif()
endforeach()
string(REPLACE ";" "|" _examples_options_arg "${_examples_options}")

add_custom_target(examples)
file(REAL_PATH "${BOTANIK_ROOT}" _examples_root)
file(GLOB _example_dirs LIST_DIRECTORIES true "${BOTANIK_ROOT}/examples/*")
foreach(_dir IN LISTS _example_dirs)
    if(NOT EXISTS "${_dir}/CMakeLists.txt")
        continue()
    endif()
    get_filename_component(_name "${_dir}" NAME)

    set(_bin "${_examples_bin}/${_name}")
    add_custom_target(example-${_name}
        COMMAND ${CMAKE_COMMAND}
                -DNAME=${_name} -DSRC=${_dir} -DBIN=${_bin} -DROOT=${_examples_root}
                -DGENERATOR=${CMAKE_GENERATOR}
                -DOPTIONS=${_examples_options_arg} -DCTEST=${CMAKE_CTEST_COMMAND}
                -DDOCTEST_ADD=${BOTANIK_ROOT}/tests/vendor/doctestAddTests.cmake
                -P ${BOTANIK_ROOT}/cmake/example-build.cmake
        COMMENT "Example ${_name}: configure, build, list its cases"
        VERBATIM)
    add_dependencies(examples example-${_name})

    set(_include "${_examples_bin}/${_name}-tests.cmake")
    file(WRITE "${_include}"
"if(EXISTS \"${_bin}/botanik-cases.cmake\")
  include(\"${_bin}/botanik-cases.cmake\")
else()
  add_test([==[example:${_name}:NOT_BUILT]==] \"${CMAKE_COMMAND}\" -DNAME=${_name} -P \"${_examples_bin}/not-built.cmake\")
endif()
")
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${_include}")
    list(APPEND _example_names ${_name})
endforeach()
message(STATUS "examples: ${_example_names}")
