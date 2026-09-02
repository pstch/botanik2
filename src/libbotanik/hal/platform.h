// REVIEW_35: TODO
#ifndef HAL_PLATFORM_H
#define HAL_PLATFORM_H

/**
 * @file platform.h
 * @brief Unified hardware abstraction layer (HAL) interface.
 *
 * Declares the platform-independent surface the rest of the codebase builds on:
 * hardware identity, stack introspection, clocks, memory-ordering primitives and
 * serial access, grouped into nested namespaces by concern (Clock, Memory,
 * Stack, Debug, System, Watchdog, Serial, plus Storage (storage.h) and Network
 * (net.h)). Exactly one backend implements it, selected below: opta.h for the
 * Arduino OPTA, simulator.h for the Linux host build.
 *
 * All other code must go through this interface; never reach for raw <time.h> or
 * platform-specific calls directly.
 */

#include <cstdint>

// ASAN puts locals on a fake stack, which stack-usage measurement cannot see.
// BOTANIK_NO_ASAN annotates the paint/scan so it works against the real one.
// Here rather than beside its only user: it is a fact about the toolchain, and
// the HAL is what measures the stack.
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define BOTANIK_NO_ASAN __attribute__((no_sanitize("address")))
#  endif
#elif defined(__SANITIZE_ADDRESS__)
#  define BOTANIK_NO_ASAN __attribute__((no_sanitize("address")))
#endif
#ifndef BOTANIK_NO_ASAN
#  define BOTANIK_NO_ASAN
#endif

// The one place TARGET_OPTA is allowed to pick a backend: both headers define
// the same HAL::Clock/Memory/Stack/Debug/System/Watchdog surface declared below.
#ifdef TARGET_OPTA
    #include "libbotanik/hal/opta.h"        // the Mbed OS implementation
#else
    #include "libbotanik/hal/simulator.h"   // the std::chrono + Linux implementation
#endif

namespace Botanik { namespace HAL {

    /** @brief Host hardware description (board / MCU) for the boot banner;
     *  static string. */
    const char* hardware_description();

    /** @brief Stack introspection: one question, one answer.
     *
     *  The region itself is deliberately not exposed. Its base and its depth
     *  came from two DIFFERENT stacks on the OPTA - the MSP exception stack and
     *  the RTOS thread the firmware runs on - and a caller doing its own
     *  arithmetic over the pair had no way to know. */
    namespace Stack {

        /** @brief One measurement of the current thread's stack. Read
         *  TOGETHER; a fraction is only meaningful over one region. */
        struct Use {
            uint32_t used;    ///< high-water bytes ever used; 0 = not measurable
            uint32_t depth;   ///< total allocated bytes; 0 = unknown
        };

        /** @brief Measure the CURRENT thread's stack.
         *
         *  OPTA reads the RTOS watermark, which is ALL-TIME and cannot fall.
         *  The host has none, so it scans a sentinel painted by the PREVIOUS
         *  call and repaints below this frame - which makes the call itself the
         *  sampling cadence, and lets the host figure fall again.
         *  @warning Not free on the host: it scans and repaints up to 64 KB.
         *  Call it on a slice boundary, never every beat. */
        Use high_water();

    }  // namespace Stack

    /** @brief Timing and clocks.
     *
     *  Declared without `inline` even for the per-beat readings: the OPTA
     *  backend defines them in platform.cpp (the simulator's stay inline in its
     *  own header), and an `inline` here would apply to both, leaving the OPTA
     *  link looking for a definition no translation unit has. */
    namespace Clock {

        /** @brief Sawtooth ms uptime (rolls over ~49 days), low-power clock
         *  (Arduino millis()). */
        uint32_t current_ms();

        /** @brief Monotonic ms uptime (no rollover), low-power clock. */
        uint64_t uptime_ms();

        /** @brief Monotonic us uptime (no rollover), low-power clock.
         *  @warning The low-power clock lacks true-us precision: on OPTA it runs
         *  at 32.768 kHz, so the smallest measurable interval is ~30 us. */
        uint64_t uptime_us();

        /** @brief Best-effort wall clock as UNIX ms. Sub-second where the
         *  platform provides it (sim host clock); whole seconds otherwise (OPTA,
         *  until the RTC sub-second reg). */
        uint64_t wall_ms();

