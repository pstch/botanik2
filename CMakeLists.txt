cmake_minimum_required(VERSION 3.10)

# The toolchain must be chosen before project() runs compiler detection.
# BOTANIK_TARGET arrives via -D, so it is already in the cache here.
if("${BOTANIK_TARGET}" STREQUAL "OPTA")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/opta-toolchain.cmake")
endif()

project(Botanik CXX C)

set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES ON)
set(CMAKE_C_USE_RESPONSE_FILE_FOR_INCLUDES ON)

# Global definitions
# ============================================================================

set(BOTANIK_TARGET "SIMULATOR" CACHE STRING "Target platform: SIMULATOR or OPTA")

# Greenhouse site to build, named either way:
#   -DBOTANIK_SITE=<name>     a site under src/sites/
#   -DBOTANIK_SITE_DIR=<path> any directory on disk, possibly out of tree
# BOTANIK_SITE_DIR wins when both are given. One site per build; both targets
# run the same one. See the site resolution block further down.
set(BOTANIK_SITE "gh_mlb41" CACHE STRING "Greenhouse site under src/sites/")
set(BOTANIK_SITE_DIR "" CACHE PATH "Site directory (overrides BOTANIK_SITE; may be out of tree)")

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
# run). OFF | address | undefined | address+undefined. Default OFF: instrumentation
# more than doubles compile CPU. `make check-full` runs the gate instrumented.
string(TOUPPER "${CMAKE_BUILD_TYPE}" _build_type_upper)
set(BOTANIK_SANITIZE "OFF" CACHE STRING "Sanitizers for the SIMULATOR build")

# Profiler-friendly Debug flags in place of CMake's bare "-g".
set(CMAKE_C_FLAGS_DEBUG   "-O0 -g3 -gdwarf-4 -fno-omit-frame-pointer" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -gdwarf-4 -fno-omit-frame-pointer" CACHE STRING "" FORCE)

# Release: -O3 (not -Ofast: it pulls in -ffast-math, which would relax the FP
# semantics the metric filters rely on), NDEBUG, stripped at link. LTO below.
set(CMAKE_C_FLAGS_RELEASE          "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE        "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s"           CACHE STRING "" FORCE)


# Symlink compile_commands.json to the project root for clang-tidy/clangd. DEBUG
# trees only, so a parallel RELEASE configure cannot repoint it at NDEBUG flags.
if(_build_type_upper STREQUAL "DEBUG")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/compile_commands.json
    )
endif()

# The library is just src/libbotanik/. src/botanik_sim/ is the host application
# and never enters the library, so it stays out of the OPTA build.
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

# Keep the per-beat metric math single-precision (no double FPU on the M4).
# Scoped to this one TU so it does not flag the benign float->double promotions
# in display formatting. COMPILE_FLAGS (string) for the CMake 3.10 floor.
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

# Optional-subsystem feature flags; defaults live in libbotanik_features.h.
# -DBOTANIK_FEATURE_<X>=ON/OFF overrides, forwarded PUBLIC so the whole build
# agrees (same ABI discipline as BOTANIK_SITE_CONFIG). An unset flag falls
# through to the site config, then the header default.
#
# Discovered by sweeping the cache for the prefix, never listed: a hand-written
# list silently ignores the -D of any flag missing from it.
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

    # Sanitizers, PUBLIC so the library and the sketch are both instrumented.
    # Findings stay recoverable (UBSAN logs and continues) so the interactive
    # simulator stays usable; UBSAN_OPTIONS=halt_on_error=1 for a strict run.
    if(NOT BOTANIK_SANITIZE STREQUAL "OFF")
        string(REPLACE "+" "," _san "${BOTANIK_SANITIZE}")
        set(_san_flags -fsanitize=${_san} -fno-omit-frame-pointer)
        target_compile_options(botanik PUBLIC ${_san_flags})
        # Also as link flags so the runtimes get linked; via
        # target_link_libraries because target_link_options is CMake 3.13+.
        target_link_libraries(botanik PUBLIC ${_san_flags})
        message(STATUS "Sanitizers enabled: ${BOTANIK_SANITIZE}")
    endif()

    # The selected site compiles into the host application, not the library, so
    # it stays out of the OPTA `botanik` archive. Empty for a header-only site.
    file(GLOB BOTANIK_SITE_SOURCES CONFIGURE_DEPENDS "${_site_dir}/*.cpp")
    add_executable(botanik_sim
        "src/botanik_sim/main.cpp"
        ${BOTANIK_SITE_SOURCES})
    target_link_libraries(botanik_sim botanik)


    # Link-time optimization for Release only, probed so a toolchain without LTO
    # degrades to plain -O3. -DBOTANIK_LTO=OFF buys the -O3 codegen without
    # paying the whole-program link.
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

    # clang-tidy over src/. The check set and header filter live in .clang-tidy,
    # with bugprone-* promoted to errors so a finding fails the target.
    #
    # Incremental, one stamp per TU: only sources whose text or included headers
    # changed are re-analyzed. Header tracking is IMPLICIT_DEPENDS, i.e. the
    # Makefile generator's dependency scanner, so the target exists only there.
    # Any re-configure or edit to .clang-tidy invalidates every stamp: the lint
    # depends on the compile flags and check set too. `rm -rf build/clang-tidy`
    # to re-lint the whole tree.
    set(_tidy_sources ${LIB_SOURCES} ${BOTANIK_SITE_SOURCES}
                      "${CMAKE_CURRENT_SOURCE_DIR}/src/botanik_sim/main.cpp")
    find_program(CLANG_TIDY NAMES clang-tidy)
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
                # Re-created here, not only at configure time: deleting the dir
                # is how a full re-lint is forced, and the touch below needs it.
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_tidy_stamp_dir}"
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
    else()
        message(STATUS "clang-tidy not found (or non-Make generator) - skipping the target")
    endif()

    # Tests are host-only, so SIMULATOR-only. enable_testing() registers the
    # CTest machinery; the suites and `make check` are defined in tests/.
    enable_testing()
    add_subdirectory(tests)

