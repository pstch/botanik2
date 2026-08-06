// REVIEW_35: TODO
#ifndef HAL_PLATFORM_H
#define HAL_PLATFORM_H

/**
 * @file platform.h
 * @brief Unified hardware abstraction layer (HAL) interface.
 *
 * Declares the platform-independent surface that the rest of the codebase
 * builds on: hardware identity, stack introspection, timing clocks,
 * memory-ordering primitives, and serial access. Exactly one backend
 * implements it, selected at compile time below: opta.h for the Arduino OPTA
 * hardware, simulator.h for the Linux host build.
 *
 * The surface is grouped into nested namespaces by concern (Clock, Memory,
 * Stack, Debug, System, Watchdog, Serial, plus Storage (storage.h) and
 * Network (net.h)). All
 * other code must go through this interface; never reach for raw <time.h> or
 * platform-specific calls directly.
 */

#include <cstdint>                    // uint8_t/uint32_t/uint64_t/uintptr_t - ms stamps, stack addresses

// The one place TARGET_OPTA is allowed to pick a backend: both headers define
// the same HAL::Clock/Memory/Stack/Debug/System/Watchdog surface declared below.
#ifdef TARGET_OPTA
    #include "libbotanik/hal/opta.h"  // the Mbed OS implementation
#else
    #include "libbotanik/hal/simulator.h" // the std::chrono + Linux implementation
#endif

namespace Botanik { namespace HAL {

    /** @brief Host hardware description (board / MCU) for the boot banner;
     *  static string. */
    inline const char* hardware_description();

    /** @brief Stack region introspection (drives the scheduler high-water
     *  metric). */
    namespace Stack {

        /** @brief High address (base) of the current thread's stack region. */
        inline uintptr_t base();

        /** @brief Usable size (depth) of the current thread's stack region,
         *  bytes. */
        inline uint32_t depth();

        /** @brief High-water stack usage (bytes), or 0 if the platform can't
         *  provide it and the caller must measure it (sim paints/scans; OPTA
         *  reads the RTOS watermark). */
        inline uint32_t high();

    }  // namespace Stack

    /** @brief Timing and clocks. */
    namespace Clock {

        /** @brief Sawtooth ms uptime (rolls over ~49 days), low-power clock
         *  (Arduino millis()). */
        inline uint32_t current_ms();

        /** @brief Monotonic ms uptime (no rollover), low-power clock. */
        inline uint64_t uptime_ms();

        /** @brief Monotonic us uptime (no rollover), low-power clock.
         *  @warning The low-power clock lacks true-us precision: on OPTA it runs
         *  at 32.768 kHz, so the smallest measurable interval is ~30 us. */
        inline uint64_t uptime_us();

        /** @brief Best-effort wall clock as UNIX ms. Sub-second where the
         *  platform provides it (sim host clock); whole seconds otherwise (OPTA,
         *  until the RTC sub-second reg). */
        inline uint64_t wall_ms();

        /** @brief Set the wall clock from UNIX ms (future NTP -> RTC). No-op on
         *  the sim (host clock is the source of truth); on OPTA it sets the
         *  RTC. */
        inline void set_wall_ms(uint64_t unix_ms);

        /** @brief Yield the CPU for at least @p ms. Ms granularity: 0 is a
         *  no-op/yield, so sub-ms waits fall through to a busy loop at the call
         *  site. */
        inline void sleep_ms(uint32_t ms);

    }  // namespace Clock

    /** @brief Memory-ordering primitives and D-cache maintenance. */
    namespace Memory {

        /** @brief Ensure all precedent explicit memory accesses are
         *  completed. */
        inline void dsb();

        /** @brief Lighter barrier than dsb(): orders explicit accesses without
         *  waiting for completion. Used for cross-core counter polls. */
        inline void dmb();

        /** @brief Copy a block of memory with hardware-enforced ordering, then
         *  fence. @p T must be a word multiple, and @p dst / @p src must not
         *  alias (the OPTA path declares src __RESTRICT, so an overlapping copy
         *  is UB there).
         *
         *  @note Currently unused. Kept for the M4 core: publishing a block to
         *  a second core is what this is for, and it is the kind of primitive
         *  that must be right rather than rewritten in a hurry. Do not remove
         *  it as dead code. */
        template <typename T>
        inline void ordered_copy(T* dst, const T* src);

