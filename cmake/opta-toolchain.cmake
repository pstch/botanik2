# Cross-compilation toolchain for the Arduino OPTA (STM32H747, Cortex-M7).
#
# Selected automatically by the top-level CMakeLists when BOTANIK_TARGET=OPTA, so
# the usual `cmake -B build-opta -DBOTANIK_TARGET=OPTA` still works. It has to run
# before project()/compiler detection — that is the whole point: setting the
# compilers here makes CMake derive the matching ARM ar/ranlib (and the rest of
# the ABI) instead of the host's, which a late set(CMAKE_CXX_COMPILER ...) cannot.

set(CMAKE_SYSTEM_NAME      Generic)   # bare-metal, no OS — also flips on cross mode
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)

# This build only ever produces a static library (the final firmware is linked in
# the external sketch, with the PLC objects), and we have no startup/linker script
# here. Probe the compiler with a static-library try-compile so CMake doesn't try
# to link a test executable and fail.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