elseif(BOTANIK_TARGET STREQUAL "OPTA")
    message(STATUS "Configuring for OPTA (Arduino/Mbed) sanity check")

    # Compiler + ar/ranlib come from cmake/opta-toolchain.cmake, selected before
    # project(). See the LTO note below for why plain ar suffices.

    # Cortex-M7 + double-precision FPU (STM32H747). These set the __ARM_ARCH_*
    # macros Mbed's headers gate on: without them mbed_atomic.h errors out with
    # "Unknown ARM architecture for exclusive access".
    target_compile_options(botanik PUBLIC
        -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=softfp
        # libmbed.a is built -fno-rtti; without matching it we get
        # undefined "typeinfo for ..." at link.
        -fno-rtti
        # We never throw, and there is no handler to throw to: an exception
        # escaping the beat would unwind past the watchdog kick with the field
        # left energised. Also drops the unwind tables our code carried for it.
        -fno-exceptions
        # One cooperative loop, so a function-local static needs no lock. Drops
        # __cxa_guard_acquire/release, which on Mbed takes an RTOS lock, from
        # per-beat paths like HAL::Serial::get().
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

    # Locate the Arduino Mbed OPTA core, by default where arduino-cli installs it
    # under ~/.arduino15 (override with -DMBED_CORE_ROOT=... or ARDUINO_DATA_DIR).
    # The highest installed version wins: the include layout below has been
    # stable across the 4.x cores, so no version is pinned.
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

    # The firmware must fit the STM32H747's flash: size-optimized, no debug info,
    # sections split for the final --gc-sections. These override the inherited
    # DEBUG flags, since GCC honors the last -O/-g and target options come after
    # the per-config ones.
    #
    # -ffat-lto-objects makes each object *also* hold real -Os machine code, so
    # the archive is usable without LTO, its size is measurable here, and plain
    # ar/ranlib archive it correctly. CMake's IPO support is unusable here:
    # check_ipo_supported links a test executable, which no bare-metal build
    # without a startup or linker script can do, so it always reports false.
    target_compile_options(botanik PUBLIC
        -Os -g0 -flto -ffat-lto-objects -ffunction-sections -fdata-sections)

    # Compile the objects without trying to link a final binary.
    set_target_properties(botanik PROPERTIES LINKER_LANGUAGE CXX)

    # The selected site compiles into the firmware alongside the entry wiring.
    # Empty for a header-only site.
    file(GLOB BOTANIK_SITE_SOURCES CONFIGURE_DEPENDS "${_site_dir}/*.cpp")

    # ------------------------------------------------------------------
    # botanik_opta - the flashable M7 image
    # ------------------------------------------------------------------
    # Built here, not by the Arduino IDE and not with the core's own toolchain
    # (~40x slower on Mbed headers). Only three things come from the core
    # package: the prebuilt libmbed.a, the linker script, and PinNames.
    #
    # Two load-bearing details:
    #  - -mfloat-abi=softfp above. libmbed.a is softfp; mixing float ABIs does
    #    not link at all.
    #  - the linker script is PREPROCESSED first: it references CM4_BINARY_START.
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

    # The Arduino wiring API, which libmbed.a does NOT carry: HAL::Pulse and
    # LocalInputs call pinMode/digitalRead/micros/attachInterrupt, and the core
    # ships those as sources. Named one by one rather than globbed - the core's
    # own main.cpp would collide with botanik_fw/main.cpp.
    set(MBED_CORE_SOURCES
        "${MBED_ROOT}/cores/arduino/wiring.cpp"          # micros / millis
        "${MBED_ROOT}/cores/arduino/wiring_digital.cpp"  # pinMode / digitalRead
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
        "src/botanik_fw/firmware.cpp"
        "src/botanik_fw/main.cpp"
        "src/botanik_fw/mbed_hooks.cpp"
        ${MBED_CORE_SOURCES}
        ${BOTANIK_SITE_SOURCES})
    add_dependencies(botanik_opta botanik_ldscript)
    set_target_properties(botanik_opta PROPERTIES
        SUFFIX ".elf"
        CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON
        LINK_FLAGS "-Wl,--gc-sections -Wl,--as-needed @${MBED_VARIANT}/ldflags.txt -T${_ldscript} -Wl,-Map,${CMAKE_BINARY_DIR}/botanik_opta.map --specs=nosys.specs")
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
        VERBATIM)

