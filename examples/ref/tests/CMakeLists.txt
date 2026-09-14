# What the variable model does with THIS site's table under it.
#
# Not the library's own suite - that lives in the submodule and names no site's
# rows. These need a real table, so they belong to the site that provides one.
#
# botanik_testkit carries doctest, the generic fixtures and the library with
# this site already wired in; BOTANIK_TEST_MAIN is doctest's main, compiled
# beside these cases. Both come from cmake/testkit.cmake.

add_executable(ref_tests
    ${BOTANIK_TEST_MAIN}
    health.cpp
    shell_vars.cpp
    table_behaviour.cpp
    vars.cpp)

target_include_directories(ref_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ref_tests botanik_testkit)

add_test(NAME ref_tests COMMAND ref_tests)

# The screens this site's table decides, over the library's pyte harness.
botanik_add_pyte_tests(ref ${CMAKE_CURRENT_SOURCE_DIR}/pyte)
