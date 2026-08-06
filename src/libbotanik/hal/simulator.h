// REVIEW_35: DONE
#ifndef HAL_SIMULATOR_H
#define HAL_SIMULATOR_H

/** @file simulator.h
 *  @brief Linux/host backend for the HAL - the parts that must be inline.
 *
 *  Reached only through hal/platform.h, which picks this or opta.h. What lives
 *  here is what a caller cannot afford a function call for (the clocks and the
 *  memory barriers, both read every beat), the templates, and the TYPES a
 *  caller needs by value (Serial::Raw, Serial::Port, System::ResetHook).
 *
 *  @section sim_split Why the rest is in platform.cpp
 *
 *  Everything cold - the serial device's bodies, raw-console/termios setup,
 *  reset, shutdown, the stack walk, the watchdog thread's poll - lives in
 *  hal/platform.cpp, the same arrangement hal/net.{h,cpp} and hal/storage.{h,cpp}
 *  already use. It is not about line count: those bodies need <termios.h>,
 *  <pthread.h>, <sys/ioctl.h>, <fcntl.h>, <csignal> and <unistd.h>, and this
 *  header is reached by ~30 translation units, so every one of them was paying
 *  for the POSIX preprocessor surface (and inheriting its macros) to call
 *  uptime_ms(). */

#ifdef TARGET_SIMULATOR

#include <atomic>       // std::atomic_thread_fence
#include <cassert>      // assert (the no-alias check in ordered_copy)
#include <chrono>       // std::chrono
#include <cstdint>      // uint8_t/uint64_t/uintptr_t
#include <cstring>      // std::memcpy
#include <type_traits>  // std::is_empty

namespace Botanik { namespace HAL {

    /** @name Host clock helpers
     *  @{ */

    /** @brief Process-start reference for the monotonic uptime clocks; captured
     *  once. */
    std::chrono::steady_clock::time_point _process_start();

    /** @brief BOTANIK_TIME_SCALE: run the sim clocks (and baud throttle) N
     *  times faster so tests finish sooner with unchanged byte/event ratios.
     *  Default 1, clamped >= 1. */
    uint32_t _time_scale();

    /** @brief Elapsed time since process start, expressed in the duration type
     *  T. The time-scale multiply is done in __int128 over nanoseconds (sim is
     *  x86-64 only), so it keeps full precision yet can't overflow at the far
     *  timepoints dilation exists to reach - multiplying the raw ns int64 by
     *  the scale first overflowed in ~days at scale 1000. */
    template <typename T>
    inline uint64_t _now_as() {
        const int64_t ns_per_unit =
            std::chrono::duration_cast<std::chrono::nanoseconds>(T(1)).count();
        __int128 ns = (__int128)std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - _process_start()).count();
        return (uint64_t)(ns * _time_scale() / ns_per_unit);
    }

    /** @} */

    namespace Clock {

    inline uint32_t current_ms() {
        return (uint32_t)_now_as<std::chrono::milliseconds>();
    }

    inline uint64_t uptime_ms() {
        return _now_as<std::chrono::milliseconds>();
    }

    inline uint64_t uptime_us() {
        return _now_as<std::chrono::microseconds>();
    }

    /** @brief Undilated ms uptime: what a wall clock saw, ignoring
     *  BOTANIK_TIME_SCALE. Simulator-only. */
    uint64_t real_uptime_ms();

    /** @brief Host wall clock, as UNIX ms. */
    uint64_t wall_ms();

    /** @brief No-op on the host: the system clock is the source of truth. */
    void set_wall_ms(uint64_t unix_ms);

    }  // namespace Clock

    namespace Memory {

