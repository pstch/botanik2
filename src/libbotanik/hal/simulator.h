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

/** @brief Pre-raw-mode terminal settings. Pointer return (not reference) keeps
 *  the extern "C" linkage the signal-handler call chain needs. */
extern "C" inline struct termios* botanik_saved_console_termios() {
    static struct termios saved;  // NOLINT(bugprone-signal-handler)
    return &saved;
}

/** @brief Restore the terminal to its pre-raw-mode settings. Async-signal-safe:
 *  TCSAFLUSH drops queued output, so a final transmission must already have
 *  drained (see botanik_restore_console_clean) before this runs. */
extern "C" inline void botanik_restore_console() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, botanik_saved_console_termios());
}

/** @brief Clean-exit console teardown (not signal-safe). Pages leave the cursor
 *  hidden and parked mid-row, so hand the shell back a sane terminal: show the
 *  cursor, reset attributes, and a fresh line, written straight to the wire (the
 *  final transmission). Then leave raw mode with TCSADRAIN - not TCSAFLUSH - so
 *  those bytes drain instead of being discarded. */
inline void botanik_restore_console_clean() {
    if (::isatty(STDIN_FILENO) != 1) return;
    static const char tail[] = "\033[?25h\033[0m\r\n";  // DECTCEM show, SGR reset, CR LF
    (void)::write(STDOUT_FILENO, tail, sizeof(tail) - 1);
    tcsetattr(STDIN_FILENO, TCSADRAIN, botanik_saved_console_termios());
}

/** @brief Restore the terminal before exiting so Ctrl+C or a crash doesn't leave
 *  the shell in raw mode. Async-signal-safe (tcsetattr + _exit). */
extern "C" inline void botanik_console_signal_handler(int) {
    botanik_restore_console();
    _exit(0);
}

/** @} */

namespace Botanik { namespace HAL {

    /** @name Internal helpers
     *  @{ */

    /** @brief Process-start reference point for the monotonic uptime clocks.
     *  Captured once on first use. */
    inline std::chrono::steady_clock::time_point _process_start() {
        static const auto t0 = std::chrono::steady_clock::now();
        return t0;
    }

    /** @brief BOTANIK_TIME_SCALE: run the sim clocks (and the baud throttle) N
     *  times faster so tests finish sooner with unchanged byte/event ratios.
     *  Default 1, clamped >= 1, read once. */
    inline uint32_t _time_scale() {
        static const uint32_t scale = []() -> uint32_t {
            const char* s = getenv("BOTANIK_TIME_SCALE");
            long v = s ? atol(s) : 1;
            return v < 1 ? 1u : (uint32_t)v;
        }();
        return scale;
    }

    /** @brief Elapsed time since process start, expressed in the duration type T. */
    template <typename T>
    inline uint64_t _now_as() {
        auto duration = (std::chrono::steady_clock::now() - _process_start())
                        * (int64_t)_time_scale();
        return (uint64_t)std::chrono::duration_cast<T>(duration).count();
    }

    /** @} */

    /** @name Hardware identity
     *  @{ */

