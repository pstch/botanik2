# 3.21 for PROJECT_IS_TOP_LEVEL: botanik is add_subdirectory()'d by a site's
# own repository, and the dev-only half of this file must not run there.
cmake_minimum_required(VERSION 3.21)

# The toolchain must be chosen before project() runs compiler detection.
# BOTANIK_TARGET arrives via -D, so it is already in the cache here. An EMBEDDED
# build cannot reach this in time - the site repo's own project() has already
# run - so it includes cmake/preamble.cmake before its project() instead.
if("${BOTANIK_TARGET}" STREQUAL "OPTA" AND NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/opta-toolchain.cmake")
endif()

project(Botanik CXX C)

# Where the botanik checkout is, whether it is the top-level project or a
# subdirectory of a site's repository. CMAKE_SOURCE_DIR is the SITE's root in
# the embedded build, so nothing below may use it to mean "here".
set(BOTANIK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    CACHE INTERNAL "The botanik checkout root")

set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES ON)
set(CMAKE_C_USE_RESPONSE_FILE_FOR_INCLUDES ON)

# Global definitions
# ============================================================================

set(BOTANIK_TARGET "SIMULATOR" CACHE STRING "Target platform: SIMULATOR or OPTA")

# Greenhouse site to build, named either way:
#   -DBOTANIK_SITE=<name>     a site under examples/
#   -DBOTANIK_SITE_DIR=<path> any directory on disk, possibly out of tree
# BOTANIK_SITE_DIR wins when both are given. One site per build; both targets
# run the same one. See the site resolution block further down.
set(BOTANIK_SITE "ref" CACHE STRING "Site under examples/ (its src/ is the site)")
set(BOTANIK_SITE_DIR "" CACHE PATH "Site directory (overrides BOTANIK_SITE; may be out of tree)")

# -DBOTANIK_SITE=NONE builds the LIBRARY and nothing else: no table, no laws, no
# simulator, no firmware image. For a consumer reusing a component - the console
# and its editor, the logger - configured through that component's own
# set_config() rather than from rows. Every library row name still resolves,
# through vars/rows_none.gen.h, to VAR_NONE and so to the bank's inert sink.
if("${BOTANIK_SITE}" STREQUAL "NONE" AND NOT BOTANIK_SITE_DIR)
    set(BOTANIK_SITE_NONE ON)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ccache as compiler launcher when installed: a no-op when absent, and an
# existing CMAKE_CXX_COMPILER_LAUNCHER is left untouched.
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

# Default to DEBUG so profiling works out of the box. Build types are spelled
# upper case; CMake upper-cases them anyway for CMAKE_*_FLAGS_<CONFIG>.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE DEBUG CACHE STRING "Build type" FORCE)
endif()

# Runtime sanitizers for the SIMULATOR build (ignored on OPTA, which we can't
# run). OFF | address | undefined | address+undefined. `make check-full` runs
# the gate instrumented.
string(TOUPPER "${CMAKE_BUILD_TYPE}" _build_type_upper)
set(BOTANIK_SANITIZE "OFF" CACHE STRING "Sanitizers for the SIMULATOR build")

# Footprint budget for the OPTA image, checked after the link by
# cmake/footprint.cmake, which reads the sizes off the ELF and writes
# build/footprint.json.
#
# Neither ceiling is enforceable in the linker script: its .heap absorbs
# whatever .bss leaves, so static RAM growth shrinks the heap rather than
# overflowing the region, and its FLASH LENGTH is computed from
# CM4_BINARY_START (in QSPI), which reads as ~1.4 GB.
#
# Static RAM is the load-bearing ceiling: what it takes comes out of the heap
# lwIP and Mbed draw on at runtime.
set(BOTANIK_FLASH_MAX_BYTES 786432 CACHE STRING
    "OPTA flash ceiling in bytes (768 KB of the 1.75 MB app region); 0 disables")
set(BOTANIK_STATIC_RAM_MAX_BYTES 327680 CACHE STRING
    "OPTA static RAM ceiling in bytes (320 KB of the 512 KB region); 0 disables")

