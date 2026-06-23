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

    /** @brief Host hardware description (board / MCU) for the boot banner;
     *  static string. */
    inline const char* hardware_description();

    /** @} */

    /** @name Stack introspection
     *  @{ */

    /** @brief High address (base) of the current thread's stack region. */
    inline uintptr_t get_stack_base();

    /** @brief Usable size (depth) of the current thread's stack region,
     *  bytes. */
    inline uint32_t get_stack_depth();

    /** @brief High-water stack usage (bytes), or 0 if the platform can't
     *  provide it and the caller must measure it (sim paints/scans; OPTA reads
     *  the RTOS watermark). */
    inline uint32_t get_stack_high();

    /** @} */

    /** @name PLC shared memory
     *  @{ */

    inline void* get_plc_input();
    inline void* get_plc_output();

    /** @} */

    /** @name Timing & clocks
     *  @{ */

    /** @brief Sawtooth ms uptime (rolls over ~49 days), low-power clock
     *  (Arduino millis()). */
    inline uint32_t current_ms();

    /** @brief Sawtooth us uptime (rolls over ~71 min), regular clock (Arduino
     *  micros()).
     *  @warning Does not run in SLEEP mode; use only for short RUN-mode
     *  intervals. */
    inline uint32_t current_us();

    /** @brief Monotonic ms uptime (no rollover), low-power clock. */
    inline uint64_t uptime_ms();

    /** @brief Monotonic us uptime (no rollover), low-power clock.
     *  @warning The low-power clock lacks true-us precision: on OPTA it runs at
     *  32.768 kHz, so the smallest measurable interval is ~30 us. */
    inline uint64_t uptime_us();

    /** @brief Best-effort wall clock as UNIX ms. Sub-second where the platform
     *  provides it (sim host clock); whole seconds otherwise (OPTA, until the
     *  RTC sub-second reg). */
    inline uint64_t wall_clock_ms();

    /** @brief Set the wall clock from UNIX ms (future NTP -> RTC). No-op on the
     *  sim (host clock is the source of truth); on OPTA it sets the RTC. */
    inline void wall_clock_set(uint64_t unix_ms);

    /** @brief Yield the CPU for at least @p ms. Ms granularity: 0 is a
     *  no-op/yield, so sub-ms waits fall through to a busy loop at the call
     *  site. */
    inline void sleep_ms(uint32_t ms);

    /** @} */

    /** @name Memory ordering
     *  @{ */

    /** @brief Ensure all precedent explicit memory accesses are completed. */
    inline void dsb();

    /** @brief Lighter barrier than dsb(): orders explicit accesses without
     *  waiting for completion. Used for cross-core counter polls. */
    inline void dmb();

    /** @brief Copy a block of memory with hardware-enforced ordering. */
    template <typename T>
    inline void ordered_copy(T* dst, const T* src);

    /** @brief Atomically push a value into a memory location. */
    template<typename T>
    inline void ordered_move(T* dst, T src);

    /** @brief Read a value the other core may be writing: a volatile load
     *  (never cached in a register) plus dmb() for acquire ordering. */
    template <typename T>
    inline T ordered_read(const volatile T* src);

    /** @brief Clean (write back) the D-cache over [addr, addr+len) after
     *  writing a shared region, so the other core sees the data in SRAM. No-op
     *  without a D-cache (simulator, or a Cortex-M4). Rounds to whole cache
     *  lines. */
    inline void cache_clean(const void* addr, size_t len);

    /** @brief Invalidate the D-cache over [addr, addr+len) before reading a
     *  shared region, so a stale cached copy can't hide the other core's
     *  writes. No-op without a D-cache. Rounds to whole cache lines. */
    inline void cache_invalidate(const void* addr, size_t len);

    /** @} */

    /** @name Debug
     *  @{ */

    /** @brief Trap into an attached debugger (host SIGTRAP, OPTA BKPT). */
    inline void debug_break();

    /** @brief True if a debugger is attached (host: ptrace tracer; OPTA:
     *  Cortex-M halting-debug-enable bit). Gate debug_break() on this: a bare
     *  BKPT with no debugger HardFaults on OPTA. */
    inline bool under_debugger();

    /** @} */

    /** @name System lifecycle
     *  @{ */

    /** @brief Reboot the firmware; does not return. OPTA: hardware reset
     *  (NVIC_SystemReset). Sim: re-execs the process image with the original
     *  argv after running any on_reset teardown hook. */
    inline void reset();

    /** @brief Shut down; does not return. Sim: runs the on_reset hook, then
     *  exits with status @p code (default 0; atexit restores the console; pass
     *  128+signo for a signal exit). OPTA: no clean halt, so falls back to
     *  reset() and @p code is ignored. */
    inline void shutdown(int code = 0);

    /** @} */

    /** @name Hardware watchdog
     *  @{ */

    /** @brief Start the hardware watchdog: not kicked within @p timeout_ms ->
     *  the firmware resets. Distinct from the PLC software watchdog
     *  (plcio/sync.h), which guards comms liveness, not the whole firmware.
     *  OPTA: the MCU's independent watchdog peripheral. Sim: serviced from the
     *  emulator thread (independent of the main loop, so a wedged loop still
     *  trips it). */
    inline void watchdog_start(uint32_t timeout_ms);

    /** @brief Reset the watchdog countdown; call once per healthy iteration. */
    inline void watchdog_kick();

    /** @brief Stop the watchdog (no-op if not started). */
    inline void watchdog_stop();

    /** @} */

    /** @name Serial I/O & console
     *  @{ */

    /** @brief Serial access is intentionally *not* re-declared here: RawSerial,
     *  get_serial() and (host-only) setup_raw_console() are platform-specific
     *  and live in the selected backend header (opta.h / simulator.h). */

    /** @} */

}}

#endif // HAL_PLATFORM_H
