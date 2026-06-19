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

# Greenhouse site to build: selects src/sites/<name>/ (its payload, pages, alert
# table, mock PLC) and the site.h header the harness/firmware include via
# BOTANIK_SITE_HEADER. One site per build; both targets run the same one.
set(BOTANIK_SITE "gh_mlb41" CACHE STRING "Greenhouse site under src/sites/")
set(BOTANIK_SITE_HEADER "sites/${BOTANIK_SITE}/site.h")

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
file(GLOB_RECURSE LIB_SOURCES
    "src/libbotanik/*.cpp"
    "src/libbotanik/*.c"
)

add_library(botanik STATIC ${LIB_SOURCES})

# Site config overrides (optional): if the selected site ships a config.h, make it
# build-wide by defining BOTANIK_SITE_CONFIG PUBLIC on the library - so libbotanik's
# own TUs and every consumer compile against the same overridden Config values
# (anything narrower is an ABI mismatch). libbotanik_config.h #includes it before
# its #ifndef defaults. No-op when the site has no overrides.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/sites/${BOTANIK_SITE}/config.h")
    target_compile_definitions(botanik PUBLIC
        BOTANIK_SITE_CONFIG="sites/${BOTANIK_SITE}/config.h")
    message(STATUS "Site config overrides: src/sites/${BOTANIK_SITE}/config.h")
endif()

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
        target_compile_options(botanik PUBLIC ${_san_flags})
        # Propagate as link flags too (via target_link_libraries for CMake 3.10
        # compatibility — target_link_options is 3.13+) so the runtimes get linked.
        target_link_libraries(botanik PUBLIC ${_san_flags})
        message(STATUS "Sanitizers enabled: ${BOTANIK_SANITIZE}")
    endif()

    # The selected site (payload + pages + alarm table + mock PLC) compiles into
    # the host application, not the library: it stays out of the OPTA `botanik`
    # archive. sim.cpp drops out on non-SIMULATOR builds via its own guard.
    file(GLOB BOTANIK_SITE_SOURCES "src/sites/${BOTANIK_SITE}/*.cpp")
    add_executable(botanik_sim
        "src/botanik_sim/main.cpp"
        ${BOTANIK_SITE_SOURCES})
    target_link_libraries(botanik_sim botanik)
    target_compile_definitions(botanik_sim PRIVATE BOTANIK_SITE_HEADER="${BOTANIK_SITE_HEADER}")

    # Link-time optimization for Release only (cross-TU inlining on top of -O3).
    # Probed so a toolchain without LTO support degrades to a plain -O3 build.
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
    if(_ipo_ok)
        set_property(TARGET botanik botanik_sim
                     PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(STATUS "IPO/LTO not supported, Release will use -O3 only: ${_ipo_msg}")
    endif()

    # Strict warning gate. A mirror of the library compiled with -Werror, built
    # only on demand (EXCLUDE_FROM_ALL) so day-to-day `make` stays warning-tolerant
    # — visibility, not a hard gate — while `make check` fails hard on any warning.
    # Linking `botanik` re-uses its PUBLIC include dirs, defines, and warning (and
    # sanitizer) flags, so the two stay in lockstep with no duplicated settings.
    add_library(botanik_strict STATIC EXCLUDE_FROM_ALL ${LIB_SOURCES})
    target_link_libraries(botanik_strict PRIVATE botanik)
    target_compile_options(botanik_strict PRIVATE -Werror)
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

    # clang-tidy static analysis target. Drives run-clang-tidy over our own src/
    # tree (the regex is matched against the paths in compile_commands.json); the
    # check set and header filter live in .clang-tidy, with bugprone-* promoted to
    # errors there so a finding makes the target — and `make check` — fail.
    find_program(RUN_CLANG_TIDY NAMES run-clang-tidy run-clang-tidy.py)
    if(RUN_CLANG_TIDY)
        add_custom_target(clang-tidy
            COMMAND ${RUN_CLANG_TIDY}
                -p ${CMAKE_BINARY_DIR}
                -quiet
                "${CMAKE_SOURCE_DIR}/src/.*"
            COMMENT "Running clang-tidy (SIMULATOR)"
            VERBATIM
        )
    else()
        message(STATUS "run-clang-tidy not found — skipping clang-tidy target")
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
        -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)

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
        "${MBED_ROOT}/cores/arduino/mbed/drivers/include"
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
    # The selected site (payload + pages + alert table) compiles into the firmware
    # alongside the entry wiring; sim.cpp drops out here via its TARGET_SIMULATOR
    # guard (this build never defines it), so only alerts.cpp carries through.
    file(GLOB BOTANIK_SITE_SOURCES "src/sites/${BOTANIK_SITE}/*.cpp")
    add_library(botanik_fw STATIC
        "src/botanik_fw/firmware.cpp"
        "src/botanik_fw/main.cpp"
        "src/botanik_fw/botanik_arduino.cpp"
        ${BOTANIK_SITE_SOURCES})
    target_link_libraries(botanik_fw PUBLIC botanik)
    target_compile_definitions(botanik_fw PRIVATE BOTANIK_SITE_HEADER="${BOTANIK_SITE_HEADER}")
    set_target_properties(botanik_fw PROPERTIES
        LINKER_LANGUAGE CXX
        CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)

