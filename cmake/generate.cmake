# The generator's targets: regen-site and check-generated.
#
# NOT tests, and not in tests/: a site repository that embeds botanik needs both
# of them and never builds the suite. Included from the top-level CMakeLists in
# every simulator build, whether botanik is the project or a subdirectory of a
# site's.
#
# `_all_sites` is every site with a site.toml under examples/. An out-of-tree
# site is added to it by the build that names it, so BOTANIK_SITE_DIR is
# regenerated too.

# The generated sources are committed and never built, and nothing else holds
# them to the tables: edit a CSV, forget to regenerate, and `make check` is green
# on a firmware without your row. check-generated regenerates into a scratch copy
# of the generator's inputs and diffs, so it never touches the working tree.
#
# Which iec2c: the pinned one (vendor/matiec, built by `make matiec`), so the
# diff is not against whatever a given developer has on PATH; then $IEC2C, the
# override tables.py itself honours; then PATH. Skipped where neither it nor
# python3 is present.
find_program(_python3 python3)
# The generators render through Jinja templates, so it is as much a
# prerequisite as the compiler is. Absent, both targets are simply not
# declared, the way they are without python3 or iec2c.
if(_python3)
    execute_process(COMMAND ${_python3} -c "import jinja2"
                    RESULT_VARIABLE _jinja_rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT _jinja_rc EQUAL 0)
        message(STATUS "check-full: no python3 jinja2 - `pip install jinja2`")
        set(_python3 "")
    endif()
endif()
if(BOTANIK_PINNED_IEC2C)
    set(_iec2c "${BOTANIK_PINNED_IEC2C}")
elseif(DEFINED ENV{IEC2C} AND EXISTS "$ENV{IEC2C}")
    set(_iec2c "$ENV{IEC2C}")
else()
    find_program(_iec2c iec2c)
