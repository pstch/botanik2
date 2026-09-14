# The targets a SITE repository gets, and botanik's own suite does not define
# for it.
#
# tests/ is botanik's: its cases are about the library, and a site repository
# never builds it. But the two gates a site actually needs are not tests of the
# library at all - they are "does my site still work" and "does my firmware
# still link" - so they are declared here, against the CONSUMING project.
#
#   make check       build everything, then run this project's own ctest cases
#   make check-opta  the cross build, which is where a link error lives
#   make docs        the site's manual, with botanik's under it, in build/docs/html
#
# Top-level builds skip this file: botanik declares richer versions of both in
# tests/, and two targets of one name is a configure error.

if(PROJECT_IS_TOP_LEVEL)
    return()
endif()

include(ProcessorCount)
ProcessorCount(_consumer_nproc)
if(_consumer_nproc EQUAL 0)
    set(_consumer_nproc 4)
endif()

# ctest never builds anything and CMake's own `test` target cannot be given
# dependencies, so a site's test binary would be run stale, or not at all.
add_custom_target(check
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --parallel ${_consumer_nproc}
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR}
            --output-on-failure
    USES_TERMINAL
    COMMENT "Gate: this project's own cases, against a freshly built site"
    VERBATIM)

# The OPTA image, because a link error is invisible to every host target:
# Arduino.h declares the whole wiring API and libmbed.a defines none of it, so a
# site reaching for a call the build does not compile fails at link time on the
# OPTA alone, with `check` green throughout.
find_program(_consumer_arm arm-none-eabi-g++)
if(_consumer_arm)
    add_custom_target(check-opta
        COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR} -S ${CMAKE_SOURCE_DIR}
                -B ${CMAKE_SOURCE_DIR}/build-opta -DBOTANIK_TARGET=OPTA
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_SOURCE_DIR}/build-opta
                --parallel ${_consumer_nproc}
        USES_TERMINAL
        COMMENT "Sweep: the OPTA cross build of this site's firmware"
        VERBATIM)
else()
    message(STATUS "check-opta: no arm-none-eabi-g++, skipping the cross build")
endif()

# The site's manual: its docs/ and src/, with the library's pages and sources in
# the same run so a site page can @ref one of botanik's. botanik's Doxyfile, with
# every path in it re-pointed, and the site's index as the main page.
find_program(_consumer_doxygen doxygen)
get_filename_component(_consumer_root "${_site_dir}" DIRECTORY)
if(_consumer_doxygen AND EXISTS "${_consumer_root}/docs/index.md")
    file(GLOB_RECURSE _consumer_images LIST_DIRECTORIES true "${_consumer_root}/docs/*")
    list(FILTER _consumer_images INCLUDE REGEX "^[^.]*$")
    set(_consumer_image_path "")
    foreach(_d "${_consumer_root}/docs" ${_consumer_images})
        if(IS_DIRECTORY "${_d}")
            string(APPEND _consumer_image_path " \\\n    \"${_d}\"")
        endif()
    endforeach()
    foreach(_d images images/logic docs/integration/diagrams docs/dev/foundations
               docs/dev/runtime docs/dev/console docs/dev/field)
        string(APPEND _consumer_image_path " \\\n    \"${BOTANIK_ROOT}/${_d}\"")
    endforeach()
    file(WRITE "${CMAKE_BINARY_DIR}/Doxyfile"
"@INCLUDE               = \"${BOTANIK_ROOT}/Doxyfile\"
PROJECT_NAME           = \"${CMAKE_PROJECT_NAME}\"
PROJECT_NUMBER         =
PROJECT_BRIEF          = \"A Botanik site\"
PROJECT_LOGO           = \"${BOTANIK_ROOT}/images/logo-small.png\"
OUTPUT_DIRECTORY       = \"${CMAKE_BINARY_DIR}/docs\"
INPUT                  = \"${_consumer_root}/docs\" \"${_site_dir}\" \"${BOTANIK_ROOT}/docs\" \"${BOTANIK_ROOT}/src\"
USE_MDFILE_AS_MAINPAGE = \"${_consumer_root}/docs/index.md\"
IMAGE_PATH             =${_consumer_image_path}
EXAMPLE_PATH           = \"${_consumer_root}/docs/generated/screens\" \"${BOTANIK_ROOT}/docs/generated/screens\"
EXCLUDE                = \"${BOTANIK_ROOT}/docs/design\" \"${BOTANIK_ROOT}/docs/doxygen\"
STRIP_FROM_PATH        = \"${_consumer_root}\" \"${BOTANIK_ROOT}\"
STRIP_FROM_INC_PATH    = \"${BOTANIK_ROOT}/src\"
INCLUDE_PATH           = \"${BOTANIK_ROOT}/src\"
WARN_LOGFILE           = \"${CMAKE_BINARY_DIR}/docs/doxygen-warnings.log\"
")
    add_custom_target(docs
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/docs
        COMMAND ${_consumer_doxygen} ${CMAKE_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${_consumer_root}
        COMMENT "Rendering the site's manual into ${CMAKE_BINARY_DIR}/docs/html"
        VERBATIM)
    # The manual documents generated pages, so it renders them current.
    add_dependencies(docs botanik_codegen)
endif()
