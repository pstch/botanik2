# Cross-compilation toolchain for the Arduino OPTA (STM32H747, Cortex-M7).
#
# Selected by the top-level CMakeLists when BOTANIK_TARGET=OPTA. It must run
# before project()/compiler detection: setting the compilers here makes CMake
# derive the matching ARM ar/ranlib and ABI instead of the host's, which a late
# set(CMAKE_CXX_COMPILER ...) cannot.

set(CMAKE_SYSTEM_NAME      Generic)   # bare-metal, and flips on cross mode
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)

# Probe the compiler with a static-library try-compile: there is no startup or
# linker script at this point, so linking a test executable would fail.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
