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

// assert() - HAL sits below the logger, so it can't use BOTANIK_ASSERT
#include <cassert>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <type_traits>
#include <csignal>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>   // PATH_MAX - reset() re-execs the resolved exe path
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
 *  cursor, reset attributes, and a fresh line, written straight to the wire
 *  (the final transmission). Then leave raw mode with TCSADRAIN - not TCSAFLUSH
 *  - so those bytes drain instead of being discarded. */
inline void botanik_restore_console_clean() {
    if (::isatty(STDIN_FILENO) != 1) return;
    // DECTCEM show, SGR reset, CR LF
    static const char tail[] = "\033[?25h\033[0m\r\n";
    (void)::write(STDOUT_FILENO, tail, sizeof(tail) - 1);
    tcsetattr(STDIN_FILENO, TCSADRAIN, botanik_saved_console_termios());
}

/** @brief Restore the terminal before exiting so Ctrl+C or a crash doesn't
 *  leave the shell in raw mode. Async-signal-safe (tcsetattr + _exit). */
extern "C" inline void botanik_console_signal_handler(int) {
    botanik_restore_console();
    _exit(0);
}

/** @} */

namespace Botanik { namespace HAL {

    /** @name Internal helpers
     *  @{ */

    /** @brief Process-start reference for the monotonic uptime clocks; captured
     *  once. */
    inline std::chrono::steady_clock::time_point _process_start() {
        static const auto t0 = std::chrono::steady_clock::now();
        return t0;
    }

    /** @brief BOTANIK_TIME_SCALE: run the sim clocks (and baud throttle) N
     *  times faster so tests finish sooner with unchanged byte/event ratios.
     *  Default 1, clamped >= 1. */
    inline uint32_t _time_scale() {
        static const uint32_t scale = []() -> uint32_t {
            const char* s = getenv("BOTANIK_TIME_SCALE");
            long v = s ? atol(s) : 1;
            return v < 1 ? 1u : (uint32_t)v;
        }();
        return scale;
    }

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

    /** @name Hardware identity
     *  @{ */

    inline const char* hardware_description() {
        return "x86-64 POSIX simulator";
    }

    /** @} */

    /** @brief Stack region introspection. */
    namespace Stack {

    inline uintptr_t base() {
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
    inline uint32_t depth() {
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

    /** @brief 0: the host has no RTOS thread watermark, so the scheduler
     *  measures the high-water itself by painting + scanning the stack (see
     *  _measure_stack_usage). */
    inline uint32_t high() { return 0; }

    }  // namespace Stack

    /** @brief PLC coprocessor shared memory. */
    namespace PlcIO {

    /// No PLC coprocessor exists on the host; the regions are absent.
    inline void* input()  { return nullptr; }
    inline void* output() { return nullptr; }

    }  // namespace PlcIO

    /** @brief Timing and clocks. */
    namespace Clock {

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

    inline uint64_t real_uptime_ms() {
        // deliberately skips the _time_scale() factor _now_as() applies, so it
        // tracks real wall-clock time even under BOTANIK_TIME_SCALE dilation
        auto elapsed = std::chrono::steady_clock::now() - _process_start();
        return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    }

    inline uint64_t wall_ms() {
        return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    /// No-op: the host system clock is the wall-clock source on the simulator.
    inline void set_wall_ms(uint64_t /*unix_ms*/) {}

    inline void sleep_ms(uint32_t ms) {
        // uint64 product: ms*1000 overflows uint32 for ms > ~71 min.
        // scaled to the sped-up clock
        usleep((useconds_t)((uint64_t)ms * 1000u / _time_scale()));
    }

    }  // namespace Clock

    /** @brief Memory-ordering primitives and D-cache maintenance. */
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
        // Match the OPTA backend's word-multiple + no-alias contract so the sim
        // build (where DEBUG + tests run) catches violations the hardware can't
        // surface.
        static_assert(std::is_empty<T>::value || sizeof(T) % sizeof(uint32_t) == 0,
                      "ordered_copy needs a word-multiple T");
        if (std::is_empty<T>::value) return;   // empty payload carries no data
#if defined(DEBUG)
        // no aliasing (ARM_MPU __RESTRICT)
        assert((const void*)dst != (const void*)src);
#endif
        std::memcpy((void*)dst, (const void*)src, sizeof(T));
        dsb();
    }

    template<typename T>
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

    /// No D-cache on the host; the PLC region is plain memory (see opta.h for
    /// the clean/invalidate discipline these mirror).
    inline void cache_clean(const void* addr, size_t len)      { (void)addr; (void)len; }
    inline void cache_invalidate(const void* addr, size_t len) { (void)addr; (void)len; }

    }  // namespace Memory

    /** @brief Debugger interaction. */
    namespace Debug {

    inline void breakpoint() {
        // <csignal> is already included above for the console handlers
        raise(SIGTRAP);
    }