    inline const char* hardware_description() {
        return "x86-64 POSIX simulator";
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

    /** @brief Total allocated stack size for the calling thread (not the used
     *  amount). */
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
        usleep((ms * 1000) / _time_scale());  // scaled to track the sped-up clock
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
        /** @brief Simulated UART FIFO depth (bytes); also caps the token bucket's bank. */
        static constexpr int FIFO_BYTES = 64;

        /** @name State
         *  @{ */

        /** @brief Input file descriptor (-1 if unbound). */
        int _fd_in = -1;
        /** @brief Output file descriptor (-1 if unbound). */
        int _fd_out = -1;
        /** @brief Configured baud rate (drives the write throttle). */
        uint32_t _baud = 0;
        /** @brief 8N1 slot (us/byte) and its FIFO-deep bank, derived once in begin(). */
        uint64_t _slot_us = 0;
        uint64_t _max_bank_us = 0;
        /** @brief Earliest time (us) the next byte may leave the wire; advanced by one
         *  8N1 slot per byte sent (token bucket, see write()). */
        uint64_t _next_write_us = 0;
        /** @brief Whether _fd_out is a terminal. False for a file/pipe sink, where
         *  the UART model (baud throttle + TIOCOUTQ) is bypassed so redirected
         *  output actually drains. */
        bool _out_is_tty = false;

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
            _slot_us = baud ? 10000000ULL / baud : 0;   // 8N1: 10 bit-times per byte
            _max_bank_us = _slot_us * FIFO_BYTES;
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
         *  @return 1 on success, or 0 if it couldn't be sent (non-blocking fd: a
         *  full kernel buffer returns EAGAIN, the byte stays queued and the baud
         *  throttle isn't advanced). */
        inline size_t write(uint8_t c) {
            if (_fd_out == -1) return 0;
            ssize_t n = ::write(_fd_out, &c, 1);
            if (n <= 0) return 0;
            if (_slot_us > 0) {
                // Token bucket: accumulate the next-slot time (not now+slot), so one
                // per-tick transmit() drains every byte whose slot has elapsed (real
                // line rate). Bank is capped to one FIFO so an idle wire can't burst.
                const uint64_t now = _now_as<std::chrono::microseconds>();
                if (_next_write_us + _max_bank_us < now) _next_write_us = now - _max_bank_us;
                _next_write_us += _slot_us;
            }
            return (size_t)n;
        }

        /** @brief Bind the input/output file descriptors (use -1 to leave an
         *  end unbound). Both ends are set non-blocking so neither a stalled
         *  reader nor a full output buffer can block the main loop. */
        inline void set_fds(int in, int out) {
            _fd_in = in;
            _fd_out = out;
            _out_is_tty = (_fd_out != -1) && (::isatty(_fd_out) == 1);
            if (_fd_in != -1) {
                int flags = fcntl(_fd_in, F_GETFL, 0);
                fcntl(_fd_in, F_SETFL, flags | O_NONBLOCK);
            }
            if (_fd_out != -1) {
                int flags = fcntl(_fd_out, F_GETFL, 0) | O_NONBLOCK;
                // File/pipe sink: append so writes always land at the end and
                // concurrent writers can't clobber each other. No-op for a tty.
                if (!_out_is_tty) flags |= O_APPEND;
                fcntl(_fd_out, F_SETFL, flags);
            }
        }

        /** @brief Whether the output end is an interactive terminal (false for a
         *  file/pipe sink, or while unbound). Lets callers suppress diagnostic
         *  output that would otherwise garble a shared console terminal. */
        inline bool is_output_tty() const { return _out_is_tty; }

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
            // File/pipe sink: no UART to model, so report a full buffer (TIOCOUTQ
            // would fail with ENOTTY and wedge TX).
            if (!_out_is_tty) return FIFO_BYTES;
            if (_slot_us > 0 && _next_write_us > _now_as<std::chrono::microseconds>()) return 0;
            int pending_bytes = 0;
            if (::ioctl(_fd_out, TIOCOUTQ, &pending_bytes) < 0) return 0;
            int space = FIFO_BYTES - pending_bytes;  // small simulated UART buffer
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

    /** @brief Whether @p port's output end is an interactive terminal. Lets sinks
     *  (e.g. MonitorLogWriter) suppress diagnostic output that would otherwise
     *  garble a console terminal shared with the UI. False for a null port. */
    inline bool serial_is_output_tty(const RawSerial* port) {
        return (port != nullptr) && port->is_output_tty();
    }

    /** @} */

    /** @name Console raw mode
     *  @{ */

    /** @brief Put the controlling terminal into raw mode for interactive use:
     *  disable canonical mode and echo, make reads non-blocking, and register
     *  atexit/signal handlers that restore the original settings on exit. */
    inline void setup_raw_console() {
        // Only meaningful for an interactive terminal; on a file/pipe stdin
        // tcgetattr/tcsetattr fail with ENOTTY and there's nothing to restore.
        if (::isatty(STDIN_FILENO) != 1) return;
        struct termios& saved = *botanik_saved_console_termios();
        tcgetattr(STDIN_FILENO, &saved);
        atexit(botanik_restore_console);
        signal(SIGINT,  botanik_console_signal_handler);
        signal(SIGTERM, botanik_console_signal_handler);
        signal(SIGSEGV, botanik_console_signal_handler);
        struct termios raw = saved;
        // Clear ISIG so the app sees Ctrl-C/Z/\ as raw bytes (it decodes them as
        // InputEvents) rather than as signals. Restore handlers still fire on exit.
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);
        raw.c_cc[VMIN]  = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }

    /** @} */

    /** @name System lifecycle
     *  @{ */

