/** @file platform_sim.cpp
 *  @brief The cold half of the simulator HAL: everything the per-beat path does not
 *         call.
 *  @note The only place <termios.h> and <pthread.h> are allowed in. */

#include "libbotanik/hal/platform.h"

#ifdef TARGET_SIMULATOR

// Simulator (Linux/host) backend

#include <atomic>
#include <chrono>
#include <cstddef>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/socket.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

extern char** environ;

/** @name Console teardown
 *  HAL-internal; nothing outside this file calls them. @{ */

namespace {

/** @brief Pre-raw-mode terminal settings. Namespace scope, not a function-local
 *  static: a local static carries a thread-safe init guard, which the signal
 *  handler chain below must not contain. A POD here is zero-initialised before
 *  main, with no guard and no dynamic init. */
struct termios g_saved_console_termios;

/** @brief Accessor for it. extern "C" because the signal handler reaches this
 *  through botanik_restore_console, and C++11 allows only C-linkage functions
 *  in that chain (the rule relaxes in C++17). */
extern "C" struct termios* saved_console_termios() {
    return &g_saved_console_termios;
}

}  // namespace

/** @brief Restore the terminal to its pre-raw-mode settings. Async-signal-safe:
 *  TCSAFLUSH drops queued output, so a final transmission must already have
 *  drained (see restore_console_clean) before this runs. extern "C" for the
 *  atexit / signal-handler call chain. */
extern "C" void botanik_restore_console() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, saved_console_termios());
}

/** @brief Restore the terminal before dying, then die the way we were asked to.
 *
 *  Async-signal-safe: tcsetattr, then the default disposition. Re-raising and
 *  NOT _exit(0) - a SIGSEGV that exits 0 is a crash the harness reads as a pass,
 *  and this handler replaces the sanitizers' own, so it would swallow their
 *  report too. */
extern "C" void botanik_console_signal_handler(int sig) {
    botanik_restore_console();
    signal(sig, SIG_DFL);
    raise(sig);
}

namespace {

/** @brief Clean-exit console teardown (not signal-safe). Pages leave the cursor
 *  hidden and parked mid-row, so hand the shell back a sane terminal: show the
 *  cursor, reset attributes, and a fresh line, written straight to the wire
 *  (the final transmission). Then leave raw mode with TCSADRAIN - not TCSAFLUSH
 *  - so those bytes drain instead of being discarded. */
void restore_console_clean() {
    if (::isatty(STDIN_FILENO) != 1) return;
    // DECTCEM show, SGR reset, CR LF
    static const char tail[] = "\033[?25h\033[0m\r\n";
    (void)::write(STDOUT_FILENO, tail, sizeof(tail) - 1);
    tcsetattr(STDIN_FILENO, TCSADRAIN, saved_console_termios());
}

}  // namespace

/** @} */

namespace Botanik { namespace HAL {

    namespace {
        /** @brief Process-start reference for the monotonic uptime clocks;
         *  captured once. */
        std::chrono::steady_clock::time_point _process_start() {
            static const auto t0 = std::chrono::steady_clock::now();
            return t0;
        }
    }

    /** @brief Declared in simulator.h, defined here: <chrono> stays in this one
     *  TU instead of the 56 that reach the header. */
    uint64_t _monotonic_ns() {
        return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now() - _process_start()).count();
    }

    bool _manual_clock() {
        static const bool manual = []() -> bool {
            const char* s = getenv("BOTANIK_SIM_CLOCK");
            if (s == nullptr) return false;
            // Both modes read the same virtual clock; they differ only in WHO
            // moves it - a harness over the control fd, or the sim itself.
            return strcmp(s, "manual") == 0 || strcmp(s, "fast") == 0;
        }();
        return manual;
    }

    uint64_t& _virtual_ns() { static uint64_t ns = 0; return ns; }


    const char* hardware_description() {
        return "x86-64 POSIX simulator";
    }

    namespace Stack {

    namespace {

    /** @brief Sentinel byte. Arbitrary: the scan broadcasts it to a word and
     *  hunts a RUN of them, so any non-degenerate value works. */
    const uint8_t  PAINT_BYTE = 0xCD;
    /** @brief How far below the frame to paint and scan, on top of the clamp
     *  against the real stack bottom. A host-stack property. */
    const uint32_t SCAN_LIMIT = 65536;
    /** @brief Intact words that mean "untouched from here down". Four makes a
     *  coincidental match in live data (~1/2^32 per word) unreachable. */
    const int      RUN = 4;