endif()
# EVERY site in the tree, not just the selected one: the library's own rows
# (vars/tables/library.csv) and the templates feed all of them, so a table edit
# regenerated for one site leaves the others stale and committed.
file(GLOB _site_tomls ${BOTANIK_ROOT}/examples/*/src/site.toml)
set(_all_sites "")
foreach(_t ${_site_tomls})
    # .../examples/<name>/src/site.toml -> <name>: the example is the project,
    # and src/ inside it is the site, exactly as a site repository is laid out.
    get_filename_component(_d ${_t} DIRECTORY)
    get_filename_component(_d ${_d} DIRECTORY)
    get_filename_component(_n ${_d} NAME)
    list(APPEND _all_sites ${_n})
endforeach()

# A site OUT of this tree is named by its path rather than by a directory under
# examples/, and it is the one the embedding build actually cares about. The
# generator takes either, so both go through the same loop.
set(_extern_site "")
if(_site_dir AND EXISTS "${_site_dir}/site.toml")
    string(FIND "${_site_dir}" "${BOTANIK_ROOT}/examples/" _in_tree)
    if(NOT _in_tree EQUAL 0)
        set(_extern_site "${_site_dir}")
    endif()
endif()

# Who the variables page belongs to. In botanik's own tree that is BOTANIK_SITE
# under BOTANIK_ROOT/docs; embedded it is the site the consuming project named,
# and the page goes under THAT project's docs. Writing it under BOTANIK_ROOT
# would regenerate the submodule's page for a site it does not own.
if(_extern_site)
    set(_points_site "${_extern_site}")
else()
    set(_points_site "${BOTANIK_SITE}")
endif()
set(_points_root "${CMAKE_SOURCE_DIR}")

# `regen-site` is check-generated's other half: the same commands in the source
# tree, so what a contributor runs after editing a table or an ST file is one
# command rather than four per site plus an env var. Not a build step and never
# in ALL; see cmake/matiec.cmake.
if(_python3 AND _iec2c)
    add_custom_target(regen-site
        COMMAND ${CMAKE_COMMAND} -E echo "Regenerating: ${_all_sites}"
        USES_TERMINAL
        COMMENT "Regenerating every site into the source tree - review the diff"
        VERBATIM)
    if(PROJECT_IS_TOP_LEVEL)
        add_custom_command(TARGET regen-site POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E chdir ${BOTANIK_ROOT}
                    ${_python3} scripts/sitegen/tables.py library
            VERBATIM)
    endif()
    foreach(_s ${_all_sites} ${_extern_site})
        add_custom_command(TARGET regen-site POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E env IEC2C=${_iec2c} ${CMAKE_COMMAND} -E chdir
                    ${BOTANIK_ROOT} ${_python3} scripts/sitegen/tables.py generate ${_s}
            COMMAND ${CMAKE_COMMAND} -E env IEC2C=${_iec2c} ${CMAKE_COMMAND} -E chdir
                    ${BOTANIK_ROOT} ${_python3} scripts/sitegen/tables.py config ${_s}
            COMMAND ${CMAKE_COMMAND} -E env IEC2C=${_iec2c} ${CMAKE_COMMAND} -E chdir
                    ${BOTANIK_ROOT} ${_python3} scripts/sitegen/tables.py plant ${_s}
            VERBATIM)
    endforeach()
    # The variables page belongs to whoever owns the site, and botanik owns
    # none: its examples are examples. Written only for a site a CONSUMING
    # project named, under that project's own root.
    if(NOT BOTANIK_SITE_NONE AND _extern_site)
        add_custom_command(TARGET regen-site POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E env IEC2C=${_iec2c} ${CMAKE_COMMAND} -E chdir
                    ${_points_root} ${_python3}
                    ${BOTANIK_ROOT}/scripts/sitegen/tables.py points ${_points_site}
            VERBATIM)
    endif()
    if(TARGET matiec)
        add_dependencies(regen-site matiec)
    endif()
endif()

set(_gen ${CMAKE_BINARY_DIR}/genchk)
if(_python3 AND _iec2c)
    add_custom_target(check-generated
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_gen}
        # The generator's inputs only: copying the source root would recurse
        # into this build directory and drag every object file with it.
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOTANIK_ROOT}/scripts   ${_gen}/scripts
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOTANIK_ROOT}/templates ${_gen}/templates
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOTANIK_ROOT}/src       ${_gen}/src
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOTANIK_ROOT}/docs      ${_gen}/docs
        USES_TERMINAL
        COMMENT "Sweep: the committed generated sources still match the tables"
        VERBATIM)

    # Every site, and for each one every file it actually committed under
    # generated/. Globbed rather than listed: the set differs per site (a site
    # with no programs has no plant.cpp and no POUS.c), and a listed name that
    # stopped being generated would silently stop being checked.
    foreach(_s ${_all_sites})
        add_custom_command(TARGET check-generated POST_BUILD
            # Per site, not the whole of examples/: each one symlinks the
            # repository root at extern/botanik, and copy_directory follows it.
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${BOTANIK_ROOT}/examples/${_s}/src ${_gen}/examples/${_s}/src
            COMMAND ${CMAKE_COMMAND} -E chdir ${_gen}
                    ${_python3} scripts/sitegen/tables.py library
            # The scratch tree is a COPY of src/, so a file that is committed
            # but no longer generated would compare equal to itself. Clear the
            # site's generated/ first and let the regeneration refill it.
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${_gen}/examples/${_s}/src/generated
            COMMAND ${CMAKE_COMMAND} -E env IEC2C=${_iec2c} ${CMAKE_COMMAND} -E chdir ${_gen}
                    ${_python3} scripts/sitegen/tables.py generate ${_s}
            COMMAND ${CMAKE_COMMAND} -E env IEC2C=${_iec2c} ${CMAKE_COMMAND} -E chdir ${_gen}
                    ${_python3} scripts/sitegen/tables.py config ${_s}
            COMMAND ${CMAKE_COMMAND} -E env IEC2C=${_iec2c} ${CMAKE_COMMAND} -E chdir ${_gen}
                    ${_python3} scripts/sitegen/tables.py plant ${_s}
            VERBATIM)
        file(GLOB _committed ${BOTANIK_ROOT}/examples/${_s}/src/generated/*)
        foreach(_f ${_committed})
            file(RELATIVE_PATH _rel ${BOTANIK_ROOT} ${_f})
            add_custom_command(TARGET check-generated POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E compare_files ${_f} ${_gen}/${_rel}
                VERBATIM)
        endforeach()
    endforeach()

    # The two the generator writes into the LIBRARY rather than into a site,
    # and the selected site's variables page.
    # The three the generator writes into the LIBRARY rather than into a site.
    add_custom_command(TARGET check-generated POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${BOTANIK_ROOT}/src/libbotanik/plc/runtime/quality.gen.h
                ${_gen}/src/libbotanik/plc/runtime/quality.gen.h
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${BOTANIK_ROOT}/src/libbotanik/plc/runtime/health.gen.h
                ${_gen}/src/libbotanik/plc/runtime/health.gen.h
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${BOTANIK_ROOT}/src/libbotanik/vars/rows_none.gen.h
                ${_gen}/src/libbotanik/vars/rows_none.gen.h
        VERBATIM)

    # ...and the selected site's variables page, which a build with no site has
    # no equivalent of.
    # `points` writes no page in this tree, so there is nothing to diff. Run it
    # into the scratch copy anyway: it is the only thing that compiles
    # reference.py, and a generator that crashes for every site repository at
    # once should not wait for a site repository to find out.
    if(NOT BOTANIK_SITE_NONE AND NOT _extern_site)
        add_custom_command(TARGET check-generated POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E env IEC2C=${_iec2c} ${CMAKE_COMMAND} -E chdir ${_gen}
                    ${_python3} scripts/sitegen/tables.py points ${BOTANIK_SITE}
            VERBATIM)
    endif()

    add_custom_command(TARGET check-generated POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_gen}
        VERBATIM)

    # Build the pinned compiler first where there is one, so the diff is against
    # it rather than against whatever the developer happens to have.
    if(TARGET matiec)
        add_dependencies(check-generated matiec)
    endif()
else()
    message(STATUS "check-full: no python3 or iec2c, skipping the generated-source check")
endif()
