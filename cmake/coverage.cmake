# Line coverage of the library, read off the gcda a coverage build left behind.
# Reports, never fails: what it is for is noticing a fall, and the per-area
# table says where.
#
# Two sources this deliberately does not use, each of which gives a badly wrong
# answer:
#
#  - `gcov -n`'s per-file summary counts a header's inline bodies against
#    whichever .cpp pulled them in.
#  - Headers themselves: gcov attributes a header's inline code to one
#    translation unit and reports every other TU that included it as zero.
#
# So: -p to write the per-source .gcov, then count the annotated lines. Without
# -p the paths are not mangled into the name and `shell/shell.cpp` collides with
# `pages/shell.cpp`.
#
# Invoked as: cmake -DGCOV=... -DTREE=... -DSRC=... -DOUT=... -P cmake/coverage.cmake

file(GLOB_RECURSE _gcda "${TREE}/*.gcda")
list(FILTER _gcda INCLUDE REGEX "/libbotanik/")
if(NOT _gcda)
    message(STATUS "coverage: no profile data under ${TREE} - was the suite run?")
    return()
endif()

set(_work "${TREE}/gcov")
file(REMOVE_RECURSE "${_work}")
file(MAKE_DIRECTORY "${_work}")
execute_process(COMMAND ${GCOV} -p -r -s ${SRC} ${_gcda}
                WORKING_DIRECTORY "${_work}"
                OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(STATUS "coverage: ${GCOV} failed")
    return()
endif()

# The mangled name carries the path with '#' for '/', so the glob filters to
# library .cpp.
file(GLOB _reports "${_work}/*#libbotanik#*.cpp.gcov")

set(_areas "")
set(_total_hit 0)
set(_total_all 0)

foreach(_report IN LISTS _reports)
    # An annotated line starts with its execution count, or with ##### for one
    # never reached. A '-' line and the header block carry no coverage and stay
    # out of the denominator.
    file(STRINGS "${_report}" _all_lines REGEX "^ *(#####|=====|[0-9]+\\*?) *: *[1-9]")
    file(STRINGS "${_report}" _hit_lines REGEX "^ *[0-9]+\\*? *: *[1-9]")
    list(LENGTH _all_lines _all)
    list(LENGTH _hit_lines _hit)
    if(_all EQUAL 0)
        continue()
    endif()

    string(REGEX REPLACE "^.*#libbotanik#" "" _rest "${_report}")
    if(_rest MATCHES "^([^#]+)#")
        set(_area "${CMAKE_MATCH_1}")
    else()
        set(_area "(root)")
    endif()

    math(EXPR _total_hit "${_total_hit} + ${_hit}")
    math(EXPR _total_all "${_total_all} + ${_all}")
    if(NOT DEFINED _hit_${_area})
        set(_hit_${_area} 0)
        set(_all_${_area} 0)
        list(APPEND _areas "${_area}")
    endif()
    math(EXPR _hit_${_area} "${_hit_${_area}} + ${_hit}")
    math(EXPR _all_${_area} "${_all_${_area}} + ${_all}")
endforeach()

if(_total_all EQUAL 0)
    message(STATUS "coverage: nothing matched src/libbotanik/**.cpp")
    return()
endif()

list(SORT _areas)
set(_json "")
message(STATUS "coverage: library .cpp lines, from ${TREE}")
foreach(_area IN LISTS _areas)
    math(EXPR _pct "(${_hit_${_area}} * 1000) / ${_all_${_area}}")
    math(EXPR _whole "${_pct} / 10")
    math(EXPR _tenth "${_pct} % 10")
    string(LENGTH "${_area}" _len)
    math(EXPR _pad "13 - ${_len}")
    string(REPEAT " " ${_pad} _sp)
    message(STATUS "  ${_area}${_sp}${_whole}.${_tenth}%"
                   "  (${_hit_${_area}} / ${_all_${_area}})")
    string(APPEND _json "    \"${_area}\": { \"covered\": ${_hit_${_area}}, "
                        "\"lines\": ${_all_${_area}} },\n")
endforeach()

math(EXPR _tpct "(${_total_hit} * 1000) / ${_total_all}")
math(EXPR _twhole "${_tpct} / 10")
math(EXPR _ttenth "${_tpct} % 10")
message(STATUS "coverage: TOTAL ${_twhole}.${_ttenth}% "
               "(${_total_hit} / ${_total_all})")

if(OUT)
    string(REGEX REPLACE ",\n$" "\n" _json "${_json}")
    file(WRITE ${OUT}
         "{\n  \"covered\": ${_total_hit},\n  \"lines\": ${_total_all},\n"
         "  \"pct\": \"${_twhole}.${_ttenth}\",\n  \"areas\": {\n${_json}  }\n}\n")
endif()
