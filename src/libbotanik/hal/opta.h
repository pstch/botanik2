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
#include <type_traits>

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

    inline void sleep_ms(uint32_t ms) {
        rtos::ThisThread::sleep_for(std::chrono::milliseconds(ms));
    }

    /** @} */

    /** @name Memory ordering
     *  @{ */

    inline void dsb() {
        __DSB();
    }

    inline void dmb() {
        __DMB();
    }

    template <typename T>
    inline void ordered_copy(T* dst, const T* src) {
        // ARM_MPU_OrderedMemcpy counts 32-bit words, not bytes. Empty payloads copy nothing.
        static_assert(std::is_empty<T>::value || sizeof(T) % sizeof(uint32_t) == 0,
                      "ordered_copy needs a word-multiple T");
        ARM_MPU_OrderedMemcpy((volatile uint32_t*)dst, (const uint32_t*)src, sizeof(T) / sizeof(uint32_t));
        __DSB();
    }

    template<typename T>
    inline void ordered_move(T* dst, T src) {
        static_assert(std::is_empty<T>::value || sizeof(T) % sizeof(uint32_t) == 0,
                      "ordered_move needs a word-multiple T");
        *dst = src;
        __DSB();
    }

    template <typename T>
    inline T ordered_read(const volatile T* src) {
        T value = *src;   // volatile load: the compiler must re-read it
        __DMB();          // acquire: order later accesses after this read
        return value;
    }

    inline void cache_clean(const void* addr, size_t len) {
    #if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        SCB_CleanDCache_by_Addr((void*)addr, (int32_t)len);  // CMSIS rounds to lines
    #else
        (void)addr; (void)len;
    #endif
    }

    inline void cache_invalidate(const void* addr, size_t len) {
    #if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        SCB_InvalidateDCache_by_Addr((void*)addr, (int32_t)len);
    #else
        (void)addr; (void)len;
    #endif
    }

    /** @} */

    /** @name Debug
     *  @{ */

    inline void debug_break() {
        __BKPT(0);  // CMSIS breakpoint intrinsic, provided via mbed.h
    }

    /** @} */

    /** @name System lifecycle
     *  @{ */

    /** @brief No-op on OPTA: a hardware reset clears RAM, so there is no saved
     *  page to restore and the board comes back on the default page. Mirrors the
     *  simulator helper so the terminal can call it unconditionally. */
    inline void save_active_page(uint8_t /*index*/) {}

    inline void reset() {
        // NVIC_SystemReset is the portable CMSIS reset (always available via
        // mbed.h); mbed_reset() is an alternative if a softer Mbed-managed reset
        // is wanted. Compile-reasoned only - confirm on a real arm-none-eabi build.
        NVIC_SystemReset();
    }

    inline void shutdown(int /*code*/) {  // default (0) declared in platform.h
        // Firmware has no clean "halt and return to OS" - there is no OS. The
        // closest sane behaviour is to reboot, so shutdown mirrors reset here.
        // There is no process exit status to carry, so @p code is ignored.
        reset();
    }

    /** @} */

    /** @name Hardware watchdog
     *  @{ */

    // Wrap the Mbed OS Watchdog singleton (the STM32H7 IWDG). Compile-reasoned
    // only - confirm the API and timeout ceiling on a real arm-none-eabi build.

    inline void watchdog_start(uint32_t timeout_ms) {
        // Clamp to the peripheral's max window; the hardware rejects anything above it.
        uint32_t max = mbed::Watchdog::get_instance().get_max_timeout();
        mbed::Watchdog::get_instance().start(timeout_ms > max ? max : timeout_ms);
    }

    inline void watchdog_kick() {
        mbed::Watchdog::get_instance().kick();
    }

    inline void watchdog_stop() {
        mbed::Watchdog::get_instance().stop();
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

    /** @brief A hardware UART is never an interactive terminal, so diagnostic
     *  output is never suppressed on OPTA. Mirrors the simulator query so sinks
     *  (e.g. MonitorLogWriter) stay platform-independent. */
    inline bool serial_is_output_tty(const RawSerial*) { return false; }

    /** @} */


}}

#endif // TARGET_OPTA
#endif // HAL_OPTA_H