    /** @brief True if a debugger (or any ptrace tracer) is attached: Linux
     *  reports a non-zero TracerPid in /proc/self/status. Re-read on each call
     *  (cheap, and
     *  a debugger may attach late). */
    inline bool under_debugger() {
        int fd = open("/proc/self/status", O_RDONLY);
        if (fd < 0) return false;
        char buf[512];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) return false;
        buf[n] = '\0';
        const char* p = strstr(buf, "TracerPid:");
        if (p == nullptr) return false;
        // skip "TracerPid:" + spaces
        for (p += 10; *p == ' ' || *p == '\t'; ++p) {}
        return *p != '0';
    }

    }  // namespace Debug

    /** @brief Serial access and console setup. */
    namespace Serial {

    /** @brief Host stand-in for the Arduino UART, backed by a pair of file
     *  descriptors with a baud-rate throttle and a simulated UART buffer. */
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
        inline void begin(uint32_t baud) {
            _baud = baud;
            // 8N1: 10 bit-times per byte
            _slot_us = baud ? 10000000ULL / baud : 0;
            _max_bank_us = _slot_us * FIFO_BYTES;
            _next_write_us = _now_as<std::chrono::microseconds>();
        }

        /** @brief Read a single byte; -1 if none available. */
        inline int read() {
            if (_fd_in == -1) return -1;
            uint8_t c;
            if (::read(_fd_in, &c, 1) > 0) return c;
            return -1;
        }

        /** @brief Write a single byte; 1 on success, 0 if it couldn't be sent
         *  (non-blocking fd: a full kernel buffer EAGAINs, the byte stays
         *  queued, throttle not advanced). */
        inline size_t write(uint8_t c) {
            if (_fd_out == -1) return 0;
            ssize_t n = ::write(_fd_out, &c, 1);
            if (n <= 0) return 0;
            if (_slot_us > 0) {
                // Token bucket: accumulate the next-slot time (not now+slot),
                // so one per-tick transmit() drains every byte whose slot has
                // elapsed (real line rate). Bank is capped to one FIFO so an
                // idle wire can't burst.
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

        /** @brief Whether the output end is an interactive terminal (false for
         *  a file/pipe sink, or while unbound). Lets callers suppress output
         *  that would garble a console. */
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
            // File/pipe sink: no UART to model, so report a full buffer
            // (TIOCOUTQ would fail with ENOTTY and wedge TX).
            if (!_out_is_tty) return FIFO_BYTES;
            if (_slot_us > 0 && _next_write_us > _now_as<std::chrono::microseconds>()) return 0;
            int pending_bytes = 0;
            if (::ioctl(_fd_out, TIOCOUTQ, &pending_bytes) < 0) return 0;
            // small simulated UART buffer
            int space = FIFO_BYTES - pending_bytes;
            return (space < 0) ? 0 : space;
        }

        /** @} */
    };

    /** @brief Logical serial endpoints: CONSOLE drives the interactive
     *  terminal, MONITOR the secondary diagnostic/log output. */
    enum Port { CONSOLE, MONITOR };

    /** @brief Resolve a logical Port to its backing Raw serial.
     *  CONSOLE is wired to stdin/stdout, MONITOR to stderr (output only).
     *  @param port CONSOLE or MONITOR. */
    inline Raw* get(Port port) {
        static Raw console, monitor;
        static bool initialized = false;
        if (!initialized) {
            console.set_fds(STDIN_FILENO, STDOUT_FILENO);
            monitor.set_fds(-1, STDERR_FILENO);
            initialized = true;
        }
        return (port == MONITOR) ? &monitor : &console;
    }

