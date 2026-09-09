# nosite - libbotanik with no site at all.
#
# A consumer that wants components rather than a firmware: the console, its
# shell and the task view, constructed directly and configured through each
# one's own set_config(). No variable table, no control programs, no
# BotanikFirmware, and so no site directory.
#
#   cmake -B build && make -C build && ./build/nosite
#
# -DBOTANIK_SITE=NONE is what makes it a library-only build: no simulator, no
# suite, and every library row name resolving through vars/rows_none.gen.h to
# VAR_NONE and so to the bank's inert sink.
cmake_minimum_required(VERSION 3.21)

include(extern/botanik/cmake/preamble.cmake)

project(nosite C CXX)

set(BOTANIK_SITE "NONE" CACHE STRING "" FORCE)

# The task view is not on by default; the shell rides on CONSOLE, which is.
set(BOTANIK_FEATURE_TASKS_PAGE ON CACHE STRING "" FORCE)

add_subdirectory(extern/botanik botanik-build)

add_executable(nosite src/main.cpp)
target_link_libraries(nosite botanik)
