#ifndef HAL_SIMULATOR_H
#ifdef TARGET_SIMULATOR
#define HAL_SIMULATOR_H

/**
 * @file simulator.h
 * @brief HAL backend for the Linux x86-64 host simulator.
 *
 * Implements the platform.h interface with std::chrono clocks, std::atomic
 * fences, and POSIX serial I/O over file descriptors. Selected automatically
 * when TARGET_SIMULATOR is defined; see platform.h.
 */

#include <cstdint>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <pthread.h>

/** @name Signal handlers
 *  @{ */

/** @brief Single-instance storage for the terminal settings captured before
 *  entering raw mode. */
extern "C" inline struct termios& botanik_saved_console_termios() {
    // Initialized once by setup_raw_console() at startup, never from signal
    // context, so the C++ init guard is never exercised during a handler.
    static struct termios saved;  // NOLINT(bugprone-signal-handler)
    return saved;
}

/** @brief Restore the terminal to its pre-raw-mode settings. */
extern "C" inline void botanik_restore_console() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &botanik_saved_console_termios());
}

/** @brief Signal handler that restores the terminal before exiting, so a
 *  Ctrl+C or crash doesn't leave the user's shell in raw mode. Async-signal-safe:
 *  tcsetattr (via botanik_restore_console) is on the POSIX safe list, and _exit
 *  avoids the non-safe atexit/stdio flush that exit() would run. */
extern "C" inline void botanik_console_signal_handler(int) {
    botanik_restore_console();
    _exit(0);
}

namespace Botanik { namespace HAL {

    /** @name Internal helpers
     *  @{ */

    /** @brief Process-start reference point for the monotonic uptime clocks.
     *  Captured once on first use. */
    inline std::chrono::steady_clock::time_point _process_start() {
        static const auto t0 = std::chrono::steady_clock::now();
        return t0;
    }

    /** @brief Elapsed time since process start, expressed in the duration type T. */
    template <typename T>
    inline uint64_t _now_as() {
        auto duration = std::chrono::steady_clock::now() - _process_start();
        return (uint64_t)std::chrono::duration_cast<T>(duration).count();
    }


    /** @name Hardware identity
     *  @{ */

    inline const char* hardware_description() {
        return "Botanik x86-64 POSIX simulator";
    }

    /** @} */

    /** @name Stack introspection
     *  @{ */

    inline uintptr_t get_stack_base() {
        pthread_t self = pthread_self();
        pthread_attr_t attr;
        if (pthread_getattr_np(self, &attr) == 0) {
            void* addr; size_t size;
            pthread_attr_getstack(&attr, &addr, &size);
            pthread_attr_destroy(&attr);
            return reinterpret_cast<uintptr_t>(addr) + size;
        }
        return 0;
    }

