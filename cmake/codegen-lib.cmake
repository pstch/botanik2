# Whether a generated tree still matches its inputs, and the hand-off to the
# generator when it does not.
#
# A generated tree (a site's generated/, or the library's own generated set)
# carries a committed codegen.stamp: the SHA-256 over the name and content of
# every input the generator reads, and then one hash per file it WROTE - the
# input hash alone cannot see a hand edit to the output, since the generator is
# still reading the same tables. Answering "does it match" is the one part
# that must not need python, so a clean clone builds without it. On a mismatch,
# `tables.py codegen` regenerates and writes the stamp handed to it; without
# python that is a hard error, never a build of stale output.
#
# Included by cmake/codegen.cmake for the input list, and run by its commands:
#
#   cmake -DROOT=<botanik> [-DSITE=<site dir>] [-DPYTHON=<python3>]
#         [-DDONE=<file>] [-DCHECK=<scratch dir>] -P codegen-lib.cmake
#
# SITE empty is the library's tree. DONE is touched once the tree is current.
# CHECK regenerates SITE into a copy under <scratch dir> and diffs it instead.

# The inputs, as "<name>|<absolute path>", sorted by the name they are hashed
# under. SITE empty is the library's own tree (`tables.py library`).
function(botanik_codegen_inputs out root site)
    file(GLOB _lib "${root}/scripts/sitegen/*.py")
    set(_site)
    if(site)
        # config/ is NOT globbed: nothing parses a config header any more, so
        # only the one the LIBRARY generates is an input here - it carries the
        # capacities a row's cap cell names, and moving one moves the pool.
        file(GLOB _more "${root}/templates/*.j2"
                        "${root}/src/libbotanik/plc/functions/*.st"
                        "${root}/src/libbotanik/plc/blocks/*.st")
        list(APPEND _lib ${_more}
            "${root}/src/libbotanik/config/capacities.gen.h"
            "${root}/src/libbotanik/tables/rows.csv"
            "${root}/src/libbotanik/plc/functions/ieclib.txt"
            "${root}/src/libbotanik.toml"
            "${root}/vendor/matiec/UPSTREAM")
        file(GLOB _site "${site}/site.toml" "${site}/tables/*.csv")
        file(GLOB_RECURSE _st "${site}/*.st")
        list(FILTER _st EXCLUDE REGEX "/generated/")
        list(APPEND _site ${_st})
    else()
        list(APPEND _lib
            "${root}/templates/lib_enum.gen.h.j2"
            "${root}/templates/rows_none.gen.h.j2"
            "${root}/templates/codegen_abi.h.j2"
            "${root}/templates/capacities.gen.h.j2"
            "${root}/templates/lib_enum.h.j2"
            "${root}/templates/features.h.j2"
            "${root}/src/libbotanik.toml"
            "${root}/src/libbotanik/tables/enums.csv"
            "${root}/src/libbotanik/plc/functions/quality.st"
            "${root}/src/libbotanik/plc/functions/health.st"
            "${root}/src/libbotanik/tables/rows.csv")
    endif()

    set(_pairs)
    foreach(_f IN LISTS _lib)
        file(RELATIVE_PATH _rel "${root}" "${_f}")
        list(APPEND _pairs "botanik/${_rel}|${_f}")
    endforeach()
    foreach(_f IN LISTS _site)
        file(RELATIVE_PATH _rel "${site}" "${_f}")
        list(APPEND _pairs "site/${_rel}|${_f}")
    endforeach()
    list(SORT _pairs)
    set(${out} ${_pairs} PARENT_SCOPE)
endfunction()

# The generated files a hand edit is plausible in, as absolute paths, filtered
# to the ones that exist: they are DEPENDS of the codegen command, so touching
# one re-runs the script, which then verifies every file the stamp lists. A
# tree that has never been generated has none, and the missing stamp covers it.
function(botanik_codegen_outputs out root site)
    if(site)
        set(_f "${site}/generated/config.gen.h" "${site}/generated/rows.gen.h"
               "${site}/generated/vars.gen.cpp" "${site}/generated/vars.gen.h"
               "${site}/generated/plant.cpp")
    else()
        set(_f "${root}/src/libbotanik/codegen_abi.h"
               "${root}/src/libbotanik/config/capacities.gen.h"
               "${root}/src/libbotanik/core/health.h"
               "${root}/src/libbotanik/plc/runtime/health.gen.h"
               "${root}/src/libbotanik/plc/runtime/quality.gen.h"
               "${root}/src/libbotanik/vars/quality.h"
               "${root}/src/libbotanik/vars/rows_none.gen.h"
               "${root}/src/libbotanik_features.h")
    endif()
    set(_have)
    foreach(_x IN LISTS _f)
        if(EXISTS "${_x}")
            list(APPEND _have "${_x}")
        endif()
    endforeach()
    set(${out} ${_have} PARENT_SCOPE)