# Profiler-friendly Debug flags in place of CMake's bare "-g".
set(CMAKE_C_FLAGS_DEBUG   "-O0 -g3 -gdwarf-4 -fno-omit-frame-pointer" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -gdwarf-4 -fno-omit-frame-pointer" CACHE STRING "" FORCE)

# Release: -O3, NDEBUG, stripped at link; LTO below. -Ofast would pull in
# -ffast-math, relaxing the FP semantics the metric filters rely on.
set(CMAKE_C_FLAGS_RELEASE          "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE        "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s"           CACHE STRING "" FORCE)


# Symlink compile_commands.json to the project root for clang-tidy/clangd. DEBUG
# trees only, so a parallel RELEASE configure cannot repoint it at NDEBUG flags.
if(_build_type_upper STREQUAL "DEBUG")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${BOTANIK_ROOT}/compile_commands.json
    )
endif()

# src/botanik_sim/ is the host application and never enters the library.
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
    "src/libbotanik/*.cpp"
    "src/libbotanik/*.c"
)

add_library(botanik STATIC ${LIB_SOURCES})

# Git short hash stamped into a generated header (BOTANIK_GIT_HASH), refreshed
# every build but written only when it changes, so it doesn't churn rebuilds.
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

# Keep the per-beat metric math single-precision: no double FPU on the M4.
# This TU only, so the benign float->double promotions in display formatting
# are not flagged. COMPILE_FLAGS (string) for the CMake 3.10 floor.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/libbotanik/scheduler/scheduler.cpp"
    PROPERTIES COMPILE_FLAGS "-Wdouble-promotion")

# Site resolution: the site DIRECTORY is the single source of truth, resolved
# absolute and forwarded PUBLIC so its files are found by full path. It never
# sits on the include path: a flat site dir carries names like config.h / vars.h,
# which must not shadow other headers (notably Mbed's). Site-internal includes
# are RELATIVE, which is what makes an out-of-tree site work.
#
# config.h carries the build-wide Config overrides, read before libbotanik's
# #ifndef defaults and with none of the library in scope.
#
# ABI guard: any TU including libbotanik_config.h MUST link `botanik` for these
# PUBLIC defines, or it silently compiles against the un-overridden defaults.
# Sizes in the PLC wire contract live in the site's payload.h for that reason.
if(BOTANIK_SITE_NONE)
    set(_site_dir "")
