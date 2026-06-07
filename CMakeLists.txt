cmake_minimum_required(VERSION 3.10)
project(Botanik CXX C)

set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES ON)
set(CMAKE_C_USE_RESPONSE_FILE_FOR_INCLUDES ON)

# Global definitions
# ============================================================================

# Change with cmake -DBOTANIK_TARGET=SIMULATOR ..
set(BOTANIK_TARGET "SIMULATOR" CACHE STRING "Target platform: SIMULATOR or OPTA")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to a Debug build so profiling/debugging works out of the box.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Override CMake's default Debug flags ("-g") with profiler-friendly ones:
# optimizations disabled, full DWARF-4 debug info, frame pointer preserved.
set(CMAKE_C_FLAGS_DEBUG   "-O0 -g3 -gdwarf-4 -fno-omit-frame-pointer" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -gdwarf-4 -fno-omit-frame-pointer" CACHE STRING "" FORCE)


# Symlink compile_commands.json to the project root so clang-tidy/clangd
# find it without extra configuration.
execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)

file(GLOB_RECURSE LIB_SOURCES
    "src/*.cpp"
    "src/*.c"
)

add_library(botanik STATIC ${LIB_SOURCES})

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

    add_executable(botanik_sim "src/botanik_sim/main.cpp")
    target_link_libraries(botanik_sim botanik)

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

elseif(BOTANIK_TARGET STREQUAL "OPTA")
    message(STATUS "Configuring for OPTA (Arduino/Mbed) sanity check")

    set(CMAKE_C_COMPILER arm-none-eabi-gcc)
    set(CMAKE_CXX_COMPILER arm-none-eabi-g++)

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

    set(MBED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/includes/arduino_mbed_opta_4.1.6_6bd5cf0253f43b1d")
    
    target_include_directories(botanik SYSTEM PUBLIC 
        "${CMAKE_CURRENT_SOURCE_DIR}/src"

        "${CMAKE_CURRENT_SOURCE_DIR}/includes/arduino_arm-none-eabi-gcc_7-2017q4_7b7be9f526b2cb64/**"

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
    # Because the PLC objects are not available in this build, we
    # tell CMake to compile the objects without trying to create a final binary
    set_target_properties(botanik PROPERTIES LINKER_LANGUAGE CXX)

endif()

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
else()
    message(STATUS "doxygen not found — skipping docs target")
endif()
