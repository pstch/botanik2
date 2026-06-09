#ifndef HAL_OPTA_H
#ifdef TARGET_OPTA
#define HAL_OPTA_H

/**
 * @file opta.h
 * @brief HAL backend for the Arduino OPTA (STM32H747 dual-core ARM).
 *
 * Implements the platform.h interface on top of Mbed OS and the Arduino core.
 * Selected automatically when TARGET_OPTA is defined; see platform.h.
 */

#include <cstdint>
#include <chrono>
#include <ctime>

/**
 * This include provides :
 *  - micros()
 *  - millis()
 *  - mbed::LowPowerClock
 *  - arduino::UART
 *
 */
#include "mbed.h"

struct PLCSharedVarsInput_t;
struct PLCSharedVarsOutput_t;

/// Linker-provided symbol marking the top of the main stack.
extern "C" char _estack;

/// Shared-memory buffers exchanged with the PLC coprocessor (M4 core).
extern "C" void* m_PLCSharedVarsInputBuf;
extern "C" void* m_PLCSharedVarsOutputBuf;



namespace Botanik { namespace HAL {

    /** @name Hardware identity
     *  @{ */

    inline const char* hardware_description() {
        return "Arduino OPTA";
    }

    /** @} */

    /** @name Stack introspection
     *  @{ */

    inline uintptr_t get_stack_base() {
        return reinterpret_cast<uintptr_t>(&_estack);
    }

    inline uint32_t get_stack_depth() {
        volatile uint32_t anchor = 0;
        uintptr_t stack_pointer = reinterpret_cast<uintptr_t>(&anchor);
        return static_cast<uint32_t>(get_stack_base() - stack_pointer);
    }

    /** @} */

    /** @name PLC shared memory
     *  @{ */

    inline void* get_plc_input() {
        return static_cast<PLCSharedVarsInput_t*>(m_PLCSharedVarsInputBuf);
    }

    inline void* get_plc_output() {
        return static_cast<PLCSharedVarsOutput_t*>(m_PLCSharedVarsOutputBuf);
    }

    /** @} */

    /** @name Timing & clocks
     *  @{ */

    inline uint32_t current_ms() {
        return millis();
    }

    inline uint32_t current_us() {
        return micros();
    }

    inline uint64_t uptime_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            mbed::LowPowerClock::now().time_since_epoch()
        ).count();
    }

    inline uint64_t uptime_us() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            mbed::LowPowerClock::now().time_since_epoch()
        ).count();
    }

    inline uint64_t wall_clock_ms() {
        // TODO: read the RTC sub-second register for true millisecond resolution;
        // time() only resolves whole seconds.
        return (uint64_t)time(NULL) * 1000;
    }

    /** @} */

    /** @name Memory ordering
     *  @{ */

    inline void dsb() {
        __DSB();
    }

    template <typename T>
    inline void ordered_copy(T* dst, const T* src) {
        ARM_MPU_OrderedMemcpy((void*)dst, (const void*)src, sizeof(T));
        __DSB();
    }

    template<typename T>
    inline void ordered_move(T* dst, T src) {
        *dst = src;
        __DSB();
    }

    /** @} */

    /** @name Serial I/O
     *  @{ */

    /// On the OPTA, the raw serial type is the Arduino UART driver itself.
    using RawSerial = arduino::UART;

    /** @brief Logical serial endpoints: CONSOLE drives the interactive terminal,
     *  MONITOR the secondary diagnostic/log output. */
    enum SerialPort { CONSOLE, MONITOR };

    /** @brief Resolve a logical SerialPort to its underlying UART.
     *  @param port CONSOLE (Serial) or MONITOR (Serial1). */
    inline RawSerial* get_serial(SerialPort port) {
        if (port == MONITOR) return &Serial1;
        return &Serial;
    }

    /** @} */


}}

#endif // TARGET_OPTA
#endif // HAL_OPTA_H
