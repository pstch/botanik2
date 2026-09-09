# What a SITE repository tests its own site with.
#
# Available in every build that has a site, embedded or not, because a site
# repository never builds tests/ - the suite in there is botanik's own.
#
#   botanik_testkit    an INTERFACE target: doctest, the generic fixtures, and
#                      the library, with the selected site already wired in
#   BOTANIK_TEST_MAIN  doctest's main, to compile beside your own cases
#
# In a site repository:
#
#   add_executable(site_tests ${BOTANIK_TEST_MAIN} tests/test_laws.cpp
#                             ${BOTANIK_SITE_SOURCES})
#   target_link_libraries(site_tests botanik_testkit)
#
# The fixtures it puts on the include path are the ones written against no site
# in particular: site_bank.h (a bank over whatever table is selected),
# live_fixture.h (a running firmware), scenario_trace.h, ram_store.h,
# term_fixture.h. A fixture that names a site's own rows belongs to that site.

add_library(botanik_testkit INTERFACE)
target_include_directories(botanik_testkit INTERFACE
    "${BOTANIK_ROOT}/tests"
    "${BOTANIK_ROOT}/tests/vendor")
target_link_libraries(botanik_testkit INTERFACE botanik)

set(BOTANIK_TEST_MAIN "${BOTANIK_ROOT}/tests/doctest_main.cpp"
    CACHE INTERNAL "doctest's main, for a site's own test binary")