    /** @brief What the last paint covered, read back by the next scan. */
    uint8_t* g_top = nullptr;
    uint8_t* g_bot = nullptr;

    /** @brief The calling thread's stack region: one past the top, and its
     *  size. Both from the same pthread_attr, so they cannot disagree. */
    void region(uintptr_t* base, uint32_t* depth) {
        *base = 0; *depth = 0;
        pthread_attr_t attr;
        if (pthread_getattr_np(pthread_self(), &attr) != 0) return;
        void* addr; size_t size;
        pthread_attr_getstack(&attr, &addr, &size);
        pthread_attr_destroy(&attr);
        *base  = reinterpret_cast<uintptr_t>(addr) + size;
        *depth = (size > UINT32_MAX) ? UINT32_MAX : (uint32_t)size;
    }

    }  // namespace

    // ASAN puts locals on a fake stack, which the scan cannot see; the
    // annotation is what makes this work against the real one. noinline is
    // load-bearing too: it paints below its own &marker, so folded into the
    // caller the paint would clobber the caller's live locals.
    BOTANIK_NO_ASAN __attribute__((noinline)) StackUsage high_water() {
        StackUsage u; u.used = 0; u.depth = 0;
        uintptr_t base = 0;
        region(&base, &u.depth);
        if (base == 0 || u.depth == 0) return u;

        // SCAN what the previous call painted, top-down, for the first
        // RUN-long stretch of intact sentinel: the start of untouched stack.
        if (g_top != nullptr) {
            const uint32_t PAT = (uint32_t)0x01010101u * PAINT_BYTE;
            const uint32_t* top = reinterpret_cast<const uint32_t*>(
                reinterpret_cast<uintptr_t>(g_top) & ~(uintptr_t)3u);
            const uint32_t* bot = reinterpret_cast<const uint32_t*>(
                (reinterpret_cast<uintptr_t>(g_bot) + 3u) & ~(uintptr_t)3u);
            const uint32_t* hw = bot;      // default: all of it was used
            int run = 0;
            for (const uint32_t* p = top; p > bot; ) {
                --p;
                if (*p == PAT) { if (++run >= RUN) { hw = p + RUN; break; } }
                else           { run = 0; }
            }
            u.used = (uint32_t)(base - reinterpret_cast<uintptr_t>(hw));
        }

        // ...then REPAINT below this frame, for the next call to read. The
        // margin clears our own spilled locals plus memset's frame - a known
        // leaf, and the only thing below us. memset captures its bounds once,
        // so it is correct even at -O0 where an inline loop would re-read
        // bounds it has just clobbered.
        volatile uint8_t marker = 0;
        const ptrdiff_t MARGIN = 512;
        // Through uintptr_t deliberately: this walks the stack, it does not
        // index `marker`, which is what -Warray-bounds is there to catch
        // everywhere else.
        uint8_t* top = reinterpret_cast<uint8_t*>(
            reinterpret_cast<uintptr_t>(const_cast<uint8_t*>(&marker))
                - (uintptr_t)MARGIN);
        uint8_t* floor_ = reinterpret_cast<uint8_t*>(base - u.depth);
        if (top <= floor_) return u;
        uint8_t* bot = top - SCAN_LIMIT;
        if (bot < floor_) bot = floor_;
        memset(bot, PAINT_BYTE, (size_t)(top - bot));
        g_top = top;
        g_bot = bot;
        return u;
    }

    }  // namespace Stack

    namespace Clock {

    uint64_t real_uptime_ms() {
        // Host time, deliberately not _now_as(): under the manual clock that
        // reads the harness's virtual clock, and a per-real-second rate measured
        // against it would read as infinite between commands.
        auto elapsed = std::chrono::steady_clock::now() - _process_start();
        return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    }

