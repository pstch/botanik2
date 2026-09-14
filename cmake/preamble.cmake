# What a project embedding botanik includes before its own project() call.
#
#   cmake_minimum_required(VERSION 3.31)
#   include(extern/botanik/cmake/preamble.cmake)
#   project(my_greenhouse C CXX)
#   botanik_add_site(SITE_DIR src)
#
# Toolchain selection has to be here: CMAKE_TOOLCHAIN_FILE must be set before
# the first project(), which runs before botanik is added. -DBOTANIK_TARGET=OPTA
# stays the one knob in both repositories, and a toolchain named on the command
# line wins.

if(NOT DEFINED BOTANIK_TARGET)
    set(BOTANIK_TARGET "SIMULATOR" CACHE STRING
        "Target platform: SIMULATOR or OPTA")
endif()

if("${BOTANIK_TARGET}" STREQUAL "OPTA" AND NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/opta-toolchain.cmake")
endif()

# botanik_add_site(SITE_DIR <dir>)
#   Adds botanik, building the firmware of the site in <dir> (relative to the
#   calling CMakeLists): the library compiled for it, botanik_sim or the OPTA
#   image, the testkit, and the consumer targets.
#
# botanik_add_library()
#   Adds botanik with no site: the library alone, for a consumer reusing its
#   components. Features are still chosen with BOTANIK_FEATURE_* cache entries.
#
# Once per build: the library is compiled with one site's defines. botanik
# refuses to be added by a bare add_subdirectory(), where a misspelled variable
# would build its reference site under this project's name.

function(botanik_add_site)
    cmake_parse_arguments(PARSE_ARGV 0 _arg "" "SITE_DIR" "")
    if(_arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "botanik_add_site: unknown argument(s): ${_arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT _arg_SITE_DIR)
        message(FATAL_ERROR "botanik_add_site: SITE_DIR <dir> is required")
    endif()
    get_filename_component(_dir "${_arg_SITE_DIR}" ABSOLUTE
                           BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT IS_DIRECTORY "${_dir}")
        message(FATAL_ERROR "botanik_add_site: SITE_DIR '${_dir}' is not a directory")
    endif()
    set(BOTANIK_SITE_DIR "${_dir}")
    _botanik_add(botanik_add_site)
endfunction()

function(botanik_add_library)
    if(ARGN)
        message(FATAL_ERROR "botanik_add_library: takes no arguments, got: ${ARGN}")
    endif()
    set(BOTANIK_SITE "NONE")
    set(BOTANIK_SITE_DIR "")
    _botanik_add(botanik_add_library)
endfunction()

# A macro, so botanik is added in the calling function's scope and sees the site
# it set.
macro(_botanik_add caller)
    get_property(_botanik_added GLOBAL PROPERTY BOTANIK_ADDED_BY)
    if(_botanik_added)
        message(FATAL_ERROR "${caller}: botanik was already added by ${_botanik_added}; "
            "one build holds one site")
    endif()
    set_property(GLOBAL PROPERTY BOTANIK_ADDED_BY ${caller})
    get_filename_component(_botanik_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    add_subdirectory("${_botanik_root}" "${CMAKE_CURRENT_BINARY_DIR}/botanik")
endmacro()
