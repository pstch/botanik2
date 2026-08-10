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
 *  Everything cold (the serial bodies, raw-console/termios setup, reset,
 *  shutdown, the stack walk, the watchdog poll) is in hal/platform.cpp, which
 *  keeps the POSIX headers they need out of this one - see its file header. */

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

    /** @brief BOTANIK_SIM_CLOCK=manual: time advances only when the harness asks
     *  (see _virtual_ns), never from the host clock. Read once. */
    bool _manual_clock();

    /** @brief The manual clock's reading, in ns of simulated time. Only the
     *  harness driver writes it; every clock below reads it. */
    uint64_t& _virtual_ns();

    /** @brief Elapsed time since process start, in the duration type T. Under
     *  the manual clock it is the harness's reading instead, which is the only
     *  thing that moves it. */
    template <typename T>
    inline uint64_t _now_as() {
        const int64_t ns_per_unit =
            std::chrono::duration_cast<std::chrono::nanoseconds>(T(1)).count();
        if (_manual_clock()) return (uint64_t)(_virtual_ns() / (uint64_t)ns_per_unit);
        return (uint64_t)(std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now() - _process_start()).count()
               / ns_per_unit);
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

    /** @brief HOST ms since process start, whatever the firmware's clock is
     *  doing: under the manual clock uptime_ms() advances only when the harness
     *  says so, and a rate measured against it would read as infinite.
     *  Simulator-only. */
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
        // build catches violations the hardware cannot surface.
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
     *  platform.cpp. */
    class Raw {
        /** @brief Simulated UART FIFO depth (bytes); also caps the token
         *  bucket's bank. */
        static constexpr int FIFO_BYTES = 64;

        /** @name State
         *  @{ */

        int _fd_in = -1;             ///< -1 if unbound
        int _fd_out = -1;            ///< -1 if unbound
        uint32_t _baud = 0;          ///< drives the write throttle
        /** @brief Bits on the wire per byte, including start, parity and stop.
         *  10 is 8N1, which is what CONSOLE and MONITOR are; the field bus sets
         *  its own, since Modbus RTU spends eleven either way (8E1, or 8N1 with
         *  a second stop bit). */
        uint32_t _bits_per_char = 10;
        /** @brief Per-byte slot (us) and its FIFO-deep bank, derived in
         *  begin() from the baud and the frame format. */
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

        /** @brief Begin at @p baud with a frame of a stated shape, so the
         *  throttle models the real byte time. Mirrors Rs485Raw::format(). */
        inline void format(uint32_t baud, uint8_t data_bits, uint8_t parity_bits,
                           uint8_t stop_bits) {
            _bits_per_char = 1u + data_bits + parity_bits + stop_bits;
            begin(baud);
        }

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

        /** @brief Whether everything written has finished leaving the wire. The
         *  token bucket already knows: the earliest time the next byte may go
         *  out is the instant the last one finished. */
        inline bool tx_idle() const {
            return _next_write_us <= _now_as<std::chrono::microseconds>();
        }

        /** @brief Discard whatever is sitting in the receive path, and say how
         *  much there was. Bounded, like its OPTA counterpart: nothing inside the
         *  beat drains without a limit. */
        uint16_t flush_input();

        /** @brief Number of bytes currently available to read. */
        int available();

        /** @brief Free space in the simulated UART buffer (0 while the baud
         *  throttle is holding off the next write). */
        int availableForWrite();

        /** @} */
    };

    /** @brief Line format parity. Mirrors the OPTA enum so a caller that sets
     *  8E1 spells it the same way on both targets. */
    enum Parity : uint8_t { PARITY_NONE = 0, PARITY_EVEN, PARITY_ODD };

    /** @brief Logical serial endpoints: CONSOLE drives the interactive
     *  terminal, MONITOR the secondary diagnostic/log output, FIELDBUS the
     *  RS-485 bus the Modbus RTU client talks on. */
    enum Port { CONSOLE, MONITOR, FIELDBUS };

    /** @brief Resolve a logical Port to its backing Raw serial.
     *  CONSOLE is wired to stdin/stdout, MONITOR to stderr (output only).
     *
     *  FIELDBUS is a SOCKETPAIR: the firmware holds one end and the simulated
     *  devices the other (fieldbus_peer_fd()). Bytes really travel, over the
     *  same read()/write() path, so the master's framing, CRCs and timeouts are
     *  exercised rather than emulated. A stream cannot lose a collision, so
     *  nothing here models two devices answering at once; the sim's devices
     *  answer strictly in turn. A pair that cannot be created leaves the port
     *  unbound: a bus with no devices on it.
     *  @param port CONSOLE, MONITOR or FIELDBUS. */
    Raw* get(Port port);

    /** @brief The simulated devices' end of the field bus. -1 when the pair
     *  could not be created. Sim-side only: the firmware never sees it. */
    int& fieldbus_peer_fd();

    /** @brief Whether @p port's output end is an interactive terminal (false
     *  for a null port). Lets sinks (e.g. the MONITOR SerialLogWriter) suppress
     *  output that would garble the UI. */
    bool is_output_tty(const Raw* port);

    /** @name Field bus control
     *
     *  The same three the OPTA backend declares, so a caller never branches on
     *  the target. There is no transceiver to enable and nothing echoes, but the
     *  turnaround is still MODELLED: a client tuned against a port that claimed
     *  to be instantly idle would be tuned for a bus that does not exist.
     *  @{ */

    inline void fieldbus_begin(uint32_t baud, uint8_t data_bits, Parity parity,
                               uint8_t stop_bits) {
        get(FIELDBUS)->format(baud, data_bits,
                              (uint8_t)(parity == PARITY_NONE ? 0 : 1), stop_bits);
    }

    inline bool fieldbus_tx_idle() { return get(FIELDBUS)->tx_idle(); }

    /** @brief Whether the receiver hears our own transmission on this port.
     *
     *  FALSE here: what we write goes to the devices' end of the socketpair,
     *  not back to ours. A real transceiver with RE tied low does hear it, so
     *  this is the one place the two backends genuinely differ - which is why
     *  the master reads it rather than assuming either way. */
    inline bool fieldbus_echoes() { return false; }

    inline uint16_t fieldbus_flush_input() { return get(FIELDBUS)->flush_input(); }

    /** @} */

    /** @brief Put the controlling terminal into raw mode for interactive use:
     *  disable canonical mode and echo, make reads non-blocking, and register
     *  atexit/signal handlers that restore the original settings on exit. */
    void setup_raw_console();

    }  // namespace Serial

    namespace System {

    /** @brief Capture argv and the resolved exe path at the top of main() so
     *  reset() can re-exec. Must resolve the path now, while /proc/self/exe
     *  still points at the running (not-yet-replaced) file. Simulator-only. */
    void set_reboot_args(int argc, char** argv);

    /** @brief A teardown hook run by reset() just before re-exec. */
    using ResetHook = void(*)();

    /** @brief Register a teardown hook invoked by reset() before re-exec, so a
     *  subsystem (e.g. the emulator thread) cleans up first without HAL
     *  depending on it. Sim-only. */
    void on_reset(ResetHook hook);

    }  // namespace System

    namespace Watchdog {

    // Written on the main thread (kick/start/stop), read by poll() on the
    // emulator thread, shared via the codebase's volatile + dsb() idiom (no
    // std::atomic). 64-bit volatile is fine: sim is x86-64 only, where aligned
    // 64-bit loads/stores are atomic.

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