    uint64_t wall_ms() {
        // Virtual clock: an epoch plus simulated uptime, so every run stamps its
        // logs and status bar identically. BOTANIK_SIM_EPOCH (UNIX seconds)
        // moves it, which is how a summer day is reached: the sun tracker and
        // the simulated weather both work from this one reading.
        // Default 2026-01-01T00:00:00Z.
        if (_manual_clock()) {
            static const uint64_t epoch_ms =
                1000ull * (uint64_t)Env::u32("BOTANIK_SIM_EPOCH", 1767225600u);
            return epoch_ms + _now_as(NS_PER_MS);
        }
        return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    /// No-op: the host system clock is the wall-clock source on the simulator.
    void set_wall_ms(uint64_t /*unix_ms*/) {}

    void sleep_ms(uint32_t ms) {
        // Manual clock: a sleep IS the passage of time, so advance rather than
        // block - otherwise the caller waits for a clock nothing will move.
        if (_manual_clock()) { _virtual_ns() += (uint64_t)ms * 1000000u; return; }
        // uint64 product: ms*1000 overflows uint32 for ms > ~71 min.
        usleep((useconds_t)((uint64_t)ms * 1000u));
    }

    // The host's uptime clock already resolves microseconds, so the tick IS a
    // microsecond and there is nothing to convert. Deliberately the same
    // reading as uptime_us(): under the manual clock an interval measured in
    // real time and one measured in simulated time are different questions,
    // and every other metric answers the second.
    uint32_t hrt_ticks() { return (uint32_t)_now_as(NS_PER_US); }

    uint32_t hrt_us(uint32_t ticks) { return ticks; }


    }  // namespace Clock

    namespace Debug {


    void breakpoint() {
        // <csignal> is already included above for the console handlers
        raise(SIGTRAP);
    }

    /** @brief True if a debugger (or any ptrace tracer) is attached: Linux
     *  reports a non-zero TracerPid in /proc/self/status. Re-read on each call,
     *  since a debugger may attach late. */
    bool under_debugger() {
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

    namespace Serial {

    void Raw::begin(uint32_t baud) {
        _baud = baud;
        // 8N1: 10 bit-times per byte
        // From the frame format, not a hardcoded ten: Modbus RTU spends ELEVEN
        // bits per character (8E1, or 8N1 with a second stop bit), which
        // plan.h's duty arithmetic and the OPTA's own Rs485Raw::format() both
        // already say. _bits_per_char was computed for exactly this and had no
        // reader, so the simulated bus ran 9% fast against its own planner.
        _slot_us = baud ? (1000000ULL * _bits_per_char) / baud : 0;
        _max_bank_us = _slot_us * FIFO_BYTES;
        _next_write_us = _now_as(NS_PER_US);
    }

    int Raw::read() {
        if (_fd_in == -1) return -1;
        uint8_t c;
        if (::read(_fd_in, &c, 1) > 0) return c;
        return -1;
    }

    size_t Raw::write(uint8_t c) {
        if (_fd_out == -1) return 0;
        ssize_t n = ::write(_fd_out, &c, 1);
        if (n <= 0) return 0;
        if (_slot_us > 0) {
            // Token bucket: accumulate the next-slot time (not now+slot), so one
            // per-tick transmit() drains every byte whose slot has elapsed (real
            // line rate). Bank is capped to one FIFO so an idle wire can't burst.
            const uint64_t now = _now_as(NS_PER_US);
            if (_next_write_us + _max_bank_us < now) _next_write_us = now - _max_bank_us;
            _next_write_us += _slot_us;
        }
        return (size_t)n;
    }

    void Raw::set_fds(int in, int out) {
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

    int Raw::available() {
        if (_fd_in == -1) return 0;
        int bytes_available = 0;
        if (::ioctl(_fd_in, FIONREAD, &bytes_available) < 0) return 0;
        return bytes_available;
    }

    int Raw::availableForWrite() {
        if (_fd_out == -1) return 0;
        // File/pipe sink: no UART to model, so report a full buffer (TIOCOUTQ
        // would fail with ENOTTY and wedge TX).
        if (!_out_is_tty) return FIFO_BYTES;
        if (_slot_us > 0 && _next_write_us > _now_as(NS_PER_US)) return 0;
        int pending_bytes = 0;
        if (::ioctl(_fd_out, TIOCOUTQ, &pending_bytes) < 0) return 0;
        // small simulated UART buffer
        int space = FIFO_BYTES - pending_bytes;
        return (space < 0) ? 0 : space;
    }

    int& fieldbus_peer_fd() { static int fd = -1; return fd; }

    uint16_t Raw::flush_input() {
        if (_fd_in == -1) return 0;
        uint8_t c;
        uint16_t n = 0;
        for (uint16_t i = 0; i < 512; ++i) {
            if (::read(_fd_in, &c, 1) <= 0) break;
            n++;
        }
        return n;
    }

    Raw* get(Port port) {
        static Raw console, monitor, fieldbus_port;
        static bool initialized = false;
        if (!initialized) {
            console.set_fds(STDIN_FILENO, STDOUT_FILENO);
            monitor.set_fds(-1, STDERR_FILENO);
            // Real socketpair (see simulator.h): a pair that cannot be created
            // leaves the port unbound - a bus with no devices on it.
            int sp[2] = { -1, -1 };
            if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
                ::fcntl(sp[0], F_SETFL, ::fcntl(sp[0], F_GETFL, 0) | O_NONBLOCK);
                ::fcntl(sp[1], F_SETFL, ::fcntl(sp[1], F_GETFL, 0) | O_NONBLOCK);
                fieldbus_port.set_fds(sp[0], sp[0]);
                fieldbus_peer_fd() = sp[1];
            }
            initialized = true;
        }
        if (port == FIELDBUS) return &fieldbus_port;
        return (port == MONITOR) ? &monitor : &console;
    }

    bool is_output_tty(const Raw* port) {
        return (port != nullptr) && port->is_output_tty();
    }

    void setup_raw_console() {
        // Only meaningful for an interactive terminal; on a file/pipe stdin
        // tcgetattr/tcsetattr fail with ENOTTY and there's nothing to restore.
        if (::isatty(STDIN_FILENO) != 1) return;
        struct termios& saved = *saved_console_termios();
        tcgetattr(STDIN_FILENO, &saved);
        atexit(botanik_restore_console);
        signal(SIGINT,  botanik_console_signal_handler);
        signal(SIGTERM, botanik_console_signal_handler);
#if !defined(__SANITIZE_ADDRESS__) && !defined(BOTANIK_SANITIZED)
        // Left to the sanitizer when there is one: its handler prints the
        // report, and ours would replace it (this runs from main(), after
        // theirs is installed).
        signal(SIGSEGV, botanik_console_signal_handler);
#endif
        struct termios raw = saved;
        // Clear ISIG so the app sees Ctrl-C/Z/\ as raw bytes (it decodes them as
        // InputEvents) rather than as signals. Restore handlers still fire on
        // exit.
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);
        // Clear software flow control too: 0x13 is Ctrl+S to the app (the
        // MONITOR page's save key), and with IXON the tty layer eats it and
        // stops the sim's output until an XON that never comes.
        raw.c_iflag &= ~(IXON | IXOFF | IXANY);
        raw.c_cc[VMIN]  = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }

    }  // namespace Serial

