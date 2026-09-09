# The library with no site at all, and a consumer that reuses one component.
#
# Its own project(), configured from check-nosite as a nested build: a site is
# chosen once per build, so "no site" cannot be a second configuration of the
# tree that already picked one.
cmake_minimum_required(VERSION 3.21)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/preamble.cmake)
project(botanik_nosite CXX C)

set(BOTANIK_SITE "NONE" CACHE STRING "" FORCE)
add_subdirectory(../.. botanik-build)

# Compiled against the library's own headers only: no generated/ on the include
# path, because there is nothing generated to reach for.
add_executable(consumer consumer.cpp)
target_link_libraries(consumer botanik)
