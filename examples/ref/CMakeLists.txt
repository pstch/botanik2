# ref - the reference site, and the smallest one worth reading.
#
# A single vent driven off a single temperature: two field readings, one law,
# one analog output. Every optional feature is on, because this is the site the
# suite compiles and runs, and a feature nothing builds is a feature nothing
# tests.
#
#   cmake -B build && make -C build && ./build/botanik_sim
#
# Copy this directory to start a site of your own. extern/botanik is a symlink
# to the repository root here; a real site carries it as a submodule, and
# nothing else differs.
cmake_minimum_required(VERSION 3.31)

include(extern/botanik/cmake/preamble.cmake)

project(ref C CXX)

botanik_add_site(SITE_DIR src)

# This site's own tests. Host-only: the testkit exists in a SIMULATOR build.
if(TARGET botanik_testkit)
    enable_testing()
    add_subdirectory(tests)
endif()