    namespace System {

    namespace {

    /** @brief argv captured at startup, for re-exec on reset(). The C runtime
     *  keeps argv alive for the whole process, so storing the pointer is
     *  enough. */
    char*** reboot_argv() { static char** argv = nullptr; return &argv; }

    /** @brief Resolved on-disk path of this executable, captured at startup.
     *  reset() re-execs this path, not the /proc/self/exe symlink: the symlink
     *  pins the running inode, so after a relink it would re-run the old,
     *  now-deleted image. Re-resolving picks up a rebuilt binary. */
    char* reboot_path() { static char path[PATH_MAX] = { 0 }; return path; }

    /** @brief Slot holding the optional reset hook (a single registrant today). */
    ResetHook* reset_hook() { static ResetHook hook = nullptr; return &hook; }

    }  // namespace

    void set_reboot_args(int /*argc*/, char** argv) {
        *reboot_argv() = argv;
        char*   path = reboot_path();
        ssize_t n    = readlink("/proc/self/exe", path, PATH_MAX - 1);
        // readlink does not NUL-terminate; empty on failure
        path[n > 0 ? n : 0] = '\0';
    }

    void on_reset(ResetHook hook) { *reset_hook() = hook; }

    void reset() {
        // Run teardown while the image is intact, then execve to replace the
        // process: drops all threads/FDs and re-enters main() with a fresh
        // uptime.
        if (ResetHook hook = *reset_hook()) hook();
        // Hand the tty back to its pre-raw state before re-exec: the child's
        // setup_raw_console() snapshots the *current* termios as "original", so
        // an un-restored raw tty here would leave the terminal raw after a later
        // exit.
        restore_console_clean();
        char** argv = *reboot_argv();
        // if argv was never captured
        char*  fallback[] = { (char*)"/proc/self/exe", nullptr };
        char** av    = argv ? argv : fallback;
        // Prefer the resolved path (re-resolves to a rebuilt binary), then the
        // /proc/self/exe symlink (robust if the path moved), then argv[0].
        const char* path = reboot_path();
        if (path[0] != '\0') execve(path, av, environ);
        execve("/proc/self/exe", av, environ);
        if (av[0] != nullptr) execve(av[0], av, environ);
        perror("reset: execve failed");  // only reached if every exec failed
        _exit(127);
    }

