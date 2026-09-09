# demo - a site with every optional feature turned on.
#
# The worked full profile: a greenhouse's worth of rows, 28 control programs in
# ST, the field bus, the network stack and every console page. It is what the
# suite is pointed at, so the breadth of the library stays exercised, and what
# a new site is compared against when the question is "how is this normally
# done".
#
#   cmake -B build && make -C build && ./build/botanik_sim
#
# extern/botanik is a symlink to the repository root, so this builds in place.
# A real site carries it as a submodule instead; nothing else differs.
cmake_minimum_required(VERSION 3.21)

include(extern/botanik/cmake/preamble.cmake)

project(demo C CXX)

set(BOTANIK_SITE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_subdirectory(extern/botanik botanik-build)