    /** @brief argv captured at startup, for re-exec on reset(). The C runtime
     *  keeps argv alive for the whole process, so storing the pointer is enough. */
    inline char*** _reboot_argv() { static char** argv = nullptr; return &argv; }

    /** @brief Capture argv at the top of main() so reset() can re-exec with the
     *  original arguments. Simulator-only (the OPTA reset takes no arguments). */
    inline void set_reboot_args(int /*argc*/, char** argv) { *_reboot_argv() = argv; }

    /** @brief A teardown hook run by reset() just before re-exec. */
    using ResetHook = void(*)();

    /** @brief Slot holding the optional reset hook (a single registrant today). */
    inline ResetHook* _reset_hook() { static ResetHook hook = nullptr; return &hook; }

    /** @brief Register a teardown hook invoked by reset() before re-exec, letting a
     *  subsystem (e.g. the PlcIO emulator thread) clean up first without HAL
     *  depending on it. Simulator-only. */
    inline void on_reset(ResetHook hook) { *_reset_hook() = hook; }

    /** @brief Persist the active UI page index in the environment so a reset()
     *  re-exec lands back on it (execve carries it across). Simulator-only; an OPTA
     *  reset clears RAM, so the OPTA backend makes this a no-op. */
    inline void save_active_page(uint8_t index) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%u", (unsigned)index);
        setenv("BOTANIK_PAGE", buf, 1);
    }

    inline void reset() {
        // Run teardown while the image is intact, then execve to replace the
        // process: drops all threads/FDs and re-enters main() with a fresh uptime.
        if (ResetHook hook = *_reset_hook()) hook();
        char** argv = *_reboot_argv();
        char*  fallback[] = { (char*)"/proc/self/exe", nullptr };  // if argv was never captured
        execve("/proc/self/exe", argv ? argv : fallback, environ);
        perror("reset: execve failed");  // only reached if the exec itself failed
        _exit(127);
    }

    inline void shutdown(int code) {  // default (0) declared in platform.h
        // Run teardown, hand the console back cleanly (fresh line + cooked echo,
        // drained before termios is restored), then exit. atexit re-runs the
        // restore harmlessly. @p code: 0 for "exit", 128+signo on a signal.
        if (ResetHook hook = *_reset_hook()) hook();
        botanik_restore_console_clean();
        exit(code);
    }

    /** @} */

    /** @name Hardware watchdog
     *  @{ */

    // State is written on the main thread (kick/start/stop) and read by
    // watchdog_poll() on the emulator thread, shared via the codebase's volatile +
    // dsb() idiom (no std::atomic). 64-bit volatile is fine: simulator-only on
    // x86-64, where aligned 64-bit loads/stores are atomic.

    /** @brief Watchdog timeout in ms; 0 means disabled/stopped. */
    inline volatile uint32_t& _wd_timeout_ms()   { static volatile uint32_t t = 0; return t; }
    /** @brief Uptime (ms) of the most recent kick. */
    inline volatile uint64_t& _wd_last_kick_ms() { static volatile uint64_t k = 0; return k; }

    inline void watchdog_start(uint32_t timeout_ms) {
        _wd_last_kick_ms() = uptime_ms();  // seed kick before timeout, so a poll
        dsb();                             // seeing the new timeout can't see a stale kick
        _wd_timeout_ms() = timeout_ms;
    }

    inline void watchdog_kick() {
        _wd_last_kick_ms() = uptime_ms();
    }

    inline void watchdog_stop() {
        _wd_timeout_ms() = 0;
    }

    /** @brief Service the watchdog when the timeout has lapsed. Simulator-only;
     *  runs on the emulator thread so it can fire even when the main loop is
     *  wedged. In DEBUG builds it traps (SIGTRAP) so a debugger breaks on the
     *  wedged state instead of silently re-exec'ing; otherwise it reset()s. */
    inline void watchdog_poll() {
        uint32_t timeout = _wd_timeout_ms();
        uint32_t uptime = uptime_ms();
        uint32_t last_kick = _wd_last_kick_ms();
        if (timeout == 0) return;
        dsb();                             // read the kick paired with this timeout
        if (uptime > timeout && uptime - timeout > last_kick) {
#ifdef DEBUG
            raise(SIGTRAP);                // freeze for the debugger, don't reboot
#else
            reset();
#endif
        }
    }

    /** @} */

}}

#endif // TARGET_SIMULATOR
#endif // HAL_SIMULATOR_H
