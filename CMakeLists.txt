cmake_minimum_required(VERSION 3.10)

# A cross-compilation toolchain must be chosen before project() runs the compiler
# detection. BOTANIK_TARGET arrives via -D (already in the cache at this point),
# so we can select the OPTA toolchain here and keep the plain
# `cmake -DBOTANIK_TARGET=OPTA` invocation.
if("${BOTANIK_TARGET}" STREQUAL "OPTA")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/opta-toolchain.cmake")
endif()

project(Botanik CXX C)

set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES ON)
set(CMAKE_C_USE_RESPONSE_FILE_FOR_INCLUDES ON)

# Global definitions
# ============================================================================

# Change with cmake -DBOTANIK_TARGET=SIMULATOR ..
set(BOTANIK_TARGET "SIMULATOR" CACHE STRING "Target platform: SIMULATOR or OPTA")

# Greenhouse site to build: one directory holding the payload, the generated
# tables, the alarm table and the mock PLC. Two ways to name it:
#
#   -DBOTANIK_SITE=<name>     a site in this tree, i.e. src/sites/<name>/
#   -DBOTANIK_SITE_DIR=<path> any directory on disk, e.g. a consumer project's
#                             LLSketch dir holding its generated site
#
# BOTANIK_SITE_DIR wins when both are given. One site per build; both targets
# run the same one. See the site resolution block further down.
set(BOTANIK_SITE "gh_mlb41" CACHE STRING "Greenhouse site under src/sites/")
set(BOTANIK_SITE_DIR "" CACHE PATH "Site directory (overrides BOTANIK_SITE; may be out of tree)")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use ccache as the compiler launcher when it's installed. Auto-detected, so it's
# a no-op when absent; an existing CMAKE_CXX_COMPILER_LAUNCHER (e.g. passed on the
# command line) is left untouched. Pays off on the frequent clean rebuilds this
# project does ('rm -rf build', config/branch switches): cached objects replay in
# ~2s instead of a full recompile.
if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        message(STATUS "ccache enabled as compiler launcher: ${CCACHE_PROGRAM}")
    else()
        message(STATUS "ccache not found - building without a compiler cache")
    endif()
endif()

# Default to a DEBUG build so profiling/debugging works out of the box. Build
# types are spelled in upper case (DEBUG / RELEASE); CMake upper-cases the type
# anyway when resolving the per-config CMAKE_*_FLAGS_<CONFIG> variables.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE DEBUG CACHE STRING "Build type" FORCE)
endif()

# Runtime sanitizers for the SIMULATOR build (ignored on OPTA, which we can't run).
# OFF | address | undefined | address+undefined. Default to address+undefined on
# DEBUG builds — the interactive sim runs fine instrumented and it catches UB and
# memory bugs early — and OFF otherwise. Override explicitly, e.g. with
# -DBOTANIK_SANITIZE=OFF on a debug tree, or to opt in on a non-debug one.
string(TOUPPER "${CMAKE_BUILD_TYPE}" _build_type_upper)
if(_build_type_upper STREQUAL "DEBUG")
    set(_san_default "address+undefined")
else()
    set(_san_default "OFF")
endif()
set(BOTANIK_SANITIZE "${_san_default}" CACHE STRING "Sanitizers for the SIMULATOR build")

# Override CMake's default Debug flags ("-g") with profiler-friendly ones:
# optimizations disabled, full DWARF-4 debug info, frame pointer preserved.
set(CMAKE_C_FLAGS_DEBUG   "-O0 -g3 -gdwarf-4 -fno-omit-frame-pointer" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -gdwarf-4 -fno-omit-frame-pointer" CACHE STRING "" FORCE)

# Release: maximum optimization with assertions compiled out, and the final
# executable stripped of its symbol table at link time (-s). We stop at -O3
# rather than -Ofast on purpose: -Ofast pulls in -ffast-math, which would relax
# the FP semantics the metric filters rely on. LTO is enabled separately below.
set(CMAKE_C_FLAGS_RELEASE          "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE        "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s"           CACHE STRING "" FORCE)


# Symlink compile_commands.json to the project root so clang-tidy/clangd find it
# without extra configuration. Only the DEBUG tree drives editor tooling, so a
# parallel RELEASE (or other) configure won't repoint the symlink at the
# optimized/NDEBUG compile flags. Compared case-insensitively (via the
# _build_type_upper computed above) so any spelling of the build type resolves.
if(_build_type_upper STREQUAL "DEBUG")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/compile_commands.json
    )
endif()

# The library is just src/libbotanik/. The src/botanik_sim/ sketch (main + mock
# PLC) is the host application, compiled into botanik_sim below — never into the
# library, so it stays out of the OPTA build, which has no host entry point.
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
    "src/libbotanik/*.cpp"
    "src/libbotanik/*.c"
)

add_library(botanik STATIC ${LIB_SOURCES})

