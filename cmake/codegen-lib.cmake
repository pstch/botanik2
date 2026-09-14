# The generator, run by the build: what it reads, the stamp that says whether
# the committed output still matches, and the run itself.
#
# A generated tree (a site's generated/, or the library's three .gen.h files)
# carries a committed codegen.stamp: the SHA-256 over the name and content of
# every input the generator reads. When it matches, nothing runs and nothing is
# required, so a clean clone builds with no python and no matiec. When it does
# not, the generator runs and rewrites the stamp; without its tools that is a
# hard error, never a build of stale output.
#
# Included by cmake/codegen.cmake for the input list, and run with -P by its
# custom commands and by `check-generated`:
#
#   cmake -DROOT=<botanik> [-DSITE=<site dir>] [-DPYTHON=<python3>]
#         [-DDONE=<file>] [-DCHECK=<scratch dir>] -P codegen-lib.cmake
#
# SITE empty is the library's tree. DONE is touched once the tree is current.
# CHECK regenerates SITE into a copy under <scratch dir> and diffs it against
# what is committed, touching nothing.

# The inputs, absolute, sorted by the name they are hashed under. SITE empty is
# the library's own codegen (`tables.py library`).
function(botanik_codegen_inputs out root site)
    set(_lib)
    if(site)
        file(GLOB _g "${root}/scripts/sitegen/*.py" "${root}/templates/*.j2"
                     "${root}/src/libbotanik/config/*.h"
                     "${root}/src/libbotanik/plc/functions/*.st"
                     "${root}/src/libbotanik/plc/blocks/*.st")
        list(APPEND _lib ${_g}
            "${root}/src/libbotanik/vars/tables/library.csv"
            "${root}/src/libbotanik/plc/functions/ieclib.txt"
            "${root}/src/libbotanik_features.h"
            "${root}/src/libbotanik/codegen_abi.h"
            "${root}/vendor/matiec/UPSTREAM")
        file(GLOB _t "${site}/site.toml" "${site}/tables/*.csv")
        file(GLOB_RECURSE _st "${site}/*.st")
        list(FILTER _st EXCLUDE REGEX "/generated/")
        set(_site ${_t} ${_st})
    else()
        file(GLOB _g "${root}/scripts/sitegen/*.py")
        list(APPEND _lib ${_g}
            "${root}/templates/lib_enum.gen.h.j2"
            "${root}/templates/rows_none.gen.h.j2"
            "${root}/src/libbotanik/vars/tables/library.csv"
            "${root}/src/libbotanik/vars/descriptor.h"
            "${root}/src/libbotanik/plc/health.h")
        set(_site)
    endif()

    set(_pairs)
    foreach(_f IN LISTS _lib)
        if(EXISTS "${_f}")
            file(RELATIVE_PATH _rel "${root}" "${_f}")
            list(APPEND _pairs "botanik/${_rel}|${_f}")
        endif()
    endforeach()
    foreach(_f IN LISTS _site)
        file(RELATIVE_PATH _rel "${site}" "${_f}")
        list(APPEND _pairs "site/${_rel}|${_f}")
    endforeach()
    list(SORT _pairs)
    set(${out} ${_pairs} PARENT_SCOPE)
endfunction()

# Names, not absolute paths, go into the hash: two checkouts agree.
function(botanik_codegen_hash out root site)
    botanik_codegen_inputs(_pairs "${root}" "${site}")
    set(_acc "")
    foreach(_p IN LISTS _pairs)
        string(REGEX REPLACE "\\|.*$" "" _name "${_p}")
        string(REGEX REPLACE "^[^|]*\\|" "" _file "${_p}")
        file(SHA256 "${_file}" _h)
        string(APPEND _acc "${_h}  ${_name}\n")
    endforeach()
    string(SHA256 _digest "${_acc}")
    set(${out} ${_digest} PARENT_SCOPE)
endfunction()

function(_botanik_codegen_stamp_path out root site)
    if(site)
        set(${out} "${site}/generated/codegen.stamp" PARENT_SCOPE)
    else()
        set(${out} "${root}/src/libbotanik/codegen.stamp" PARENT_SCOPE)
    endif()
endfunction()

function(_botanik_codegen_stamp_read out path)
    set(_s "")
    if(EXISTS "${path}")
        file(STRINGS "${path}" _s REGEX "^[0-9a-f]+$" LIMIT_COUNT 1)
    endif()
    set(${out} "${_s}" PARENT_SCOPE)
endfunction()

# Whether the site declares a law, so matiec is needed at all.
function(_botanik_codegen_has_programs out site)
    set(_n 0)
    if(EXISTS "${site}/tables/programs.csv")
        file(STRINGS "${site}/tables/programs.csv" _lines REGEX "[^ \t,]")
        list(LENGTH _lines _n)
        math(EXPR _n "${_n} - 1")
    endif()
    if(_n GREATER 0)
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# The pinned iec2c, built on first use; then $IEC2C; then PATH. `missing` names
# what stood in the way when none is found.
function(_botanik_codegen_iec2c out missing root)
    set(_pinned "${root}/vendor/matiec/build/iec2c")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DSRC=${root}/vendor/matiec
                -P ${root}/cmake/matiec-build.cmake
        RESULT_VARIABLE _rc ERROR_VARIABLE _err OUTPUT_QUIET)
    if(_rc EQUAL 0 AND EXISTS "${_pinned}")
        set(${out} "${_pinned}" PARENT_SCOPE)
        return()
    endif()
    string(STRIP "${_err}" _err)
    if(DEFINED ENV{IEC2C} AND EXISTS "$ENV{IEC2C}")
        set(_found "$ENV{IEC2C}")
    else()
        find_program(_botanik_path_iec2c iec2c)
        set(_found "${_botanik_path_iec2c}")
    endif()
    if(_found)
        message(WARNING "codegen: the pinned matiec could not be built, using "
            "${_found}; its output may differ from the pin's.\n${_err}")
        set(${out} "${_found}" PARENT_SCOPE)
    else()
        set(${missing} "the pinned matiec (${_err})" PARENT_SCOPE)
    endif()
