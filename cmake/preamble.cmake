# Toolchain selection, for a site repository that embeds botanik.
#
# CMAKE_TOOLCHAIN_FILE has to be set before the first project() call, and a site
# repo's project() runs before its add_subdirectory(extern/botanik). So the
# choice cannot live in botanik's own CMakeLists.txt for that build: the site
# includes this file first instead.
#
#   cmake_minimum_required(VERSION 3.21)
#   include(extern/botanik/cmake/preamble.cmake)
#   project(my_greenhouse C CXX)
#
# Keeps -DBOTANIK_TARGET=OPTA as the one knob in both repositories. A toolchain
# named on the command line wins, so a site can still cross-compile some other
# way without editing anything.

if(NOT DEFINED BOTANIK_TARGET)
    set(BOTANIK_TARGET "SIMULATOR" CACHE STRING
        "Target platform: SIMULATOR or OPTA")
endif()

if("${BOTANIK_TARGET}" STREQUAL "OPTA" AND NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/opta-toolchain.cmake")
endif()