    void shutdown(int code) {
        // Run teardown, hand the console back cleanly (fresh line + cooked echo,
        // drained before termios is restored), then exit. atexit re-runs the
        // restore harmlessly. @p code: 0 for "exit", 128+signo on a signal.
        if (ResetHook hook = *reset_hook()) hook();
        restore_console_clean();
        exit(code);
    }

    }  // namespace System

    namespace Watchdog {

    volatile uint32_t& _wd_timeout_ms()   { static volatile uint32_t t = 0; return t; }
    volatile uint64_t& _wd_last_kick_ms() { static volatile uint64_t k = 0; return k; }

    void poll();

    /** @brief Whether this process arms the watchdog at all.
     *
     *  `BOTANIK_WATCHDOG=0` inhibits it. A running sim wants the real thing -
     *  the point is that a wedged loop resets the unit - but a test process does
     *  not: the doctest binary builds schedulers it never kicks, and a pyte case
     *  that paused the sim would be re-executed underneath its own PTY. Read
     *  once: the answer cannot change within a process. */
    bool _wd_enabled() {
        // Off by default on a virtual clock: the nap is real time and the
        // window is not, so the loop can cross a whole window between two
        // polls. Still opt-in-able, which is how the one case that asserts the
        // watchdog works gets it.
        static const bool on = (Env::u8("BOTANIK_WATCHDOG",
                                        _manual_clock() ? 0 : 1) != 0);
        return on;
    }

    /** @brief The servicing thread. Its own, NOT the main loop's: a watchdog
     *  polled by the loop it is watching cannot notice that loop wedging, which
     *  is the whole failure it exists for. Detached and never joined - it dies
     *  with the process, or with the execve System::reset() does. */
    void* _wd_thread(void*) {
        for (;;) {
            poll();
            // The nap is REAL time; the window is measured on the uptime clock.
            // Under the manual clock those are different domains - virtual time
            // moves a beat at a time and can cross a whole window in microseconds
            // of wall clock - so poll briskly and cheaply there rather than
            // sleeping a fraction of a window that is not passing in real time
            // at all. Free-running, a quarter of the window catches a lapse well
            // inside it without spinning.
            const uint32_t t = _wd_timeout_ms();
            uint32_t nap_ms;
            if      (_manual_clock()) nap_ms = 2;
            else if (t == 0)          nap_ms = 100;   // stopped; just idle
            else if (t < 40u)         nap_ms = 10;
            else                      nap_ms = t / 4u;
            usleep(nap_ms * 1000u);
        }
        return nullptr;
    }

    void start(uint32_t timeout_ms) {
        if (!_wd_enabled()) return;
        // seed kick before timeout, so a poll seeing the new timeout can't see a
        // stale kick
        _wd_last_kick_ms() = Clock::uptime_ms();
        Memory::dsb();
        _wd_timeout_ms() = timeout_ms;

        // One thread per process, however often the watchdog is re-armed.
        static bool started = false;
        if (started) return;
        started = true;
        pthread_t th;
        if (pthread_create(&th, nullptr, &_wd_thread, nullptr) == 0)
            pthread_detach(th);
        else
            _wd_timeout_ms() = 0;   // nothing will service it; do not pretend
    }

    void stop() { _wd_timeout_ms() = 0; }

    void poll() {
        // 64-bit throughout (no 49-day wrap) and read last_kick *before* now, so
        // a concurrent kick() can't leave last_kick newer than uptime and
        // underflow the elapsed check into a spurious reset.
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