    inline void dsb() {
        // Mimic the ARM DSB for thread visibility
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    inline void dmb() {
        // The sim has no distinct lighter barrier; a full fence is a safe
        // superset.
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    template <typename T>
    inline void ordered_copy(T* dst, const T* src) {
        // Match the OPTA backend's word-multiple + no-alias contract, so the sim
        // build (where DEBUG and the tests run) catches violations the hardware
        // cannot surface.
        static_assert(std::is_empty<T>::value || sizeof(T) % sizeof(uint32_t) == 0,
                      "ordered_copy needs a word-multiple T");
        if (std::is_empty<T>::value) return;   // empty payload carries no data
#if defined(DEBUG)
        assert((const void*)dst != (const void*)src);   // no aliasing
#endif
        std::memcpy((void*)dst, (const void*)src, sizeof(T));
        dsb();
    }

    template <typename T>
    inline void ordered_move(T* dst, T src) {
        static_assert(std::is_empty<T>::value || sizeof(T) % sizeof(uint32_t) == 0,
                      "ordered_move needs a word-multiple T");
        if (std::is_empty<T>::value) return;   // empty payload carries no data
        *dst = src;
        dsb();
    }

    template <typename T>
    inline T ordered_read(const volatile T* src) {
        T value = *src;
        dmb();
        return value;
    }

    /// No D-cache on the host; a shared region is plain memory. See opta.h for
    /// the clean/invalidate discipline these mirror.
    inline void cache_clean(const void* addr, size_t len)      { (void)addr; (void)len; }
    inline void cache_invalidate(const void* addr, size_t len) { (void)addr; (void)len; }

    }  // namespace Memory

    namespace Serial {

    /** @brief Host stand-in for the Arduino UART, backed by a pair of file
     *  descriptors with a baud-rate throttle and a simulated UART buffer.
     *
     *  Declared here (callers hold one by value) with every body in
     *  platform.cpp, so the POSIX headers they need stay out of this header. */
    class Raw {
        /** @brief Simulated UART FIFO depth (bytes); also caps the token
         *  bucket's bank. */
        static constexpr int FIFO_BYTES = 64;

        /** @name State
         *  @{ */

        int _fd_in = -1;             ///< -1 if unbound
        int _fd_out = -1;            ///< -1 if unbound
        uint32_t _baud = 0;          ///< drives the write throttle
        /** @brief 8N1 slot (us/byte) and its FIFO-deep bank, derived once in
         *  begin(). */
        uint64_t _slot_us = 0;
        uint64_t _max_bank_us = 0;
        /** @brief Earliest time (us) the next byte may leave the wire; advanced
         *  by one 8N1 slot per byte sent (token bucket, see write()). */
        uint64_t _next_write_us = 0;
        /** @brief Whether _fd_out is a terminal. False for a file/pipe sink,
         *  where the UART model (baud throttle + TIOCOUTQ) is bypassed so
         *  redirected output drains. */
        bool _out_is_tty = false;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        Raw() = default;

        /** @} */

        /** @name I/O
         *  @{ */

        /** @brief Begin operation at the given baud rate (drives write
         *  throttling). */
        void begin(uint32_t baud);

        /** @brief Read a single byte; -1 if none available. */
        int read();

        /** @brief Write a single byte; 1 on success, 0 if it couldn't be sent
         *  (non-blocking fd: a full kernel buffer EAGAINs, the byte stays
         *  queued, throttle not advanced). */
        size_t write(uint8_t c);

        /** @brief Bind the input/output file descriptors (use -1 to leave an
         *  end unbound). Both ends are set non-blocking so neither a stalled
         *  reader nor a full output buffer can block the main loop. */
        void set_fds(int in, int out);

        /** @brief Whether the output end is an interactive terminal (false for
         *  a file/pipe sink, or while unbound). Lets callers suppress output
         *  that would garble a console. */
        bool is_output_tty() const { return _out_is_tty; }

        /** @brief Number of bytes currently available to read. */
        int available();

        /** @brief Free space in the simulated UART buffer (0 while the baud
         *  throttle is holding off the next write). */
        int availableForWrite();

        /** @} */
    };

    /** @brief Logical serial endpoints: CONSOLE drives the interactive
     *  terminal, MONITOR the secondary diagnostic/log output. */
    enum Port { CONSOLE, MONITOR };

    /** @brief Resolve a logical Port to its backing Raw serial.
     *  CONSOLE is wired to stdin/stdout, MONITOR to stderr (output only).
     *  @param port CONSOLE or MONITOR. */
    Raw* get(Port port);

    /** @brief Whether @p port's output end is an interactive terminal (false
     *  for a null port). Lets sinks (e.g. the MONITOR SerialLogWriter) suppress
     *  output that would garble the UI. */
    bool is_output_tty(const Raw* port);

    /** @brief Put the controlling terminal into raw mode for interactive use:
     *  disable canonical mode and echo, make reads non-blocking, and register
     *  atexit/signal handlers that restore the original settings on exit. */
    void setup_raw_console();

    }  // namespace Serial

    namespace System {

    /** @brief Capture argv and the resolved exe path at the top of main() so
     *  reset() can re-exec. Resolving the path now, while /proc/self/exe still
     *  points at the running (not-yet-replaced) file, records the directory
     *  entry for later re-exec. Simulator-only (the OPTA reset takes no
     *  arguments). */
    void set_reboot_args(int argc, char** argv);

    /** @brief A teardown hook run by reset() just before re-exec. */
    using ResetHook = void(*)();

    /** @brief Register a teardown hook invoked by reset() before re-exec, so a
     *  subsystem (e.g. the emulator thread) cleans up first without HAL
     *  depending on it. Sim-only. */
    void on_reset(ResetHook hook);

    }  // namespace System

    namespace Watchdog {

    // State is written on the main thread (kick/start/stop) and read by
    // poll() on the emulator thread, shared via the codebase's volatile +
    // dsb() idiom (no std::atomic). 64-bit volatile is fine: simulator-only on
    // x86-64, where aligned 64-bit loads/stores are atomic.

    /** @brief Watchdog timeout in ms; 0 means disabled/stopped. */
    volatile uint32_t& _wd_timeout_ms();
    /** @brief Uptime (ms) of the most recent kick. */
    volatile uint64_t& _wd_last_kick_ms();

    /** @brief Reset the countdown. Inline: the main loop kicks every beat. */
    inline void kick() {
        _wd_last_kick_ms() = Clock::uptime_ms();
    }

    /** @brief Service the watchdog when the timeout has lapsed. Simulator-only;
     *  runs on the emulator thread so it can fire even when the main loop is
     *  wedged. Traps (SIGTRAP) when a debugger is attached so it breaks on the
     *  wedged state; otherwise resets the process. */
    void poll();

    }  // namespace Watchdog

}}

#endif // TARGET_SIMULATOR
#endif // HAL_SIMULATOR_H
