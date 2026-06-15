# Unit tests (doctest) — host-only, SIMULATOR build.
# ============================================================================
# These tests link against the SIMULATOR-target `botanik` library and run on the
# host, so they are only wired up from the SIMULATOR branch of the top-level
# CMakeLists. doctest ships as a single vendored header (tests/vendor/doctest.h);
# there is nothing to fetch or build.

add_executable(botanik_tests
    doctest_main.cpp
    test_text.cpp
    test_time.cpp
    test_sizes.cpp
)

# vendor/ for doctest.h; src/ for the library and the simulator's site payloads.
target_include_directories(botanik_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(botanik_tests botanik)

# Register with CTest, one entry per TEST_CASE. doctest_discover_tests runs the
# binary at build time with --list-test-cases and emits an add_test() per case,
# so `ctest` and VS Code's Testing panel show/run/debug each case individually
# (rather than the whole binary as a single pass/fail). The module + its helper
# are vendored next to doctest.h; no fetch or install needed.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
include(doctest)
doctest_discover_tests(botanik_tests)

# `make check` is the one-shot pre-push gate. Its dependencies build everything
# the suite needs (`make test`/`ctest` do not) plus the two static gates, then the
# command runs the suite with failures echoed inline:
#   - botanik_tests : the doctest binary (links the normal library)
#   - botanik_strict : warning-clean compile gate — the -Werror mirror fails the
#                      build on any compiler warning
#   - clang-tidy     : bugprone static analysis, findings promoted to errors
# clang-tidy is added conditionally since the target only exists when
# run-clang-tidy was found at configure time.
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS botanik_tests botanik_strict
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Pre-push gate: warning-clean build + clang-tidy + unit tests"
    VERBATIM
)
if(TARGET clang-tidy)
    add_dependencies(check clang-tidy)
endif()