elseif(BOTANIK_SITE_DIR)
    get_filename_component(_site_dir "${BOTANIK_SITE_DIR}" ABSOLUTE
                           BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
else()
    set(_site_dir "${CMAKE_CURRENT_SOURCE_DIR}/examples/${BOTANIK_SITE}/src")
endif()
# The ST runtime headers matiec names bare (accessor.h, iec_std_lib.h). Every
# target needs them: st_runtime.cpp is a site source, so it builds into the
# sim, the suite and the firmware.
target_include_directories(botanik PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/libbotanik/plc/runtime")
if(NOT BOTANIK_SITE_NONE)
    target_include_directories(botanik PUBLIC "${_site_dir}/generated")
endif()

if(BOTANIK_SITE_NONE)
    message(STATUS "Site: none - building the library alone")
elseif(NOT EXISTS "${_site_dir}/site.h")
    message(FATAL_ERROR "Site directory '${_site_dir}' has no site.h; "
        "every site must ship one. Point -DBOTANIK_SITE_DIR=<path> at a site "
        "directory, or -DBOTANIK_SITE=<name> at one under examples/.")
endif()
# The config a table-driven site GENERATES, or the one it hand-writes. A site
# whose site.toml produces config.gen.h needs no shim over it.
if(EXISTS "${_site_dir}/generated/config.gen.h" AND EXISTS "${_site_dir}/config.h")
    message(FATAL_ERROR "Site directory '${_site_dir}' has BOTH "
        "generated/config.gen.h and config.h. One site, one config: the "
        "generated one would win and edits to the hand-written one would go "
        "nowhere. Delete whichever is not the source of truth.")
endif()
if(BOTANIK_SITE_NONE)
    # Nothing to define: libbotanik_site.h resolves neither macro and
    # config/base.h skips the include, so every Config value takes its own
    # #ifndef default.
elseif(EXISTS "${_site_dir}/generated/config.gen.h")
    set(_site_config "${_site_dir}/generated/config.gen.h")
elseif(EXISTS "${_site_dir}/config.h")
    set(_site_config "${_site_dir}/config.h")
else()
    message(FATAL_ERROR "Site directory '${_site_dir}' has neither "
        "generated/config.gen.h nor config.h; one of them carries the "
        "build-wide BOTANIK_* overrides.")
endif()
if(NOT BOTANIK_SITE_NONE)
    target_compile_definitions(botanik PUBLIC
        BOTANIK_SITE_CONFIG=\"${_site_config}\"
        BOTANIK_SITE_HEADER=\"${_site_dir}/site.h\")
endif()
# The row INDICES, the same way. A library consumer names a row through
# Vars::Row::<name>, so a name no table declares is a compile error rather than
# a reference that resolves to the sink for the life of the run. Left unset by a
# build that names no site, and vars/rows.h then falls back to the library's own
# rows_none.gen.h, where every one of them is VAR_NONE.
if(_site_dir AND EXISTS "${_site_dir}/generated/rows.gen.h")
    target_compile_definitions(botanik PUBLIC
        BOTANIK_SITE_ROWS=\"${_site_dir}/generated/rows.gen.h\")
endif()
if(NOT BOTANIK_SITE_NONE)
    message(STATUS "Site: ${_site_dir}")
endif()

# Optional-subsystem feature flags; defaults live in libbotanik_features.h.
# -DBOTANIK_FEATURE_<X>=ON/OFF overrides, forwarded PUBLIC so the whole build
# agrees (same ABI discipline as BOTANIK_SITE_CONFIG). An unset flag falls
# through to the site config, then the header default.
#
# Swept out of the cache by prefix rather than listed: a hand-written list
# silently ignores the -D of any flag missing from it.
get_cmake_property(_all_vars VARIABLES)
foreach(_v ${_all_vars})
    if(_v MATCHES "^BOTANIK_FEATURE_[A-Z0-9_]+$")
        if(${_v})
            set(_feat_val 1)
        else()
            set(_feat_val 0)
        endif()
        target_compile_definitions(botanik PUBLIC ${_v}=${_feat_val})
        message(STATUS "Feature ${_v}: ${_feat_val}")
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
    )

    target_include_directories(botanik PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

    # Common warning set, PUBLIC so it covers the simulator sketch too.
    # -Wno-misleading-indentation: the plcio state machines are deliberately
    # column-aligned, which trips that heuristic.
    target_compile_options(botanik PUBLIC -Wall -Wextra -Wno-misleading-indentation)

    # Warnings are errors, on the library's own sources only: PRIVATE, so the
    # sketch and the test suite keep -Wall -Wextra without -Werror.
    option(BOTANIK_WERROR "Treat warnings as errors when compiling the library" ON)
    if(BOTANIK_WERROR)
        target_compile_options(botanik PRIVATE -Werror)
    endif()

    # PUBLIC so the library and the sketch are both instrumented. Findings stay
    # recoverable (UBSAN logs and continues) so the interactive simulator stays
    # usable; UBSAN_OPTIONS=halt_on_error=1 for a strict run.
    if(NOT BOTANIK_SANITIZE STREQUAL "OFF")
        string(REPLACE "+" "," _san "${BOTANIK_SANITIZE}")
        set(_san_flags -fsanitize=${_san} -fno-omit-frame-pointer)
        target_compile_options(botanik PUBLIC ${_san_flags})
        # Lets the HAL leave SIGSEGV to the sanitizer's own handler. GCC and
        # clang spell the built-in test differently, so state it once for both.
        target_compile_definitions(botanik PUBLIC BOTANIK_SANITIZED=1)
        # Also as link flags so the runtimes get linked; via
        # target_link_libraries because target_link_options is CMake 3.13+.
        target_link_libraries(botanik PUBLIC ${_san_flags})
        message(STATUS "Sanitizers enabled: ${BOTANIK_SANITIZE}")
    endif()

    # The site compiles into the host application, so it stays out of the OPTA
    # `botanik` archive. Empty for a header-only site. Recursive: generated/
    # holds vars.gen.cpp, which defines the tables, and the control programs sit
    # one directory further down.
    # No simulator without a site: main.cpp includes BOTANIK_SITE_HEADER and
    # runs Site::firmware(). The library alone is the whole product then.
    if(NOT BOTANIK_SITE_NONE)
        file(GLOB_RECURSE BOTANIK_SITE_SOURCES CONFIGURE_DEPENDS
             "${_site_dir}/*.cpp")

        # Exported, like BOTANIK_TEST_MAIN beside it: a site repository builds
        # its own targets from this, and add_subdirectory() puts a plain
        # variable in a child scope its own CMakeLists cannot see.
        set(BOTANIK_SITE_SOURCES "${BOTANIK_SITE_SOURCES}" CACHE INTERNAL
            "The selected site's own sources, for a site repo's own targets")

        # -Wshadow on the site's own sources only. A control program reads its
        # variables as members, so a local of the same name takes over silently.
        set_source_files_properties(${BOTANIK_SITE_SOURCES}
                                    PROPERTIES COMPILE_FLAGS "-Wshadow")
        add_executable(botanik_sim
            "src/botanik_sim/main.cpp"
            ${BOTANIK_SITE_SOURCES})
        target_link_libraries(botanik_sim botanik)
    else()
        # A cache entry outlives the branch that set it: reconfiguring the same
        # tree from a site to no site would otherwise leave the old site's
        # sources behind, for clang-tidy to lint and a consumer to compile.
        set(BOTANIK_SITE_SOURCES "" CACHE INTERNAL
            "The selected site's own sources, for a site repo's own targets")
    endif()


    # Release only, probed so a toolchain without LTO degrades to plain -O3.
    option(BOTANIK_LTO "Link-time optimization for RELEASE builds" ON)
    set(_lto_targets "")
    if(TARGET botanik_sim)
        set(_lto_targets botanik_sim)
    endif()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
    if(NOT BOTANIK_LTO)
        message(STATUS "LTO disabled (BOTANIK_LTO=OFF), Release will use -O3 only")
    elseif(_ipo_ok)
        set_property(TARGET botanik ${_lto_targets}
                     PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(STATUS "IPO/LTO not supported, Release will use -O3 only: ${_ipo_msg}")
    endif()

    # clang-tidy over src/. The check set and header filter live in .clang-tidy.
    #
    # Incremental, one stamp per TU: only sources whose text or included headers
    # changed are re-analyzed. Header tracking is IMPLICIT_DEPENDS, the Makefile
    # generator's own dependency scanner, so the target exists only there. A
    # re-configure or an edit to .clang-tidy invalidates every stamp, since the
    # lint depends on the compile flags and the check set. `rm -rf
    # build/clang-tidy` re-lints the whole tree.
    set(_tidy_sources ${LIB_SOURCES} ${BOTANIK_SITE_SOURCES}
                      "${CMAKE_CURRENT_SOURCE_DIR}/src/botanik_sim/main.cpp")
    # ...minus what a generator wrote: never hand-edited, and matiec's output is
    # a .c included as a header and X-macros whose arguments are type names.
    # Linting it reports the tool rather than the code.
    list(FILTER _tidy_sources EXCLUDE REGEX "/generated/")
    find_program(CLANG_TIDY NAMES clang-tidy)
    if(CLANG_TIDY AND CMAKE_GENERATOR MATCHES "Make")
        set(_tidy_stamp_dir "${CMAKE_BINARY_DIR}/clang-tidy")
        file(MAKE_DIRECTORY "${_tidy_stamp_dir}")
        set(_tidy_stamps "")
        foreach(_tidy_src IN LISTS _tidy_sources)
            file(RELATIVE_PATH _tidy_rel "${BOTANIK_ROOT}" "${_tidy_src}")
            string(REPLACE "/" "_" _tidy_stamp "${_tidy_rel}")
            set(_tidy_stamp "${_tidy_stamp_dir}/${_tidy_stamp}.stamp")
            add_custom_command(
                OUTPUT "${_tidy_stamp}"
                # Re-created here, not only at configure time: deleting the dir
                # is how a full re-lint is forced, and the touch below needs it.
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_tidy_stamp_dir}"
                COMMAND ${CLANG_TIDY} -p ${CMAKE_BINARY_DIR} --quiet "${_tidy_src}"
                COMMAND ${CMAKE_COMMAND} -E touch "${_tidy_stamp}"
                DEPENDS "${_tidy_src}" "${BOTANIK_ROOT}/.clang-tidy"
                        "${CMAKE_BINARY_DIR}/compile_commands.json"
                IMPLICIT_DEPENDS CXX "${_tidy_src}"
                COMMENT "clang-tidy ${_tidy_rel}"
                VERBATIM)
            list(APPEND _tidy_stamps "${_tidy_stamp}")
        endforeach()
        add_custom_target(clang-tidy DEPENDS ${_tidy_stamps})
    else()
        message(STATUS "clang-tidy not found (or non-Make generator) - skipping the target")
    endif()

    # Both subdirectories below want a job count.
    include(ProcessorCount)
    ProcessorCount(_nproc)
    if(_nproc EQUAL 0)
        set(_nproc 4)
    endif()

    # The pinned matiec: a host tool, not in ALL, absent where flex/bison are.
    # Before generate.cmake, which diffs against it.
    include(cmake/matiec.cmake)

    # regen-site and check-generated. In EVERY simulator build, embedded or not:
    # a site repository needs both and never builds tests/.
    include(cmake/generate.cmake)

    # ...and what a site repository tests its own site WITH, for the same
    # reason: the suite under tests/ is botanik's own.
    include(cmake/testkit.cmake)

    # The suite, `make check` and the sweeps are botanik's own. A site repo
    # embedding botanik tests its site, not this library, so none of it is
    # declared there - and enable_testing() outside the top level would put
    # CTestTestfile.cmake somewhere ctest never looks.
    # ...and no suite either: every case is written against a site's table.
    if(PROJECT_IS_TOP_LEVEL AND NOT BOTANIK_SITE_NONE)
        enable_testing()
        add_subdirectory(tests)
    endif()

elseif(BOTANIK_TARGET STREQUAL "OPTA")
    message(STATUS "Configuring for OPTA (Arduino/Mbed) sanity check")

    # Compiler + ar/ranlib come from cmake/opta-toolchain.cmake, selected before
    # project().

    # Cortex-M7 + double-precision FPU (STM32H747). These set the __ARM_ARCH_*
    # macros Mbed's headers gate on: without them mbed_atomic.h errors out with
    # "Unknown ARM architecture for exclusive access".
    target_compile_options(botanik PUBLIC
        -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=softfp
        # libmbed.a is built -fno-rtti; without matching it we get
        # undefined "typeinfo for ..." at link.
        -fno-rtti
        # We never throw, and there is no handler: an exception escaping the
        # beat would unwind past the watchdog kick with the field energised.
        # Also drops the unwind tables.
        -fno-exceptions
        # One cooperative loop, so a function-local static needs no lock. Drops
        # __cxa_guard_acquire/release (an RTOS lock on Mbed) from per-beat paths
        # like HAL::Serial::get().
        -fno-threadsafe-statics)

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

    # The Arduino Mbed OPTA core, by default where arduino-cli installs it under
    # ~/.arduino15 (override with -DMBED_CORE_ROOT=... or ARDUINO_DATA_DIR). The
    # highest installed version wins; the include layout below is stable across
    # the 4.x cores, so no version is pinned.
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
        # KVStore: HAL::Storage's OPTA backend. Already in the prebuilt
        # libmbed.a; only the headers were missing.
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
    # Mbed's headers use C++14, so this build needs it even though our code is
    # C++11. Layout/ABI is identical, so libbotanik.h's footprint asserts hold.
    set_target_properties(botanik PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)

    # Size-optimized, no debug info, sections split for the final --gc-sections.
    # These override the inherited DEBUG flags: GCC honors the last -O/-g, and
    # target options come after the per-config ones.
    #
    # -ffat-lto-objects makes each object also hold real machine code, so the
    # archive is usable without LTO, its size is measurable here, and plain
    # ar/ranlib archive it correctly. CMake's IPO support cannot be used:
    # check_ipo_supported links a test executable, which a bare-metal build with
    # no startup or linker script cannot do, so it always reports false.
    #
    # -Os against -O3 is within a percent here once LTO and --gc-sections are in,
    # and which one wins moves with the code, hence the cache variable.
    set(BOTANIK_OPTA_OPT "-Os" CACHE STRING "Optimization level for the OPTA image")

    target_compile_options(botanik PUBLIC
        ${BOTANIK_OPTA_OPT} -g0 -flto -ffat-lto-objects -ffunction-sections -fdata-sections)

    # Compile the objects without trying to link a final binary.
    set_target_properties(botanik PROPERTIES LINKER_LANGUAGE CXX)

    # The selected site compiles into the firmware alongside the entry wiring.
    # Empty for a header-only site.
    # RECURSIVE: generated/ holds vars.gen.cpp, which DEFINES the tables, and a
    # site's control programs sit one directory further down still.
    file(GLOB_RECURSE BOTANIK_SITE_SOURCES CONFIGURE_DEPENDS
         "${_site_dir}/*.cpp")

    # Exported, like BOTANIK_TEST_MAIN beside it: a site repository builds its
    # own targets from this, and add_subdirectory() puts a plain variable in a
    # child scope its own CMakeLists cannot see.
    set(BOTANIK_SITE_SOURCES "${BOTANIK_SITE_SOURCES}" CACHE INTERNAL
        "The selected site's own sources, for a site repo's own targets")

    # -Wshadow on the site's own sources only. A control program reads its
    # variables as members, so a local of the same name takes over silently.
    set_source_files_properties(${BOTANIK_SITE_SOURCES}
                                PROPERTIES COMPILE_FLAGS "-Wshadow")

    # botanik_opta, the flashable M7 image. Three things come from the core
    # package: the prebuilt libmbed.a, the linker script, and PinNames.
    #
    # Two load-bearing details:
    #  - -mfloat-abi=softfp above. libmbed.a is softfp; mixing float ABIs does
    #    not link at all.
    #  - the linker script is preprocessed first: it references CM4_BINARY_START.
    #
    # Layout: .text at 0x08040000 (the app base, bootloader below it), .data/.bss
    # in RAM at 0x24000000, entry at Reset_Handler.
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

    # The Arduino wiring API, which libmbed.a does not carry: the HAL's
    # DigitalIn / DigitalOut / AnalogIn / Pulse call pinMode, digitalRead,
    # digitalWrite, analogRead, micros and attachInterrupt, and the core ships
    # those as sources.
    #
    # wiring*.cpp as a glob rather than named files: Arduino.h declares the whole
    # API and libmbed.a defines none of it, so naming them makes every new HAL
    # call a potential link error, visible on the cross build alone. The glob
    # covers wiring, wiring_analog, wiring_digital, wiring_pulse and
    # wiring_shift, which is the wiring API.
    #
    # The rest stay named: the core's main.cpp would collide with
    # botanik_fw/main.cpp, and Serial.cpp brings the core's own `Serial`, a macro
    # over a USB CDC object.
    file(GLOB MBED_CORE_SOURCES "${MBED_ROOT}/cores/arduino/wiring*.cpp")
    list(APPEND MBED_CORE_SOURCES
        "${MBED_ROOT}/cores/arduino/Interrupts.cpp"      # attachInterrupt
        "${MBED_ROOT}/cores/arduino/pinToIndex.cpp"      # the pin -> index map
        "${MBED_VARIANT}/variant.cpp")                   # PINCOUNT_fn + g_APinDescription

    # variant.cpp reaches for the QSPI flash driver's header, which is not on
    # the core include list above (nothing else in this build needs it).
    # It also reads the CM4 partition bounds, the same three the linker script
    # is preprocessed with below.
    set_source_files_properties("${MBED_VARIANT}/variant.cpp" PROPERTIES
        INCLUDE_DIRECTORIES
        "${MBED_ROOT}/cores/arduino/mbed/storage/blockdevice/COMPONENT_QSPIF/include/QSPIF"
        COMPILE_DEFINITIONS
        "CM4_BINARY_START=0x60000000;CM4_BINARY_END=0x60040000;CM4_RAM_END=0x60080000")

    add_executable(botanik_opta
        "src/botanik_fw/main.cpp"
        "src/botanik_fw/mbed_hooks.cpp"
        ${MBED_CORE_SOURCES}
        ${BOTANIK_SITE_SOURCES})
    add_dependencies(botanik_opta botanik_ldscript)

    # nosys.specs supplies the syscall stubs (-lnosys): a bare-metal image has
    # no _write/_sbrk/_kill of its own, and newlib will not link without them.
    # nano.specs, when on, rewrites -lc/-lstdc++/-lsupc++ to their _nano variants.
    option(BOTANIK_OPTA_NANO "Link newlib-nano on the OPTA image" OFF)
    set(_specs "--specs=nosys.specs")
    if(BOTANIK_OPTA_NANO)
        set(_specs "${_specs} --specs=nano.specs")
    endif()

    set_target_properties(botanik_opta PROPERTIES
        SUFFIX ".elf"
        CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON
        LINK_FLAGS "-Wl,--gc-sections -Wl,--as-needed @${MBED_VARIANT}/ldflags.txt -T${_ldscript} -Wl,-Map,${CMAKE_BINARY_DIR}/botanik_opta.map ${_specs}")
    # Via target_link_libraries so the libraries land AFTER our objects.
    # --whole-archive keeps libmbed's vector table and RTOS entry, which nothing
    # references by symbol; --gc-sections then prunes what is unused.
    target_link_libraries(botanik_opta
        botanik
        -Wl,--whole-archive "${MBED_VARIANT}/libs/libmbed.a" -Wl,--no-whole-archive
        -Wl,--start-group -lstdc++ -lsupc++ -lm -lc -lgcc -lnosys -Wl,--end-group)

    add_custom_command(TARGET botanik_opta POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary
                "$<TARGET_FILE:botanik_opta>" "${CMAKE_BINARY_DIR}/botanik_opta.bin"
        COMMAND ${CMAKE_COMMAND} -E echo "-- flashable image: ${CMAKE_BINARY_DIR}/botanik_opta.bin"
        # The budgets, read back off the ELF.
        COMMAND ${CMAKE_COMMAND}
                "-DELF=$<TARGET_FILE:botanik_opta>"
                "-DSIZE_TOOL=${CMAKE_SIZE}"
                "-DFLASH_MAX=${BOTANIK_FLASH_MAX_BYTES}"
                "-DRAM_MAX=${BOTANIK_STATIC_RAM_MAX_BYTES}"
                "-DOUT=${CMAKE_BINARY_DIR}/footprint.json"
                -P "${BOTANIK_ROOT}/cmake/footprint.cmake"
        VERBATIM)

endif()

# Documentation
# ============================================================================
# botanik's own manual. Top-level only: a site repository renders its own docs,
# and these targets write into THIS tree.
if(PROJECT_IS_TOP_LEVEL)
    # `make -C build docs` renders the manual into docs/doxygen/html/.
    #
    # `docs-screens` is NOT one of its dependencies. Recapturing the screenshots
    # means booting a simulator per scenario and is a minute and a half, which is
    # too much to pay for every render; run it when a screen has actually moved.
    #
    # Both targets WRITE INTO THE SOURCE TREE, as `regen-site` does: what they
    # produce is committed, and a diff afterwards is the point rather than a
    # surprise.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        execute_process(COMMAND ${Python3_EXECUTABLE} -c "import jinja2"
                        RESULT_VARIABLE _docs_jinja_rc OUTPUT_QUIET ERROR_QUIET)
    endif()
    if(Python3_Interpreter_FOUND AND _docs_jinja_rc EQUAL 0)
        # The console screenshots, which drive the real simulator through the pyte
        # harness: a built sim, and pyte importable. Skipped rather than failed
        # where either is absent, the way the screen tests are.
        if(TARGET botanik_sim)
            execute_process(COMMAND ${Python3_EXECUTABLE} -c "import pytest, pyte"
                            RESULT_VARIABLE _screens_rc OUTPUT_QUIET ERROR_QUIET)
            if(_screens_rc EQUAL 0)
                add_custom_target(docs-screens
                    COMMAND ${CMAKE_COMMAND} -E chdir ${BOTANIK_ROOT}
                            ${Python3_EXECUTABLE} scripts/screens.py
                            $<TARGET_FILE:botanik_sim>
                    COMMAND ${CMAKE_COMMAND} -E chdir ${BOTANIK_ROOT}
                            ${Python3_EXECUTABLE} scripts/screens.py --scenarios
                            $<TARGET_FILE:botanik_sim>
                    DEPENDS botanik_sim
                    USES_TERMINAL
                    COMMENT "Recapturing the console screenshots (a minute or two)"
                    VERBATIM)
            else()
                message(STATUS "docs: pyte not importable - screenshots stay as committed")
            endif()
        endif()
    endif()

    find_program(DOXYGEN doxygen)
    if(DOXYGEN)
        add_custom_target(docs
            COMMAND ${DOXYGEN} ${BOTANIK_ROOT}/Doxyfile
            WORKING_DIRECTORY ${BOTANIK_ROOT}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
        )
        # `make clean` wipes the output (CMake 3.15+). Doxygen never deletes stale
        # pages, so without this a removed page lingers until a manual rm.
        set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES
            "${BOTANIK_ROOT}/docs/doxygen")
    else()
        message(STATUS "doxygen not found - skipping docs target")
    endif()
endif()