        /** @brief Set the wall clock from UNIX ms (future NTP -> RTC). No-op on
         *  the sim (host clock is the source of truth); on OPTA it sets the
         *  RTC. */
        void set_wall_ms(uint64_t unix_ms);

        /** @brief A free-running high-resolution tick, for measuring INTERVALS.
         *
         *  Wraps. Subtract two readings as unsigned and the wrap takes care of
         *  itself for any interval shorter than the wrap period - ~9 s on the
         *  OPTA at 480 MHz, ~71 min on the host.
         *  @warning Not a clock: no epoch, no relation to uptime, and not
         *  comparable across a reset. For "how long did that take", never for
         *  "what time is it" - which is uptime_ms() and wall_ms(). */
        uint32_t hrt_ticks();

        /** @brief A hrt_ticks() DELTA as microseconds.
         *  @param ticks a difference of two hrt_ticks() readings, never one of
         *  them. */
        uint32_t hrt_us(uint32_t ticks);

        /** @brief Yield the CPU for at least @p ms. Ms granularity: 0 is a
         *  no-op/yield, so sub-ms waits fall through to a busy loop at the call
         *  site. */
        void sleep_ms(uint32_t ms);

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
         *  @note Currently unused; kept for the M4 core (publishing a block to
         *  a second core is what it is for). Do not remove as dead code. */
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
         *  or a Cortex-M4). Always safe: it only ever writes back, so a
         *  rounded-up line loses nothing.
         *
         *  @note Currently unused; kept for the M4 core. See ordered_copy(). */
        inline void cache_clean(const void* addr, size_t len);

        /** @brief Invalidate the D-cache over [addr, addr+len) before reading a
         *  shared region, so a stale cached copy cannot hide the other core's
         *  writes. Rounds to whole cache lines. No-op without a D-cache.
         *
         *  @warning Unlike cache_clean(), this DISCARDS whole lines, so it is
         *  safe only on a region we treat as read-only: invalidating a line
         *  holding something we wrote silently drops that write. Pair the two by
         *  direction (invalidate what the other core produces, clean what we
         *  produce) and keep the two regions on separate cache lines.
         *
         *  @note Currently unused; kept for the M4 core. See ordered_copy(). */
        inline void cache_invalidate(const void* addr, size_t len);

    }  // namespace Memory

    /** @brief Debugger interaction. */
    namespace Debug {

        /** @brief Trap into an attached debugger (host SIGTRAP, OPTA BKPT). */
        void breakpoint();

        /** @brief True if a debugger is attached (host: ptrace tracer; OPTA:
         *  Cortex-M halting-debug-enable bit). Gate breakpoint() on this: a bare
         *  BKPT with no debugger HardFaults on OPTA. */
        bool under_debugger();

    }  // namespace Debug

    /** @brief System lifecycle (reset / shutdown). */
    namespace System {

        /** @brief Reboot the firmware; does not return. OPTA: hardware reset
         *  (NVIC_SystemReset). Sim: re-execs the process image with the original
         *  argv after running any on_reset teardown hook. */
        void reset();

        /** @brief Shut down; does not return. Sim: runs the on_reset hook, then
         *  exits with status @p code (0 for "exit", 128+signo on a signal).
         *  OPTA: no clean halt, so falls back to reset() and @p code is
         *  ignored. */
        void shutdown(int code = 0);

    }  // namespace System

    /** @brief Hardware watchdog. */
    namespace Watchdog {

        /** @brief Start the hardware watchdog: not kicked within @p timeout_ms
         *  -> the firmware resets. OPTA: the MCU's independent watchdog
         *  peripheral. Sim: serviced from its own thread (independent of the
         *  main loop, so a wedged loop still trips it). */
        void start(uint32_t timeout_ms);

        /** @brief Reset the watchdog countdown; call once per healthy
         *  iteration. */
        /*  Not declared inline: the OPTA backend defines it in platform.cpp,
         *  while the simulator's stays inline in its own backend header. */
        void kick();

        /** @brief Stop the watchdog (no-op if not started). */
        void stop();

    }  // namespace Watchdog

    /** @brief The controller's own digital input terminals, read as LEVELS.
     *  Pulse below counts edges on the same eight; a contact that sits open or
     *  closed is this one. */
    namespace DigitalIn {