endfunction()

function(_botanik_codegen_python out missing python)
    if(NOT python)
        find_program(_botanik_python3 python3)
        set(python "${_botanik_python3}")
    endif()
    if(NOT python)
        set(${missing} "python3" PARENT_SCOPE)
        return()
    endif()
    execute_process(COMMAND ${python} -c "import jinja2"
                    RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT _rc EQUAL 0)
        set(${missing} "python3's jinja2 (pip install jinja2)" PARENT_SCOPE)
        return()
    endif()
    set(${out} "${python}" PARENT_SCOPE)
endfunction()

# Runs tables.py; SITE empty is `library`. Output goes where the generator puts
# it: beside the site, or into the library.
function(_botanik_codegen_generate root site python iec2c)
    set(_py ${CMAKE_COMMAND} -E env --unset=BOTANIK_SITE_DIR IEC2C=${iec2c}
            ${python} ${root}/scripts/sitegen/tables.py)
    if(site)
        set(_cmds generate config plant)
    else()
        set(_cmds library)
    endif()
    foreach(_c IN LISTS _cmds)
        execute_process(COMMAND ${_py} ${_c} ${site}
                        RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_VARIABLE _e)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "codegen: tables.py ${_c} ${site} failed\n${_o}${_e}")
        endif()
    endforeach()
endfunction()

# botanik_codegen(<root> <site or empty> <python or empty>)
#   Regenerates when the stamp does not match, and rewrites the stamp.
function(botanik_codegen root site python)
    _botanik_codegen_stamp_path(_stamp "${root}" "${site}")
    botanik_codegen_hash(_want "${root}" "${site}")
    _botanik_codegen_stamp_read(_have "${_stamp}")
    if(_have STREQUAL _want)
        return()
    endif()

    # Several build trees can build the same site at once (the examples,
    # the sanitizer tree). The second waits, then finds the stamp current.
    file(LOCK "${root}/.codegen.lock" GUARD FUNCTION TIMEOUT 900)
    _botanik_codegen_stamp_read(_have "${_stamp}")
    if(_have STREQUAL _want)
        return()
    endif()

    if(site)
        set(_what "site ${site}")
    else()
        set(_what "the library")
    endif()
    set(_missing "")
    _botanik_codegen_python(_python _missing "${python}")
    set(_iec2c "")
    if(site AND _python)
        _botanik_codegen_has_programs(_laws "${site}")
        if(_laws)
            _botanik_codegen_iec2c(_iec2c _missing "${root}")
        endif()
    endif()
    if(_missing)
        message(FATAL_ERROR "codegen: the generated sources of ${_what} do not "
            "match their inputs (${_stamp}), and they cannot be regenerated "
            "without ${_missing}. Install it, or build a commit whose "
            "generated sources were committed with their inputs.")
    endif()

    message(STATUS "codegen: regenerating ${_what}")
    _botanik_codegen_generate("${root}" "${site}" "${_python}" "${_iec2c}")
    file(WRITE "${_stamp}"
        "# Input hash of this generated tree, written by cmake/codegen-lib.cmake.\n"
        "# The build regenerates when it stops matching. Commit it with the output.\n"
        "${_want}\n")
endfunction()

# Script mode: the `codegen` and `check-generated` targets.
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    get_filename_component(ROOT "${ROOT}" ABSOLUTE)
    if(NOT CHECK)
        botanik_codegen("${ROOT}" "${SITE}" "${PYTHON}")
        if(DONE)
            get_filename_component(_dir "${DONE}" DIRECTORY)
            file(MAKE_DIRECTORY "${_dir}")
            file(TOUCH "${DONE}")
        endif()
        return()
    endif()

    # A copy of the site without its output, regenerated from scratch, so a
    # committed file that is no longer generated has nothing to compare equal to.
    get_filename_component(_name "${SITE}" DIRECTORY)
    get_filename_component(_name "${_name}" NAME)
    set(_copy "${CHECK}/${_name}/src")
    file(REMOVE_RECURSE "${CHECK}/${_name}")
    file(COPY "${SITE}/" DESTINATION "${_copy}"
         REGEX "/generated$" EXCLUDE)
    botanik_codegen("${ROOT}" "${_copy}" "${PYTHON}")

    file(GLOB _committed RELATIVE "${SITE}/generated" "${SITE}/generated/*")
    file(GLOB _fresh RELATIVE "${_copy}/generated" "${_copy}/generated/*")
    set(_bad "")
    foreach(_f IN LISTS _committed _fresh)
        if(NOT EXISTS "${SITE}/generated/${_f}" OR NOT EXISTS "${_copy}/generated/${_f}")
            list(APPEND _bad "${_f}")
            continue()
        endif()
        file(SHA256 "${SITE}/generated/${_f}" _a)
        file(SHA256 "${_copy}/generated/${_f}" _b)
        if(NOT _a STREQUAL _b)
            list(APPEND _bad "${_f}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _bad)
    file(REMOVE_RECURSE "${CHECK}/${_name}")
    if(_bad)
        message(FATAL_ERROR "check-generated: ${SITE}/generated differs from a "
            "fresh generation: ${_bad}")
    endif()
    message(STATUS "check-generated: ${_name} matches")
endif()
