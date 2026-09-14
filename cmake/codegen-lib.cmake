# Whether a generated tree still matches its inputs, and the hand-off to the
# generator when it does not.
#
# A generated tree (a site's generated/, or the library's three .gen.h files)
# carries a committed codegen.stamp: the SHA-256 over the name and content of
# every input the generator reads. Answering "does it match" is the one part
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
        file(GLOB _more "${root}/templates/*.j2"
                        "${root}/src/libbotanik/config/*.h"
                        "${root}/src/libbotanik/plc/functions/*.st"
                        "${root}/src/libbotanik/plc/blocks/*.st")
        list(APPEND _lib ${_more}
            "${root}/src/libbotanik/vars/tables/library.csv"
            "${root}/src/libbotanik/plc/functions/ieclib.txt"
            "${root}/src/libbotanik_features.h"
            "${root}/src/libbotanik/codegen_abi.h"
            "${root}/vendor/matiec/UPSTREAM")
        file(GLOB _site "${site}/site.toml" "${site}/tables/*.csv")
        file(GLOB_RECURSE _st "${site}/*.st")
        list(FILTER _st EXCLUDE REGEX "/generated/")
        list(APPEND _site ${_st})
    else()
        list(APPEND _lib
            "${root}/templates/lib_enum.gen.h.j2"
            "${root}/templates/rows_none.gen.h.j2"
            "${root}/src/libbotanik/vars/tables/library.csv"
            "${root}/src/libbotanik/vars/descriptor.h"
            "${root}/src/libbotanik/plc/health.h")
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
if(NOT _have STREQUAL _want)
    # Several build trees can build the same tree at once: every example shares
    # the library's. The second waits, then finds the stamp current.
    file(LOCK "${ROOT}/.codegen.lock" TIMEOUT 900)
    _botanik_codegen_stamp(_have)
endif()
if(_have STREQUAL _want)
    message(STATUS "codegen: ${_what} is up to date")
else()
    set(_why "its inputs changed")
    if(_have STREQUAL "")
        set(_why "no stamp")
    endif()
    message(STATUS "codegen: regenerating ${_what}: ${_why}")
    _botanik_codegen_python("regenerating ${_what}" codegen "${_arg}" ${_want})
endif()

if(DONE)
    get_filename_component(_dir "${DONE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dir}")
    file(TOUCH "${DONE}")
endif()