    /** @brief Total allocated stack size for the calling thread.
     *  @note Must NOT return base - current_sp (that is the used amount, not
     *  the depth). */
    inline uint32_t get_stack_depth() {
        pthread_t self = pthread_self();
        pthread_attr_t attr;
        if (pthread_getattr_np(self, &attr) == 0) {
            void* addr; size_t size;
            pthread_attr_getstack(&attr, &addr, &size);
            pthread_attr_destroy(&attr);
            (void)addr;
            return (size > UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(size);
        }
        return 0;
    }

    /** @} */

    /** @name PLC shared memory
     *  @{ */

    /// No PLC coprocessor exists on the host; the regions are absent.
    inline void* get_plc_input()  { return nullptr; }
    inline void* get_plc_output() { return nullptr; }

    /** @} */

    /** @name Timing & clocks
     *  @{ */

    inline uint32_t current_ms() {
        return (uint32_t)_now_as<std::chrono::milliseconds>();
    }

    inline uint32_t current_us() {
        return (uint32_t)_now_as<std::chrono::microseconds>();
    }

    inline uint64_t uptime_ms() {
        return _now_as<std::chrono::milliseconds>();
    }

    inline uint64_t uptime_us() {
        return _now_as<std::chrono::microseconds>();
    }

    inline uint64_t wall_clock_ms() {
        return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    inline void sleep_ms(uint32_t ms) {
        usleep(ms * 1000);  // usleep(0) returns ~immediately, so sub-ms callers busy-loop
    }

    /** @} */

    /** @name Memory ordering
     *  @{ */

    inline void dsb() {
        // Mimic the ARM DSB for thread visibility
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    template <typename T>
    inline void ordered_copy(T* dst, const T* src) {
        std::memcpy((void*)dst, (const void*)src, sizeof(T));
        dsb();
    }

    template<typename T>
    inline void ordered_move(T* dst, T src) {
        *dst = src;
        dsb();
    }

    /** @} */

    /** @name Debug
     *  @{ */

    inline void debug_break() {
        raise(SIGTRAP);  // <csignal> is already included above for the console handlers
    }

    /** @} */

    /** @name Serial I/O
     *  @{ */

    /** @brief Host stand-in for the Arduino UART, backed by a pair of file
     *  descriptors with a baud-rate throttle and a simulated UART buffer. */
    class RawSerial {
        /** @name State
         *  @{ */

        /** @brief Input file descriptor (-1 if unbound). */
        int _fd_in = -1;
        /** @brief Output file descriptor (-1 if unbound). */
        int _fd_out = -1;
        /** @brief Configured baud rate (drives the write throttle). */
        uint32_t _baud = 0;
        /** @brief Earliest time (us) the next write may proceed. */
        uint64_t _next_write_us = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        RawSerial() = default;

        /** @} */

        /** @name I/O
         *  @{ */

        /** @brief Begin operation at the given baud rate (drives write throttling). */
        inline void begin(uint32_t baud) {
            _baud = baud;
            _next_write_us = _now_as<std::chrono::microseconds>();
        }

        /** @brief Read a single byte.
         *  @return The byte read, or -1 if none is available. */
        inline int read() {
            if (_fd_in == -1) return -1;
            uint8_t c;
            if (::read(_fd_in, &c, 1) > 0) return c;
            return -1;
        }

        /** @brief Write a single byte.
         *  @return 1 on success, or 0 if the byte could not be sent (the fd is
         *  non-blocking, so a full kernel buffer returns EAGAIN; the byte stays
         *  queued and the baud throttle is not advanced). */
        inline size_t write(uint8_t c) {
            if (_fd_out == -1) return 0;
            ssize_t n = ::write(_fd_out, &c, 1);
            // Non-blocking fd: a full kernel buffer returns EAGAIN (n < 0). Report
            // no progress so the byte stays queued, and don't advance the baud
            // throttle since nothing was actually sent.
            if (n <= 0) return 0;
            if (_baud > 0) {
                _next_write_us = _now_as<std::chrono::microseconds>() + 10000000ULL / _baud;
            }
            return (size_t)n;
        }

        /** @brief Bind the input/output file descriptors (use -1 to leave an
         *  end unbound). Both ends are set non-blocking so neither a stalled
         *  reader nor a full output buffer can block the main loop. */
        inline void set_fds(int in, int out) {
            _fd_in = in;
            _fd_out = out;
            // Both ends non-blocking so neither a stalled reader nor a full output
            // buffer can block the main loop (which would show up as overruns).
            if (_fd_in != -1) {
                int flags = fcntl(_fd_in, F_GETFL, 0);
                fcntl(_fd_in, F_SETFL, flags | O_NONBLOCK);
            }
            if (_fd_out != -1) {
                int flags = fcntl(_fd_out, F_GETFL, 0);
                fcntl(_fd_out, F_SETFL, flags | O_NONBLOCK);
            }
        }

        /** @brief Number of bytes currently available to read. */
        inline int available() {
            if (_fd_in == -1) return 0;
            int bytes_available = 0;
            if (::ioctl(_fd_in, FIONREAD, &bytes_available) < 0) return 0;
            return bytes_available;
        }

        /** @brief Free space in the simulated UART buffer (0 while the baud
         *  throttle is holding off the next write). */
        inline int availableForWrite() {
            if (_fd_out == -1) return 0;
            if (_baud > 0 && _next_write_us > _now_as<std::chrono::microseconds>()) return 0;
            int pending_bytes = 0;
            // TIOCOUTQ gives us the number of bytes currently in the output buffer
            // NOTE: this should always return when bound to a PTY
            if (::ioctl(_fd_out, TIOCOUTQ, &pending_bytes) < 0) return 0;
            // simulate a small UART buffer
            int space = 64 - pending_bytes;
            return (space < 0) ? 0 : space;
        }

        /** @} */
    };

    /** @brief Logical serial endpoints: CONSOLE drives the interactive terminal,
     *  MONITOR the secondary diagnostic/log output. */
    enum SerialPort { CONSOLE, MONITOR };

    /** @brief Resolve a logical SerialPort to its backing RawSerial.
     *  CONSOLE is wired to stdin/stdout, MONITOR to stderr (output only).
     *  @param port CONSOLE or MONITOR. */
    inline RawSerial* get_serial(SerialPort port) {
        static RawSerial console, monitor;
        static bool initialized = false;
        if (!initialized) {
            console.set_fds(STDIN_FILENO, STDOUT_FILENO);
            monitor.set_fds(-1, STDERR_FILENO);
            initialized = true;
        }
        return (port == MONITOR) ? &monitor : &console;
    }

    /** @} */

    /** @name Console raw mode
     *  @{ */

    /** @brief Put the controlling terminal into raw mode for interactive use:
     *  disable canonical mode and echo, make reads non-blocking, and register
     *  atexit/signal handlers that restore the original settings on exit. */
    inline void setup_raw_console() {
        struct termios& saved = botanik_saved_console_termios();
        tcgetattr(STDIN_FILENO, &saved);
        atexit(botanik_restore_console);
        signal(SIGINT,  botanik_console_signal_handler);
        signal(SIGTERM, botanik_console_signal_handler);
        signal(SIGSEGV, botanik_console_signal_handler);
        struct termios raw = saved;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN]  = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }

    /** @} */

    /** @name System lifecycle
     *  @{ */

    /** @brief Slot holding the argv captured at startup, for re-exec on reset().
     *  The C runtime keeps argv (and its strings) alive for the whole process, so
     *  storing the pointer is enough — no copy needed. */
    inline char*** _reboot_argv() { static char** argv = nullptr; return &argv; }

    /** @brief Capture argv at the top of main() so reset() can re-exec with the
     *  original arguments. Simulator-only (the OPTA reset takes no arguments). */
    inline void set_reboot_args(int /*argc*/, char** argv) { *_reboot_argv() = argv; }

    /** @brief A teardown hook run by reset() just before re-exec. */
    using ResetHook = void(*)();

    /** @brief Slot holding the optional reset hook (a single registrant today). */
    inline ResetHook* _reset_hook() { static ResetHook hook = nullptr; return &hook; }

    /** @brief Register a teardown hook invoked by reset() before re-exec. Lets a
     *  subsystem (e.g. the PlcIO emulator thread) join/clean up first without HAL
     *  taking a dependency on it — the subsystem reaches up into HAL, not vice
     *  versa. Simulator-only. */
    inline void on_reset(ResetHook hook) { *_reset_hook() = hook; }

    inline void reset() {
        // Run the registered teardown (cooperative thread join, etc.) while the
        // image is still intact, then replace the whole process: execve drops all
        // threads and FDs atomically and re-enters main() with the original argv,
        // giving a clean "reboot" with a fresh uptime clock.
        if (ResetHook hook = *_reset_hook()) hook();
        char** argv = *_reboot_argv();
        char*  fallback[] = { (char*)"/proc/self/exe", nullptr };  // if argv was never captured
        execve("/proc/self/exe", argv ? argv : fallback, environ);
        perror("reset: execve failed");  // only reached if the exec itself failed
        _exit(127);
    }

    /** @} */

    /** @name Hardware watchdog
     *  @{ */

    // The watchdog state is written by the caller (kick/start/stop on the main
    // thread) and read by watchdog_poll() on the emulator thread. volatile keeps
    // the poll from caching the values, HAL::dsb() orders the timeout/last-kick
    // pair — the codebase's lock-free idiom, no std::atomic. Safe as 64-bit
    // volatile here because this is simulator-only (x86-64, where aligned 64-bit
    // loads/stores are atomic); on OPTA the watchdog is the hardware peripheral.

    /** @brief Watchdog timeout in ms; 0 means disabled/stopped. */
    inline volatile uint32_t& _wd_timeout_ms()   { static volatile uint32_t t = 0; return t; }
    /** @brief Uptime (ms) of the most recent kick. */
    inline volatile uint64_t& _wd_last_kick_ms() { static volatile uint64_t k = 0; return k; }

    inline void watchdog_start(uint32_t timeout_ms) {
        _wd_last_kick_ms() = uptime_ms();  // seed the kick first...
        dsb();                             // ...so a poll seeing the new timeout can't see a stale kick
        _wd_timeout_ms() = timeout_ms;
    }

    inline void watchdog_kick() {
        _wd_last_kick_ms() = uptime_ms();
    }

    inline void watchdog_stop() {
        _wd_timeout_ms() = 0;
    }

    /** @brief Service the watchdog, calling reset() if the timeout has lapsed.
     *  Simulator-only; invoked from the emulator thread, whose cadence is
     *  independent of the main loop — that independence is what lets the watchdog
     *  fire even when the loop is wedged. */
    inline void watchdog_poll() {
        uint32_t timeout = _wd_timeout_ms();
        if (timeout == 0) return;
        dsb();                             // observe the timeout, then read the kick it was paired with
        if (uptime_ms() - _wd_last_kick_ms() > timeout) reset();
    }

    /** @} */

}}

#endif // TARGET_SIMULATOR
#endif // HAL_SIMULATOR_H