endif()

# Doxygen documentation
# ============================================================================
# Platform-independent: `make -C build docs` regenerates the HTML API docs from
# the root Doxyfile, into docs/doxygen/html/.
find_program(DOXYGEN doxygen)
find_program(PYTHON3 NAMES python3 python)
if(DOXYGEN)
    # Code charts (LoC + churn by area) atop the Library page, generated just
    # before doxygen runs. The script falls back to a placeholder SVG when
    # git/cloc are missing; without Python the step is skipped entirely.
    set(DOCS_CHART_CMD "")
    if(PYTHON3)
        set(DOCS_CHART_CMD COMMAND ${PYTHON3}
            ${CMAKE_SOURCE_DIR}/scripts/code_charts.py
            --out ${CMAKE_SOURCE_DIR}/docs/generated
            --since "12 months ago"
            # Module coupling matrix on the same page: which way the cross-module
            # includes point. Reads the source tree, so it needs no build.
            COMMAND ${PYTHON3}
            ${CMAKE_SOURCE_DIR}/scripts/coupling_matrix.py
            --out ${CMAKE_SOURCE_DIR}/docs/generated)
    else()
        message(STATUS "python3 not found - Library code charts will be skipped")
    endif()
    add_custom_target(docs
        ${DOCS_CHART_CMD}
        COMMAND ${DOXYGEN} ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )

    # `make clean` wipes the generated docs output (CMake 3.15+). Doxygen never
    # deletes stale pages, so without this a removed page lingers until a rm.
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES
        "${CMAKE_SOURCE_DIR}/docs/doxygen"
        "${CMAKE_SOURCE_DIR}/docs/generated")

    # To read them: xdg-open docs/doxygen/html/index.html
else()
    message(STATUS "doxygen not found - skipping docs target")
endif()