        /** @brief Push a single value into a memory location, then fence - the
         *  scalar counterpart of ordered_copy().
         *
         *  @note Currently unused; kept for the M4 core. See ordered_copy(). */
        template <typename T>
        inline void ordered_move(T* dst, T src);

        /** @brief Read a value the other core may be writing: a volatile load
         *  (never cached in a register) plus dmb() for acquire ordering. */
        template <typename T>
        inline T ordered_read(const volatile T* src);

        /** @brief Clean (write back) the D-cache over [addr, addr+len) after
         *  writing a shared region, so the other core sees the data in SRAM.
         *  Rounds to whole cache lines. No-op without a D-cache (the simulator,
         *  or a Cortex-M4).
         *
         *  Always safe: it only ever writes back, so a rounded-up line loses
         *  nothing.
         *
         *  @note Currently unused; kept for the M4 core. See ordered_copy(). */
        inline void cache_clean(const void* addr, size_t len);

        /** @brief Invalidate the D-cache over [addr, addr+len) before reading a
         *  shared region, so a stale cached copy cannot hide the other core's
         *  writes. Rounds to whole cache lines. No-op without a D-cache.
         *
         *  @warning Unlike cache_clean(), this DISCARDS whole lines. It is safe
         *  only on a region we treat as read-only. Invalidating a line that
         *  holds something we wrote silently drops that write - so pair the two
         *  by direction: invalidate what the other core produces, clean what we
         *  produce, and keep the two regions on separate cache lines.
         *
         *  @note Currently unused; kept for the M4 core. See ordered_copy(). */
        inline void cache_invalidate(const void* addr, size_t len);

    }  // namespace Memory

    /** @brief Debugger interaction. */
    namespace Debug {

        /** @brief Trap into an attached debugger (host SIGTRAP, OPTA BKPT). */
        inline void breakpoint();

        /** @brief True if a debugger is attached (host: ptrace tracer; OPTA:
         *  Cortex-M halting-debug-enable bit). Gate breakpoint() on this: a bare
         *  BKPT with no debugger HardFaults on OPTA. */
        inline bool under_debugger();

    }  // namespace Debug

    /** @brief System lifecycle (reset / shutdown). */
    namespace System {

        /** @brief Reboot the firmware; does not return. OPTA: hardware reset
         *  (NVIC_SystemReset). Sim: re-execs the process image with the original
         *  argv after running any on_reset teardown hook. */
        inline void reset();

        /** @brief Shut down; does not return. Sim: runs the on_reset hook, then
         *  exits with status @p code (default 0; atexit restores the console;
         *  pass 128+signo for a signal exit). OPTA: no clean halt, so falls back
         *  to reset() and @p code is ignored. */
        inline void shutdown(int code = 0);

    }  // namespace System

    /** @brief Hardware watchdog. */
    namespace Watchdog {

        /** @brief Start the hardware watchdog: not kicked within @p timeout_ms
         *  -> the firmware resets. OPTA: the MCU's independent watchdog
         *  peripheral. Sim: serviced from its own thread (independent of the
         *  main loop, so a wedged loop still trips it). */
        inline void start(uint32_t timeout_ms);

        /** @brief Reset the watchdog countdown; call once per healthy
         *  iteration. */
        inline void kick();

        /** @brief Stop the watchdog (no-op if not started). */
        inline void stop();

    }  // namespace Watchdog

    /** @brief Process environment. Host-only in practice: the OPTA backend has
     *  no environment and always yields the fallback. Lives here rather than in
     *  utils/ because reading it is a platform call. */
    namespace Env {

        /** @brief Environment variable @p name as a uint32.
         *  @returns @p fallback if unset, empty, unparseable or out of range. */
        uint32_t u32(const char* name, uint32_t fallback = 0);

        /** @brief Environment variable @p name as a uint8.
         *  @returns @p fallback if unset or it does not fit a byte. */
        uint8_t  u8(const char* name, uint8_t fallback = 0);

    }  // namespace Env

    /** @brief Serial access (Serial::Raw, Serial::get(), ports) is
     *  intentionally *not* re-declared here: the types and (host-only)
     *  setup_raw_console() are platform-specific and live in the selected
     *  backend header (opta.h / simulator.h). The network transport lives in
     *  the HAL::Network namespace (net.h). */

}}

#endif // HAL_PLATFORM_H