endfunction()

if(NOT CMAKE_SCRIPT_MODE_FILE)
    return()
endif()

# Names, not absolute paths, go into the hash: two checkouts agree.
function(_botanik_codegen_hash out)
    botanik_codegen_inputs(_pairs "${ROOT}" "${SITE}")
    set(_acc "")
    foreach(_p IN LISTS _pairs)
        string(REPLACE "|" ";" _p "${_p}")
        list(GET _p 1 _file)
        file(SHA256 "${_file}" _h)
        list(GET _p 0 _name)
        string(APPEND _acc "${_h}  ${_name}\n")
    endforeach()
    string(SHA256 _digest "${_acc}")
    set(${out} ${_digest} PARENT_SCOPE)
endfunction()

function(_botanik_codegen_stamp out)
    set(_s "")
    if(EXISTS "${_stamp}")
        file(STRINGS "${_stamp}" _s REGEX "^[0-9a-f]+$" LIMIT_COUNT 1)
    endif()
    set(${out} "${_s}" PARENT_SCOPE)
endfunction()

# Whether every file the stamp lists still hashes to what it listed. The input
# hash cannot see a hand edit to the OUTPUT: it is the same generator reading
# the same tables, so it matches while the file on disk says something else.
# Names are relative to the tree's root, so two checkouts agree.
function(_botanik_codegen_outputs_ok out)
    set(${out} FALSE PARENT_SCOPE)
    set(_botanik_codegen_edited "" PARENT_SCOPE)
    if(NOT EXISTS "${_stamp}")
        return()
    endif()
    if(SITE)
        get_filename_component(_root "${SITE}" DIRECTORY)
    else()
        set(_root "${ROOT}")
    endif()
    file(STRINGS "${_stamp}" _lines REGEX "^[0-9a-f]+  ")
    if(NOT _lines)
        return()   # a stamp from before the output list: regenerate once
    endif()
    foreach(_l IN LISTS _lines)
        string(SUBSTRING "${_l}" 0 64 _want)
        string(SUBSTRING "${_l}" 66 -1 _name)
        if(NOT EXISTS "${_root}/${_name}")
            return()
        endif()
        file(SHA256 "${_root}/${_name}" _have)
        if(NOT _have STREQUAL _want)
            set(_botanik_codegen_edited "${_name}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out} TRUE PARENT_SCOPE)
endfunction()

# tables.py <args>.
function(_botanik_codegen_python what)
    set(_python "${PYTHON}")
    if(NOT _python)
        find_program(_python python3)
    endif()
    if(NOT _python)
        message(FATAL_ERROR "codegen: ${what} needs python3")
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env CMAKE=${CMAKE_COMMAND}
                ${_python} ${ROOT}/scripts/sitegen/tables.py ${ARGN}
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "codegen: ${what} failed")
    endif()
endfunction()

get_filename_component(ROOT "${ROOT}" ABSOLUTE)
if(SITE)
    set(_what "site ${SITE}")
    set(_stamp "${SITE}/generated/codegen.stamp")
    set(_arg "${SITE}")
else()
    set(_what "the library")
    set(_stamp "${ROOT}/src/libbotanik/codegen.stamp")
    set(_arg "-")
endif()
_botanik_codegen_hash(_want)

if(CHECK)
    _botanik_codegen_python("check-generated" check "${SITE}" ${_want} "${CHECK}")
    return()
endif()

_botanik_codegen_stamp(_have)
_botanik_codegen_outputs_ok(_outs)
if(NOT _have STREQUAL _want OR NOT _outs)
    # Several build trees can build the same tree at once: every example shares
    # the library's. The second waits, then finds the stamp current.
    file(LOCK "${ROOT}/.codegen.lock" TIMEOUT 900)
    _botanik_codegen_stamp(_have)
    _botanik_codegen_outputs_ok(_outs)
endif()
if(_have STREQUAL _want AND _outs)
    message(STATUS "codegen: ${_what} is up to date")
else()
    set(_why "its inputs changed")
    if(_have STREQUAL "")
        set(_why "no stamp")
    elseif(_botanik_codegen_edited)
        set(_why "${_botanik_codegen_edited} was edited by hand")
    elseif(_have STREQUAL _want)
        set(_why "a generated file is missing")
    endif()
    message(STATUS "codegen: regenerating ${_what}: ${_why}")
    _botanik_codegen_python("regenerating ${_what}" codegen "${_arg}" ${_want})
endif()

if(DONE)
    get_filename_component(_dir "${DONE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dir}")
    file(TOUCH "${DONE}")
endif()