    /** @brief Whether @p port's output end is an interactive terminal (false
     *  for a null port). Lets sinks (e.g. MonitorLogWriter) suppress output
     *  that would garble the UI. */
    inline bool is_output_tty(const Raw* port) {
        return (port != nullptr) && port->is_output_tty();
    }

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
        // Clear ISIG so the app sees Ctrl-C/Z/\ as raw bytes (it decodes them
        // as InputEvents) rather than as signals. Restore handlers still fire
        // on exit.
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);
        raw.c_cc[VMIN]  = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }

    }  // namespace Serial

    /** @brief System lifecycle (reset / shutdown). */
    namespace System {

    /** @brief argv captured at startup, for re-exec on reset(). The C runtime
     *  keeps argv alive for the whole process, so storing the pointer is
     *  enough. */
    inline char*** _reboot_argv() { static char** argv = nullptr; return &argv; }

    /** @brief Resolved on-disk path of this executable, captured at startup.
     *  reset() re-execs this path rather than the /proc/self/exe magic symlink:
     *  the symlink pins the running inode, so after a relink (a new inode at
     *  the same path) it would re-run the old, now-deleted image. Re-resolving
     *  the path picks up a rebuilt binary - this is what lets a SIGUSR1 after a
     *  build hot-restart the sim. */
    inline char* _reboot_path() { static char path[PATH_MAX] = { 0 }; return path; }

    /** @brief Capture argv and the resolved exe path at the top of main() so
     *  reset() can re-exec. Resolving the path now, while /proc/self/exe still
     *  points at the running (not-yet-replaced) file, records the directory
     *  entry for later re-exec. Simulator-only (the OPTA reset takes no
     *  arguments). */
    inline void set_reboot_args(int /*argc*/, char** argv) {
        *_reboot_argv() = argv;
        char*   path = _reboot_path();
        ssize_t n    = readlink("/proc/self/exe", path, PATH_MAX - 1);
        // readlink does not NUL-terminate; empty on failure
        path[n > 0 ? n : 0] = '\0';
    }

    /** @brief A teardown hook run by reset() just before re-exec. */
    using ResetHook = void(*)();

    /** @brief Slot holding the optional reset hook (a single registrant
     *  today). */
    inline ResetHook* _reset_hook() { static ResetHook hook = nullptr; return &hook; }

    /** @brief Register a teardown hook invoked by reset() before re-exec, so a
     *  subsystem (e.g. the emulator thread) cleans up first without HAL
     *  depending on it. Sim-only. */
    inline void on_reset(ResetHook hook) { *_reset_hook() = hook; }


    inline void reset() {
        // Run teardown while the image is intact, then execve to replace the
        // process: drops all threads/FDs and re-enters main() with a fresh
        // uptime.
        if (ResetHook hook = *_reset_hook()) hook();
        // Hand the tty back to its pre-raw state before re-exec: the child's
        // setup_raw_console() snapshots the *current* termios as "original", so
        // an un-restored raw tty here would leave the terminal raw after a
        // later exit.
        botanik_restore_console_clean();
        char** argv = *_reboot_argv();
        // if argv was never captured
        char*  fallback[] = { (char*)"/proc/self/exe", nullptr };
        char** av    = argv ? argv : fallback;
        // Prefer the resolved path (re-resolves to a rebuilt binary), then the
        // /proc/self/exe symlink (robust if the path moved), then argv[0].
        const char* path = _reboot_path();
        if (path[0] != '\0') execve(path, av, environ);
        execve("/proc/self/exe", av, environ);
        if (av[0] != nullptr) execve(av[0], av, environ);
        perror("reset: execve failed");  // only reached if every exec failed
        _exit(127);
    }

    inline void shutdown(int code) {  // default (0) declared in platform.h
        // Run teardown, hand the console back cleanly (fresh line + cooked
        // echo,
        // drained before termios is restored), then exit. atexit re-runs the
        // restore harmlessly. @p code: 0 for "exit", 128+signo on a signal.
        if (ResetHook hook = *_reset_hook()) hook();
        botanik_restore_console_clean();
        exit(code);
    }

    }  // namespace System

    /** @brief Hardware watchdog. */
    namespace Watchdog {

    // State is written on the main thread (kick/start/stop) and read by
    // watchdog_poll() on the emulator thread, shared via the codebase's
    // volatile +
    // dsb() idiom (no std::atomic). 64-bit volatile is fine: simulator-only on
    // x86-64, where aligned 64-bit loads/stores are atomic.

    /** @brief Watchdog timeout in ms; 0 means disabled/stopped. */
    inline volatile uint32_t& _wd_timeout_ms()   { static volatile uint32_t t = 0; return t; }
    /** @brief Uptime (ms) of the most recent kick. */
    inline volatile uint64_t& _wd_last_kick_ms() { static volatile uint64_t k = 0; return k; }

    inline void start(uint32_t timeout_ms) {
        // seed kick before timeout, so a poll seeing the new timeout can't see
        // a stale kick
        _wd_last_kick_ms() = Clock::uptime_ms();
        Memory::dsb();
        _wd_timeout_ms() = timeout_ms;
    }

    inline void kick() {
        _wd_last_kick_ms() = Clock::uptime_ms();
    }

    inline void stop() {
        _wd_timeout_ms() = 0;
    }

    /** @brief Service the watchdog when the timeout has lapsed. Simulator-only;
     *  runs on the emulator thread so it can fire even when the main loop is
     *  wedged. Traps (SIGTRAP) when a debugger is attached so it breaks on the
     *  wedged state; otherwise resets the process. */
    inline void poll() {
        // 64-bit throughout (no 49-day wrap) and read last_kick *before* now,
        // so a concurrent kick() can't leave last_kick newer than
        // uptime and underflow the elapsed check into a spurious reset.
        uint64_t timeout   = _wd_timeout_ms();
        if (timeout == 0) return;
        uint64_t last_kick = _wd_last_kick_ms();
        // read the kick paired with this timeout
        Memory::dsb();
        uint64_t uptime    = Clock::uptime_ms();
        if (uptime - last_kick > timeout) {
            if (Debug::under_debugger()) Debug::breakpoint();  // freeze for the debugger
            else                         System::reset();      // reboot the wedged process
        }
    }

    }  // namespace Watchdog

}}

#endif // TARGET_SIMULATOR
#endif // HAL_SIMULATOR_H
