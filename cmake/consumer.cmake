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