endif()

# Arduino library package
# ============================================================================
# The Axel PLC IDE owns the sketch and only accepts *imported Arduino libraries* -
# it can't take an arbitrary file tree or a sketch folder. So this is the delivery
# artifact: a zip whose top folder holds library.properties + src/, importable via
# the IDE's "Add .ZIP Library". It's NOT a compile step - the OPTA build flags come
# from the board when the IDE compiles the library, which is exactly why we don't
# replay the DEVICE_*/TARGET_* define wall here. Platform-independent (just copies +
# zips text), so `make arduino-lib` works from the default SIMULATOR tree.
#
# The package is libbotanik ONLY (the umbrella headers + the libbotanik/ tree). The
# entry-point wiring - #include <libbotanik.h>, construct the components, define the
# botanik_setup()/botanik_step() the sketch window calls - is written by the
# integrator in a C++ file at the *project* root, NOT shipped here. src/botanik_fw/
# is our reference for that file (and drives the OPTA ABI check); it stays out of the
# imported library so it can't collide with the integrator's own entry point.
set(ARDUINO_PKG "${CMAKE_BINARY_DIR}/arduino")
set(ARDUINO_LIB "${ARDUINO_PKG}/libbotanik")        # top folder name == library name

add_custom_target(arduino-lib
    # Clean stage so a removed source never lingers in the package.
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${ARDUINO_LIB}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ARDUINO_LIB}/src"
    # The metadata marker that makes the folder a (recursive-layout) library.
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/cmake/library.properties.in"
            "${ARDUINO_LIB}/library.properties"
    # Library content: the umbrella headers + the libbotanik tree, nothing else.
    # copy_directory follows the real tree (not a symlink), so the zip stays
    # self-contained once unpacked on the IDE host.
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/src/libbotanik.h"
            "${CMAKE_SOURCE_DIR}/src/libbotanik_config.h"
            "${CMAKE_SOURCE_DIR}/src/libbotanik_sizes.h"
            "${ARDUINO_LIB}/src/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/src/libbotanik" "${ARDUINO_LIB}/src/libbotanik"
    # Zip it: cmake -E tar can emit a zip with --format=zip.
    COMMAND ${CMAKE_COMMAND} -E chdir "${ARDUINO_PKG}"
            ${CMAKE_COMMAND} -E tar cf "libbotanik.zip" --format=zip "libbotanik"
    COMMENT "Packaging importable Arduino library: ${ARDUINO_PKG}/libbotanik.zip"
    VERBATIM
)

# Doxygen documentation
# ============================================================================
# Platform-independent: `make -C build docs` regenerates the HTML API docs
# from the Doxyfile at the project root (output under docs/doxygen/html/).
find_program(DOXYGEN doxygen)
if(DOXYGEN)
    add_custom_target(docs
        COMMAND ${DOXYGEN} ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )

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