# Build-time git short hash, stamped into a generated header (BOTANIK_GIT_HASH) and
# refreshed every build (written only when it changes, so it doesn't churn rebuilds).
# The Arduino .ino deploy doesn't run CMake, so version.h falls back to "unknown" there.
set(BOTANIK_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
add_custom_target(botanik_gitversion
    BYPRODUCTS "${BOTANIK_GENERATED_DIR}/botanik_gitversion.h"
    COMMAND ${CMAKE_COMMAND}
            "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DOUT=${BOTANIK_GENERATED_DIR}/botanik_gitversion.h"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gitversion.cmake"
    COMMENT "Stamping git hash")
add_dependencies(botanik botanik_gitversion)
target_include_directories(botanik PUBLIC "${BOTANIK_GENERATED_DIR}")

# Keep the per-beat metric math single-precision on the M4 (no double FPU): the
# scheduler/filters hot path must stay float-clean. Scoped to that one TU so it
# enforces the hot path without flagging the benign float->double promotions in
# display formatting (format_float takes a double). COMPILE_FLAGS (string) for the
# CMake 3.10 floor; applies in every target that compiles this source.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/libbotanik/scheduler/scheduler.cpp"
    PROPERTIES COMPILE_FLAGS "-Wdouble-promotion")

# Site resolution: the site DIRECTORY is the single source of truth. We resolve it
# to two absolute paths and forward those PUBLIC on the library, so the site's files
# are found by full path and nothing has to sit on the include path (a flat site dir
# carries names like config.h / status.h / alarms.h, which we must not let shadow
# anyone else's headers - notably Mbed's on the OPTA build).
#
# Site-internal includes are RELATIVE ("payload.h", not "sites/<name>/payload.h"),
# which is what makes an out-of-tree site work: a quoted include resolves against
# the including file's own directory first, so a site's files always find each
# other and can never be shadowed by the reference site in this tree.
#
# config.h carries the build-wide Config overrides libbotanik_config.h #includes
# before its #ifndef defaults, so libbotanik's own TUs and every consumer compile
# against the same values.
# ABI guard: any TU that includes libbotanik_config.h MUST link `botanik` so it picks
# up these PUBLIC defines transitively; a TU that includes the header without linking
# the library would silently compile against the un-overridden defaults and mismatch
# the rest of the build. Sizes that are part of the PLC wire contract are declared by
# the site's payload.h instead, precisely so they cannot go missing this way.
if(BOTANIK_SITE_DIR)
    get_filename_component(_site_dir "${BOTANIK_SITE_DIR}" ABSOLUTE
                           BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
else()
    set(_site_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/sites/${BOTANIK_SITE}")
endif()
foreach(_f config.h site.h)
    if(NOT EXISTS "${_site_dir}/${_f}")
        message(FATAL_ERROR "Site directory '${_site_dir}' has no ${_f}; "
            "every site must ship one. Point -DBOTANIK_SITE_DIR=<path> at a site "
            "directory, or -DBOTANIK_SITE=<name> at one under src/sites/.")
    endif()
endforeach()
target_compile_definitions(botanik PUBLIC
    BOTANIK_SITE_CONFIG=\"${_site_dir}/config.h\"
    BOTANIK_SITE_HEADER=\"${_site_dir}/site.h\")
message(STATUS "Site: ${_site_dir}")

# Optional-subsystem feature flags (libbotanik_features.h). Each defaults ON in
# the header; pass -DBOTANIK_FEATURE_<X>=ON/OFF to override. We forward only the
# flags explicitly set on the command line, as a PUBLIC define on `botanik` so
# the whole build agrees (same ABI discipline as BOTANIK_SITE_CONFIG above).
foreach(_feat NETWORK NTP MQTT CONSOLE MONITOR SUN)
    if(DEFINED BOTANIK_FEATURE_${_feat})
        if(BOTANIK_FEATURE_${_feat})
            set(_feat_val 1)
        else()
            set(_feat_val 0)
        endif()
        target_compile_definitions(botanik PUBLIC BOTANIK_FEATURE_${_feat}=${_feat_val})
        message(STATUS "Feature ${_feat}: ${_feat_val}")
    endif()
endforeach()

# Simulator mode
# ============================================================================

if(BOTANIK_TARGET STREQUAL "SIMULATOR")
    message(STATUS "Configuring for SIMULATOR mode")
    target_compile_definitions(botanik PUBLIC
        TARGET_SIMULATOR
        DEBUG
        _GNU_SOURCE
        BOTANIK_LOG_MIN_LEVEL=1   # Logs::LOG_DEBUG - verbose on the sim (header default is LOG_INFO)
    )

    target_include_directories(botanik PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

    # Enable the common warning set on the host build (no -Werror yet; the goal is
    # visibility, not a hard gate). PUBLIC so it also covers the simulator sketch.
    # -Wno-misleading-indentation: the plcio state machines are deliberately
    # column-aligned for readability, which trips this heuristic.
    target_compile_options(botanik PUBLIC -Wall -Wextra -Wno-misleading-indentation)

    # Sanitizers (PUBLIC so the library and the sketch are both instrumented, and
    # propagated as link flags so the runtimes are linked in). Findings stay
    # recoverable (UBSAN logs each and continues) so the interactive simulator
    # remains usable; set UBSAN_OPTIONS=halt_on_error=1 at runtime for a strict
    # one-shot/CI run. The frame pointer keeps stacks readable.
    if(NOT BOTANIK_SANITIZE STREQUAL "OFF")
        string(REPLACE "+" "," _san "${BOTANIK_SANITIZE}")
        set(_san_flags -fsanitize=${_san} -fno-omit-frame-pointer)
        # Everything that links botanik gets instrumented - except the strict
        # warning mirror below, which is compiled for its diagnostics and never
        # run. $<TARGET_PROPERTY:NAME> in an INTERFACE genex names the *consuming*
        # target, and this is the only way to opt out: CMake emits a target's own
        # options before the ones inherited from its link libraries, so a
        # -fno-sanitize=all on the mirror would be overridden by this line.
        target_compile_options(botanik PUBLIC
            "$<$<NOT:$<STREQUAL:$<TARGET_PROPERTY:NAME>,botanik_strict>>:${_san_flags}>")
        # Propagate as link flags too (via target_link_libraries for CMake 3.10
        # compatibility — target_link_options is 3.13+) so the runtimes get linked.
        target_link_libraries(botanik PUBLIC ${_san_flags})
        message(STATUS "Sanitizers enabled: ${BOTANIK_SANITIZE}")
    endif()

    # The selected site (payload + tables + alarm table + mock PLC) compiles into
    # the host application, not the library: it stays out of the OPTA `botanik`
    # archive. gh_mlb41 is header-only, so this glob is empty; it stays for sites
    # that ship their own .cpp.
    file(GLOB BOTANIK_SITE_SOURCES CONFIGURE_DEPENDS "${_site_dir}/*.cpp")
    add_executable(botanik_sim
        "src/botanik_sim/main.cpp"
        ${BOTANIK_SITE_SOURCES})
    target_link_libraries(botanik_sim botanik)

    # Hot-restart: after every relink, signal any running simulators to re-exec the
    # rebuilt binary (SIGUSR1 -> botanik_sim_reexec -> HAL::reset, which re-execs the
    # resolved exe path). pkill exits non-zero when none are running, so `|| true`
    # keeps the build green. Runs only when the target is actually relinked. VERBATIM
    # so the whole `sh -c` script reaches the shell as one argument - without it CMake
    # escapes only the spaces, not the `||`, and make's shell splits the fallback into
    # a bogus ` true` command ("true: not found") whenever no sim is running.
    add_custom_command(TARGET botanik_sim POST_BUILD
        COMMAND sh -c "pkill -USR1 -x botanik_sim || true"
        VERBATIM
        COMMENT "Signaling running botanik_sim instances to hot-restart (SIGUSR1)")

    # Link-time optimization for Release only (cross-TU inlining on top of -O3).
    # Probed so a toolchain without LTO support degrades to a plain -O3 build.
    # -DBOTANIK_LTO=OFF turns it off for builds that want the -O3 codegen without
    # paying the whole-program link: the pyte sub-build below does exactly that,
    # since its sim only has to be "not sanitizer-slow", not optimally inlined.
    option(BOTANIK_LTO "Link-time optimization for RELEASE builds" ON)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
    if(NOT BOTANIK_LTO)
        message(STATUS "LTO disabled (BOTANIK_LTO=OFF), Release will use -O3 only")
    elseif(_ipo_ok)
        set_property(TARGET botanik botanik_sim
                     PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(STATUS "IPO/LTO not supported, Release will use -O3 only: ${_ipo_msg}")
    endif()

    # Strict warning gate. A mirror of the library compiled with -Werror, built
    # only on demand (EXCLUDE_FROM_ALL) so day-to-day `make` stays warning-tolerant
    # — visibility, not a hard gate — while `make check` fails hard on any warning.
    # Linking `botanik` re-uses its PUBLIC include dirs, defines and warning flags,
    # so the two stay in lockstep with no duplicated settings.
    add_library(botanik_strict STATIC EXCLUDE_FROM_ALL ${LIB_SOURCES})
    target_link_libraries(botanik_strict PRIVATE botanik)
    # -g0, and no sanitizer instrumentation (opted out where the flags are set):
    # the mirror is never run or debugged, only compiled for its diagnostics, and
    # neither DWARF nor ASan changes which warnings GCC emits. The optimization
    # level does (-Wmaybe-uninitialized and friends need the optimizer's flow
    # analysis), so -O0 is inherited untouched and the diagnostic set stays
    # exactly the one this gate has always applied.
    target_compile_options(botanik_strict PRIVATE -Werror -g0)
    # Keep this mirror out of compile_commands.json. Otherwise every library TU
    # gets a second, -Werror entry, and run-clang-tidy lints each file twice —
    # the -Werror pass promoting ordinary -Wall warnings (e.g. unused-private-field
    # on WIP stubs) into hard errors. clang-tidy is meant to gate on bugprone-*
    # only (see .clang-tidy); the warning gate is botanik_strict's own GCC build.
    # (Target property is honored on CMake >= 3.20; older versions ignore it.)
    set_target_properties(botanik_strict PROPERTIES EXPORT_COMPILE_COMMANDS OFF)

    # cppcheck static analysis target
    find_program(CPPCHECK cppcheck)
    if(CPPCHECK)
        add_custom_target(cppcheck
            COMMAND ${CPPCHECK}
                --project=${CMAKE_BINARY_DIR}/compile_commands.json
                --enable=warning,performance,portability
                --std=c++11
                --suppress=missingIncludeSystem
                --suppress=unmatchedSuppression
                --inline-suppr
                --quiet
                --error-exitcode=1
            COMMENT "Running cppcheck (SIMULATOR)"
            VERBATIM
        )
    else()
        message(STATUS "cppcheck not found — skipping cppcheck target")
    endif()

    # clang-tidy static analysis over our own src/ tree. The check set and header
    # filter live in .clang-tidy, with bugprone-* promoted to errors there so a
    # finding makes the target (and `make check`) fail.
    #
    # Incremental, one stamp file per TU: `make check` re-lints only the sources
    # whose text (or whose included headers) changed, instead of re-analyzing all
    # of src/ on every run. That full sweep cost ~130s here and was pure waste on
    # an unchanged tree, since a bare custom target has no outputs to compare.
    # Header tracking comes from IMPLICIT_DEPENDS, i.e. the Makefile generator's
    # own dependency scanner. Ninja and the IDE generators ignore it, so they
    # keep the always-run full sweep (correct, just slower). Re-generating
    # compile_commands.json (any re-configure) or editing .clang-tidy invalidates
    # every stamp: the lint depends on the exact compile flags and check set, not
    # only on the source text.
    set(_tidy_sources ${LIB_SOURCES} ${BOTANIK_SITE_SOURCES}
                      "${CMAKE_CURRENT_SOURCE_DIR}/src/botanik_sim/main.cpp")
    find_program(CLANG_TIDY NAMES clang-tidy)
    find_program(RUN_CLANG_TIDY NAMES run-clang-tidy run-clang-tidy.py)
    if(CLANG_TIDY AND CMAKE_GENERATOR MATCHES "Make")
        set(_tidy_stamp_dir "${CMAKE_BINARY_DIR}/clang-tidy")
        file(MAKE_DIRECTORY "${_tidy_stamp_dir}")
        set(_tidy_stamps "")
        foreach(_tidy_src IN LISTS _tidy_sources)
            file(RELATIVE_PATH _tidy_rel "${CMAKE_SOURCE_DIR}" "${_tidy_src}")
            string(REPLACE "/" "_" _tidy_stamp "${_tidy_rel}")
            set(_tidy_stamp "${_tidy_stamp_dir}/${_tidy_stamp}.stamp")
            add_custom_command(
                OUTPUT "${_tidy_stamp}"
                COMMAND ${CLANG_TIDY} -p ${CMAKE_BINARY_DIR} --quiet "${_tidy_src}"
                COMMAND ${CMAKE_COMMAND} -E touch "${_tidy_stamp}"
                DEPENDS "${_tidy_src}" "${CMAKE_SOURCE_DIR}/.clang-tidy"
                        "${CMAKE_BINARY_DIR}/compile_commands.json"
                IMPLICIT_DEPENDS CXX "${_tidy_src}"
                COMMENT "clang-tidy ${_tidy_rel}"
                VERBATIM)
            list(APPEND _tidy_stamps "${_tidy_stamp}")
        endforeach()
        add_custom_target(clang-tidy DEPENDS ${_tidy_stamps})
    elseif(RUN_CLANG_TIDY)
        add_custom_target(clang-tidy
            COMMAND ${RUN_CLANG_TIDY}
                -p ${CMAKE_BINARY_DIR}
                -quiet
                "${CMAKE_SOURCE_DIR}/src/.*"
            COMMENT "Running clang-tidy (SIMULATOR)"
            VERBATIM
        )
    else()
        message(STATUS "clang-tidy not found - skipping clang-tidy target")
    endif()

    # The unconditional sweep, kept as its own target: same checks, every TU, no
    # stamps consulted. For CI, or to re-validate the whole tree on demand.
    if(RUN_CLANG_TIDY)
        add_custom_target(clang-tidy-all
            COMMAND ${RUN_CLANG_TIDY}
                -p ${CMAKE_BINARY_DIR}
                -quiet
                "${CMAKE_SOURCE_DIR}/src/.*"
            COMMENT "Running clang-tidy over all of src/ (SIMULATOR)"
            VERBATIM
        )
    endif()

    # doctest unit tests (host-only, so SIMULATOR-only). enable_testing() here
    # registers the CTest machinery for `ctest`/`make test`; the suite itself —
    # and a `make check` that builds-then-runs it — is defined in tests/.
    enable_testing()
    add_subdirectory(tests)

elseif(BOTANIK_TARGET STREQUAL "OPTA")
    message(STATUS "Configuring for OPTA (Arduino/Mbed) sanity check")

    # Compiler + ar/ranlib come from cmake/opta-toolchain.cmake (selected before
    # project()), so the ARM toolchain is used consistently for compiling and
    # archiving — see the LTO note below for why plain ar suffices here.

    # Cortex-M7 + double-precision FPU (STM32H747). These set the __ARM_ARCH_*
    # macros Mbed's headers gate on (atomics, CRC) — without them mbed_atomic.h
    # errors out with "Unknown ARM architecture for exclusive access".
    target_compile_options(botanik PUBLIC
        -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=softfp
        # libmbed.a is built -fno-rtti; without matching it we get
        # undefined "typeinfo for ..." at link.
        -fno-rtti)

    target_compile_definitions(botanik PUBLIC 
        ARM_MATH_CM7

        COMPONENT_4343W_FS=1
        COMPONENT_CYW43XXX=1
        COMPONENT_FLASHIAP=1
        COMPONENT_QSPIF=1
        COMPONENT_WHD=1

        CM4_BOOT_BY_APPLICATION
        CORE_CM7
        __CORTEX_M7
        __CMSIS_RTOS
        CYW43XXX_UNBUFFERED_UART

        DEVICE_ANALOGIN=1
        DEVICE_ANALOGOUT=1
        DEVICE_CAN=1
        DEVICE_CRC=1
        DEVICE_EMAC=1
        DEVICE_FLASH=1
        DEVICE_I2C=1
        DEVICE_I2C_ASYNCH=1
        DEVICE_I2CSLAVE=1
        DEVICE_INTERRUPTIN=1
        DEVICE_LPTICKER=1
        DEVICE_MPU=1
        DEVICE_PORTIN=1
        DEVICE_PORTINOUT=1
        DEVICE_PORTOUT=1
        DEVICE_PWMOUT=1
        DEVICE_QSPI=1
        DEVICE_RESET_REASON=1
        DEVICE_RTC=1
        DEVICE_SERIAL=1
        DEVICE_SERIAL_FC=1
        DEVICE_SLEEP=1
        DEVICE_SPI=1
        DEVICE_SPI_ASYNCH=1
        DEVICE_SPISLAVE=1
        DEVICE_STDIO_MESSAGES=1
        DEVICE_TRNG=1
        DEVICE_USBDEVICE=1
        DEVICE_USTICKER=1
        DEVICE_WATCHDOG=1
        FEATURE_BLE=1

        __FPU_PRESENT=1
        __MBED__=1
        __MBED_CMSIS_RTOS_CM
        MBED_BUILD_TIMESTAMP=1721368775.741562
        MBED_TICKLES

        MBED_NO_GLOBAL_USING_DIRECTIVE=1
        CORE_MAJOR=4
        CORE_MINOR=1
        CORE_PATCH=6
        USE_ARDUINO_PINOUT


        MBEDTLS_FS_IO
        MBEDTLS_SHA1_C

        METAL_INTERNAL
        METAL_MAX_DEVICE_REGIONS=2

        NO_ATOMIC_64_SUPPORT
        QSPI_NO_SAMPLE_SHIFT
        TRANSACTION_QUEUE_SIZE_SPI=2
        RPMSG_BUFFER_SIZE=512

        STM32H747xx
        TARGET_CORDIO
        TARGET_CORTEX
        TARGET_CORTEX_M
        TARGET_LIKE_CORTEX_M7
        TARGET_LIKE_MBED TARGET_M7
        TARGET_MCU_STM32
        TARGET_MCU_STM32H7
        TARGET_MCU_STM32H747xI
        TARGET_MCU_STM32H747xI_CM7

        TARGET_RTOS_M4_M7
        TARGET_STM
        TARGET_STM32H7
        TARGET_STM32H747xI
        TARGET_STM32H747xI_CM7

        TARGET_NAME=OPTA
        TARGET_OPTA
        TARGET_RELEASE

        TOOLCHAIN_GCC
        TOOLCHAIN_GCC_ARM

        USE_FULL_LL_DRIVER
        USE_HAL_DRIVER
        VIRTIO_DRIVER_ONLY
        EXTRA_IDLE_STACK_REQUIRED
    )

    # Locate the Arduino Mbed OPTA core. Default to the one arduino-cli installs
    # under ~/.arduino15 (override with -DMBED_CORE_ROOT=... or ARDUINO_DATA_DIR);
    # the highest installed version wins. The include layout below has been stable
    # across the 4.x cores, so we don't pin a version. This replaces the old
    # vendored includes/ tree - the ARM compiler ships its own libstdc++/newlib
    # headers, so no separate toolchain include path is needed here.
    if(NOT MBED_CORE_ROOT)
        if(DEFINED ENV{ARDUINO_DATA_DIR})
            set(_ard_data "$ENV{ARDUINO_DATA_DIR}")
        else()
            set(_ard_data "$ENV{HOME}/.arduino15")
        endif()
        file(GLOB _mbed_versions "${_ard_data}/packages/arduino/hardware/mbed_opta/*")
        if(_mbed_versions)
            list(SORT _mbed_versions)
            list(GET _mbed_versions -1 MBED_CORE_ROOT)
        endif()
    endif()
    if(NOT MBED_CORE_ROOT OR NOT EXISTS "${MBED_CORE_ROOT}/cores/arduino/mbed")
        message(FATAL_ERROR
            "Arduino Mbed OPTA core not found. Install it with:\n"
            "  arduino-cli core install arduino:mbed_opta\n"
            "or pass -DMBED_CORE_ROOT=/path/to/packages/arduino/hardware/mbed_opta/<version>")
    endif()
    message(STATUS "Using Mbed OPTA core: ${MBED_CORE_ROOT}")
    set(MBED_ROOT "${MBED_CORE_ROOT}")

    target_include_directories(botanik SYSTEM PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"

        "${MBED_ROOT}/cores/arduino/mbed/platform/include/"
        "${MBED_ROOT}/cores/arduino/mbed/platform/include/platform"
        "${MBED_ROOT}/cores/arduino/mbed/platform/cxxsupport"
        "${MBED_ROOT}/cores/arduino/mbed/hal/include/"
        "${MBED_ROOT}/cores/arduino/mbed/hal/include/hal/"
        "${MBED_ROOT}/cores/arduino/mbed/rtos/include/"
        "${MBED_ROOT}/cores/arduino/mbed/rtos/include/rtos/internal"
        "${MBED_ROOT}/cores/arduino/mbed/targets/TARGET_STM/"
        "${MBED_ROOT}/cores/arduino/mbed/targets/TARGET_STM/TARGET_STM32H7/"
        "${MBED_ROOT}/cores/arduino/mbed/targets/TARGET_STM/TARGET_STM32H7/STM32Cube_FW/"
        "${MBED_ROOT}/cores/arduino/mbed/targets/TARGET_STM/TARGET_STM32H7/STM32Cube_FW/CMSIS"
        "${MBED_ROOT}/cores/arduino/mbed/targets/TARGET_STM/TARGET_STM32H7/STM32Cube_FW/STM32H7xx_HAL_Driver"
        "${MBED_ROOT}/cores/arduino/mbed/targets/TARGET_STM/TARGET_STM32H7/TARGET_STM32H747xI/TARGET_STM32H747xI_CM4"
        "${MBED_ROOT}/cores/arduino/mbed/targets/TARGET_STM/TARGET_STM32H7/TARGET_STM32H747xI/TARGET_OPTA"
        "${MBED_ROOT}/cores/arduino/mbed/cmsis/CMSIS_5/CMSIS/TARGET_CORTEX_M/Include"
        "${MBED_ROOT}/cores/arduino/mbed/cmsis/CMSIS_5/CMSIS/RTOS2/RTX/Config"
        "${MBED_ROOT}/cores/arduino/mbed/cmsis/CMSIS_5/CMSIS/RTOS2/RTX/Include"
        "${MBED_ROOT}/cores/arduino/mbed/cmsis/CMSIS_5/CMSIS/RTOS2/RTX/Include1"
        "${MBED_ROOT}/cores/arduino/mbed/cmsis/CMSIS_5/CMSIS/RTOS2/Include"
        "${MBED_ROOT}/cores/arduino/mbed/cmsis/device/rtos/include"
        "${MBED_ROOT}/cores/arduino/mbed/cmsis/device/RTE/include"
        "${MBED_ROOT}/cores/arduino/mbed/cmsis/device"
        "${MBED_ROOT}/cores/arduino/mbed/connectivity/mbedtls"
        "${MBED_ROOT}/cores/arduino/mbed/connectivity/mbedtls/include"
        "${MBED_ROOT}/cores/arduino/mbed/connectivity/netsocket/include"
        "${MBED_ROOT}/cores/arduino/mbed/events/include"
        "${MBED_ROOT}/cores/arduino/mbed/storage/filesystem/include"
        "${MBED_ROOT}/cores/arduino/mbed/storage/blockdevice/include"
        # KVStore: HAL::Storage's OPTA backend. Already in the prebuilt libmbed.a
        # (kv_set/kv_get/kv_remove + TDBStore); only the headers were missing.
        "${MBED_ROOT}/cores/arduino/mbed/storage/kvstore/include"
        "${MBED_ROOT}/cores/arduino/mbed/storage/kvstore/kvstore_global_api/include"
        "${MBED_ROOT}/cores/arduino/mbed/storage/kvstore/tdbstore/include"
        "${MBED_ROOT}/cores/arduino/mbed/platform/include"
        "${MBED_ROOT}/cores/arduino/mbed/drivers/include"
        "${MBED_ROOT}/cores/arduino/mbed/drivers/usb/include"
        "${MBED_ROOT}/cores/arduino/mbed/drivers/usb/include/usb"
        "${MBED_ROOT}/cores/arduino/mbed/drivers/usb/include/usb/internal"
        "${MBED_ROOT}/cores/arduino/mbed/hal/usb/include"
        "${MBED_ROOT}/cores/arduino/mbed/hal/usb/include/usb"
        "${MBED_ROOT}/cores/arduino"
        "${MBED_ROOT}/variants/OPTA"
        "${MBED_ROOT}/libraries/SocketWrapper/src"
        "${MBED_ROOT}/libraries/Wire"
        "${MBED_ROOT}/libraries/Ethernet/src"
    )
    # Mbed's headers (e.g. MbedCRC.h) use C++14 (std::enable_if_t), so this build
    # needs C++14 even though our own code is C++11 — the layout/ABI is identical
    # between the two, so the footprint asserts in libbotanik.h still hold.
    set_target_properties(botanik PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)

    # Firmware that must fit in the STM32H747's flash, so build it the way it ships:
    # size-optimized, no debug info, dead code split out for the final --gc-sections,
    # and LTO so the whole-program link in the sketch can inline and prune across TUs.
    # These override the inherited DEBUG flags (-O0 -g3) — GCC honors the last -O/-g
    # on the line, and target options come after the per-config ones.
    #
    # -flto carries the cross-TU optimization into the final link; -ffat-lto-objects
    # makes each object *also* hold real -Os machine code, so the archive is usable
    # without LTO and its code size is measurable here (arm-none-eabi-size) even
    # though the final binary is linked elsewhere. CMake's own IPO support is not
    # used: check_ipo_supported links a test executable, which can't succeed in this
    # bare-metal build (no startup/linker script), so it always reports unsupported.
    # Fat objects keep a normal symbol table, so the toolchain's plain ar/ranlib
    # archive them correctly — the gcc-ar LTO wrappers are only needed for thin ones.
    target_compile_options(botanik PUBLIC
        -Os -g0 -flto -ffat-lto-objects -ffunction-sections -fdata-sections)

    # Because the PLC objects are not available in this build, we
    # tell CMake to compile the objects without trying to create a final binary
    set_target_properties(botanik PROPERTIES LINKER_LANGUAGE CXX)

    # Firmware application: the BotanikFirmware wiring plus its main() entry. The
    # OPTA counterpart of botanik_sim, but archived as a static lib rather than
    # linked into an executable - the startup code, linker script, and PLC objects
    # that produce the flashable image live in the Arduino build, not here. Linking
    # botanik PUBLIC inherits its include dirs, defines, and -Os/-flto options;
    # LINKER_LANGUAGE skips the final link (no entry point in this build) and the
    # C++14 standard matches botanik (Mbed headers need it).
    # The selected site (payload + tables + alarm table) compiles into the firmware
    # alongside the entry wiring; gh_mlb41 is header-only, so this glob is empty.
    # It stays for sites that ship their own .cpp.
    file(GLOB BOTANIK_SITE_SOURCES CONFIGURE_DEPENDS "${_site_dir}/*.cpp")
    add_library(botanik_fw STATIC
        "src/botanik_fw/firmware.cpp"
        "src/botanik_fw/main.cpp"
        ${BOTANIK_SITE_SOURCES})
    target_link_libraries(botanik_fw PUBLIC botanik)
    set_target_properties(botanik_fw PROPERTIES
        LINKER_LANGUAGE CXX
        CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)

    # ------------------------------------------------------------------
    # botanik_opta - the flashable M7 image
    # ------------------------------------------------------------------
    # Built entirely by us: no Arduino IDE, no Arduino core sources, and not the
    # core's 2017 toolchain (which is ~40x slower on Mbed headers - a three-line
    # Mbed program does not finish compiling in 100 s with it, versus 2.6 s on
    # GCC 14). We take only three things from the core package: the prebuilt
    # libmbed.a, the linker script, and the target's PinNames.
    #
    # Two things are load-bearing here:
    #  - -mfloat-abi=softfp above. libmbed.a is softfp; mixing float ABIs does
    #    not link at all.
    #  - the linker script is PREPROCESSED first: it references CM4_BINARY_START.
    #
    # Resulting layout: .text at 0x08040000 (the app base, with a bootloader
    # below it), .data/.bss in RAM at 0x24000000, entry at Reset_Handler.
    set(MBED_VARIANT "${MBED_ROOT}/variants/OPTA")
    set(MBED_CM4_DEFS -DCM4_BINARY_START=0x60000000
                      -DCM4_BINARY_END=0x60040000
                      -DCM4_RAM_END=0x60080000)
    set(_ldscript "${CMAKE_BINARY_DIR}/linker_script.ld")

    add_custom_command(OUTPUT "${_ldscript}"
        COMMAND ${CMAKE_CXX_COMPILER} -E -P -x c ${MBED_CM4_DEFS}
                "${MBED_VARIANT}/linker_script.ld" -o "${_ldscript}"
        DEPENDS "${MBED_VARIANT}/linker_script.ld"
        COMMENT "Preprocessing the Mbed linker script"
        VERBATIM)
    add_custom_target(botanik_ldscript DEPENDS "${_ldscript}")

    add_executable(botanik_opta
        "src/botanik_fw/firmware.cpp"
        "src/botanik_fw/main.cpp"
        "src/botanik_fw/mbed_hooks.cpp"
        ${BOTANIK_SITE_SOURCES})
    add_dependencies(botanik_opta botanik_ldscript)
    set_target_properties(botanik_opta PROPERTIES
        SUFFIX ".elf"
        CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON
        LINK_FLAGS "-Wl,--gc-sections -Wl,--as-needed @${MBED_VARIANT}/ldflags.txt -T${_ldscript} -Wl,-Map,${CMAKE_BINARY_DIR}/botanik_opta.map --specs=nosys.specs")
    # Libraries via target_link_libraries so they land AFTER our objects.
    # --whole-archive keeps libmbed's vector table and RTOS entry, which nothing
    # references by symbol; --gc-sections then prunes what is genuinely unused.
    target_link_libraries(botanik_opta
        botanik
        -Wl,--whole-archive "${MBED_VARIANT}/libs/libmbed.a" -Wl,--no-whole-archive
        -Wl,--start-group -lstdc++ -lsupc++ -lm -lc -lgcc -lnosys -Wl,--end-group)

    add_custom_command(TARGET botanik_opta POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary
                "$<TARGET_FILE:botanik_opta>" "${CMAKE_BINARY_DIR}/botanik_opta.bin"
        COMMAND ${CMAKE_COMMAND} -E echo "-- flashable image: ${CMAKE_BINARY_DIR}/botanik_opta.bin"
        VERBATIM)

endif()

# NOTE: the Arduino library zip and the vendored-sketch tree are gone. Both
# existed to get sources into the Axel PLC IDE; we build the flashable image
# ourselves now (botanik_opta above) and sites live in this tree.

# Doxygen documentation
# ============================================================================
# Platform-independent: `make -C build docs` regenerates the HTML API docs
# from the Doxyfile at the project root (output under docs/doxygen/html/).
find_program(DOXYGEN doxygen)
find_program(PYTHON3 NAMES python3 python)
if(DOXYGEN)
    # Code charts (LoC + churn by area) shown atop the Library page. Generated into
    # docs/generated/ (gitignored) just before doxygen runs, so they always reflect
    # the current tree. The script falls back to a placeholder SVG if git/cloc are
    # missing; the whole step is skipped when there is no Python interpreter.
    set(DOCS_CHART_CMD "")
    if(PYTHON3)
        set(DOCS_CHART_CMD COMMAND ${PYTHON3}
            ${CMAKE_SOURCE_DIR}/scripts/code_charts.py
            --out ${CMAKE_SOURCE_DIR}/docs/generated
            --since "12 months ago")
    else()
        message(STATUS "python3 not found — Library code charts will be skipped")
    endif()
    add_custom_target(docs
        ${DOCS_CHART_CMD}
        COMMAND ${DOXYGEN} ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )

    # `make clean` wipes the whole generated docs output (CMake 3.15+; ignored on
    # older). Doxygen never deletes stale pages on its own, so without this a removed
    # page lingers in docs/doxygen/html until a manual rm - clean now purges it, along
    # with the generated code charts.
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES
        "${CMAKE_SOURCE_DIR}/docs/doxygen"
        "${CMAKE_SOURCE_DIR}/docs/generated")

    # Convenience target: build the tests + docs, run the suite, then open the
    # generated HTML in a browser. Opt-in (not folded into `docs`) so a plain
    # `make docs` stays safe in CI / over SSH. Uses xdg-open on Linux, open on
    # macOS.
    find_program(OPEN_CMD NAMES xdg-open open)
    if(OPEN_CMD)
        add_custom_target(docs-open
            COMMAND ${OPEN_CMD} ${CMAKE_SOURCE_DIR}/docs/doxygen/html/index.html
            DEPENDS docs
            COMMENT "Opening API documentation in browser"
            VERBATIM
        )
        # In a SIMULATOR tree the unit tests exist (`check` builds + runs them);
        # chain them in so the docs open alongside a validated, green suite — the
        # size table in the Testing page then reflects a passing run.
        if(TARGET check)
            add_dependencies(docs-open check)
        endif()
    else()
        message(STATUS "no xdg-open/open found — skipping docs-open target")
    endif()
else()
    message(STATUS "doxygen not found — skipping docs target")
endif()
