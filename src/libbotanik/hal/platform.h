#ifndef HAL_PLATFORM_H
#define HAL_PLATFORM_H

/**
 * @file platform.h
 * @brief Unified hardware abstraction layer (HAL) interface.
 *
 * Declares the platform-independent surface that the rest of the codebase
 * builds on: hardware identity, stack introspection, PLC shared memory, timing
 * clocks, memory-ordering primitives, and serial access. Exactly one backend
 * implements it, selected at compile time below: opta.h for the Arduino OPTA
 * hardware, simulator.h for the Linux host build.
 *
 * All other code must go through this interface; never reach for raw <time.h>
 * or platform-specific calls directly.
 */

#include <cstdint>
#include <cstring>

#ifdef TARGET_OPTA
    #include "libbotanik/hal/opta.h"
#else
    #include "libbotanik/hal/simulator.h"
#endif

namespace Botanik { namespace HAL {

    /** @name Hardware identity
     *  @{ */

    /** @brief Human-readable description of the host hardware (board / MCU),
     *  for the boot banner. Platform-specific; returns a static string. */
    inline const char* hardware_description();

    /** @} */

    /** @name Stack introspection
     *  @{ */

    /** @brief Return the high address (base) of the current thread's stack region. */
    inline uintptr_t get_stack_base();

    /** @brief Return the usable size (depth) of the current thread's stack region in bytes. */
    inline uint32_t get_stack_depth();

    /** @} */

    /** @name PLC shared memory
     *  @{ */

    /** @brief Get the pointer to the PLC input memory region. */
    inline void* get_plc_input();

    /** @brief Get the pointer to the PLC output memory region. */
    inline void* get_plc_output();

    /** @} */

    /** @name Timing & clocks
     *  @{ */

    /** @brief Sawtooth millisecond uptime counter (rolls over every ~49 days),
     *  using the low-power clock (equivalent to Arduino's millis()). */
    inline uint32_t current_ms();

    /** @brief Sawtooth microsecond uptime counter (rolls over every ~71 minutes),
     *  using the regular clock (equivalent to Arduino's micros()).
     *  @warning This counter does not run when the device is in SLEEP mode; it
     *  should only be used for short intervals during which the device is
     *  guaranteed to stay in RUN mode. */
    inline uint32_t current_us();

    /** @brief Monotonic millisecond uptime counter (no rollover), using the
     *  low-power clock. */
    inline uint64_t uptime_ms();

    /** @brief Monotonic microsecond uptime counter (no rollover), using the
     *  low-power clock.
     *  @warning The low-power clock used for this counter will generally not
     *  have enough precision for true microseconds. On an Arduino OPTA, this
     *  clock runs at 32.768 kHz, so the smallest interval it can measure is
     *  around ~30 microseconds. */
    inline uint64_t uptime_us();

    /** @brief Best-effort wall-clock time as UNIX milliseconds. Carries
     *  sub-second resolution where the platform provides it (host clock on the
     *  simulator); whole seconds otherwise (OPTA, until the RTC sub-second
     *  register is wired). */
    inline uint64_t wall_clock_ms();

    /** @brief Yield the CPU for at least @p ms milliseconds. Millisecond
     *  granularity: a request of 0 is a no-op/yield, so sub-millisecond waits
     *  fall through to a busy loop at the call site. */
    inline void sleep_ms(uint32_t ms);

    /** @} */

    /** @name Memory ordering
     *  @{ */

    /** @brief Ensure that all precedent explicit memory accesses are completed. */
    inline void dsb();

    /** @brief Copy a block of memory with hardware-enforced ordering. */
    template <typename T>
    inline void ordered_copy(T* dst, const T* src);

    /** @brief Atomically push a value into a memory location. */
    template<typename T>
    inline void ordered_move(T* dst, T src);

    /** @} */

    /** @name Debug
     *  @{ */

    /** @brief Trap into an attached debugger (SIGTRAP on the host, a hardware
     *  breakpoint instruction on the OPTA). A no-op-ish trap if nothing is
     *  attached, depending on the platform's default signal/fault handling. */
    inline void debug_break();

    /** @} */

    /** @name Serial I/O & console
     *  @{ */

    /** @brief Serial access is intentionally *not* re-declared here: the RawSerial
     *  type and get_serial() are platform-specific (an Arduino UART on OPTA, a
     *  file-descriptor stand-in on the simulator), and setup_raw_console() exists
     *  only on the host. They are declared in the selected backend header above
     *  (opta.h / simulator.h) rather than in this unified surface. */

    /** @} */

}}

#endif // HAL_PLATFORM_H