        /** @brief The local input terminals: I1 is 0, I8 is 7. */
        static const uint8_t CHANNELS = 8;

        /** @brief Make @p ch an input.
         *  @return false if the channel is out of range.
         *  @warning Call from setup(): the OPTA path configures the pin. */
        bool attach(uint8_t ch);

        /** @brief The level on @p ch now. False for a channel never attached,
         *  which is also what an open terminal reads. */
        bool read(uint8_t ch);

        /** @brief Drive @p ch by hand. Simulator only, and a no-op on the
         *  OPTA: a real terminal is driven by the wire. */
        void inject(uint8_t ch, bool level);

    }  // namespace DigitalIn

    /** @brief Process environment. Host-only in practice: the OPTA backend has
     *  no environment and always yields the fallback. Lives here rather than in
     *  utils/ because reading it is a platform call. */
    namespace Pulse {

        /** @brief Which edge of the input advances the count. LOW and HIGH are
         *  absent deliberately: a level is not an event, and the OPTA core maps
         *  every mode that is not CHANGE or FALLING onto RISING anyway. */
        enum Edge : uint8_t { EDGE_RISING, EDGE_FALLING, EDGE_BOTH };

        /** @brief How many local channels there are to count on: the same
         *  terminals DigitalIn reads as levels. What BOUNDS an index. */
        static const uint8_t CHANNELS = DigitalIn::CHANNELS;

        /** @brief The most that may count AT ONCE. I1 and I4 share an interrupt
         *  line and cannot both carry one, so seven of the eight is the real
         *  ceiling. A CAPACITY, never an index bound - sizing an array with it
         *  worked only because the two happen to differ by one. */
        static const uint8_t MAX_CHANNELS = CHANNELS - 1;

        /** @brief A count and the instant it was taken, read together.
         *  Separately, a rate would be computed from two moments that never
         *  coexisted - the error is small but always in the same direction. */
        struct Sample {
            uint32_t count;    ///< edges since attach(); wraps at 2^32
            uint64_t at_ms;    ///< uptime when the count was read
        };

        /** @brief Count edges on local channel @p ch (0 = I1, 7 = I8).
         *  IDEMPOTENT: a channel already counting @p edge is already in the
         *  state asked for, and answers true.
         *  @return false if the channel is out of range, would collide with one
         *  that is attached (I1 and I4 share a line), or is already counting a
         *  DIFFERENT edge.
         *  @warning Call from setup(). The OPTA core allocates on attach. */
        bool attach(uint8_t ch, Edge edge);

        /** @brief @p ch's count and the instant of the read.
         *  Re-reads until the count is stable, so an edge arriving mid-read
         *  cannot pair a new count with an old timestamp. */
        Sample sample(uint8_t ch);

        /** @brief Edges dropped because they arrived faster than the input can
         *  resolve. Non-zero means the meter is over-running the channel and
         *  the volume it reports is an UNDER-estimate. */
        uint32_t overruns(uint8_t ch);

        /** @brief Feed @p ch an edge by hand. Simulator only, and a no-op on the
         *  OPTA: a simulated field has no wire to pulse. */
        void inject(uint8_t ch, uint32_t edges);

    }  // namespace Pulse

    namespace Env {

        /** @brief Environment variable @p name as a uint32.
         *  @returns @p fallback if unset, empty, unparseable or out of range. */
        uint32_t u32(const char* name, uint32_t fallback = 0);

        /** @brief Environment variable @p name, or @p fallback if unset or
         *  empty. Never null when @p fallback is not. OPTA has no environment,
         *  so it always answers @p fallback. */
        const char* str(const char* name, const char* fallback = nullptr);

        /** @brief Environment variable @p name as a uint8.
         *  @returns @p fallback if unset or it does not fit a byte. */
        uint8_t  u8(const char* name, uint8_t fallback = 0);

    }  // namespace Env

    /** @brief Serial access (Serial::Raw, Serial::get(), ports) is deliberately
     *  *not* re-declared here: the types and the host-only setup_raw_console()
     *  are platform-specific and live in the selected backend header. The
     *  network transport is HAL::Network (net.h). */

}}

#endif // HAL_PLATFORM_H
