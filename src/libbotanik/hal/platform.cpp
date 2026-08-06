/** @file platform.cpp
 *  @brief The cold half of the HAL: everything the per-beat path does not call.
 *
 *  hal/platform.h declares the whole surface and picks a backend header for the
 *  parts that must be inline (the clocks, the barriers, the templates, and the
 *  types a caller holds by value). Everything else is defined here, under the
 *  same `#ifdef TARGET_OPTA / #else` split hal/net.cpp and hal/storage.cpp use.
 *
 *  This is where the platform's own headers - <termios.h>, <pthread.h>,
 *  <sys/ioctl.h> and friends on the host, mbed.h on the OPTA - are allowed to
 *  be included. Keeping them out of the backend headers is the point: those are
 *  reached by ~30 translation units, none of which want the POSIX or Mbed
 *  preprocessor surface in scope just to read the clock. */

#include "libbotanik/hal/platform.h"    // the surface defined here

#ifdef TARGET_OPTA

// OPTA backend
// ===========================================================================

#include <cstdint>                      // uint32_t/uint64_t/uintptr_t
#include <ctime>                        // time / time_t / set_time

#include "USBSerial.h"                  // USBSerial (the MONITOR transport)

/// Linker symbol for the top of the MSP/ISR stack (the ~1KB exception stack,
/// NOT the RTOS thread stack the firmware runs on).
extern "C" char _estack;

namespace Botanik { namespace HAL {

    const char* hardware_description() {
        return "Arduino OPTA";
    }

    namespace Stack {

    uintptr_t base() {
        return reinterpret_cast<uintptr_t>(&_estack);
    }

    uint32_t depth() {
        // Size of the CURRENT RTOS thread's stack: setup()/loop() run in the
        // Arduino Mbed main thread, NOT on the ~1KB MSP stack _estack bounds,
        // so ask the RTOS.
        return static_cast<uint32_t>(osThreadGetStackSize(osThreadGetId()));
    }

    uint32_t high() {
        osThreadId_t id = osThreadGetId();
        uint32_t size  = static_cast<uint32_t>(osThreadGetStackSize(id));
        // space: min free ever
        uint32_t space = static_cast<uint32_t>(osThreadGetStackSpace(id));
        return (space > 0 && space <= size) ? (size - space) : 0;
    }

    }  // namespace Stack

    namespace Clock {

    uint64_t wall_ms() {
        // TODO [WALLCLOCK-WRAP-2038]: time() returns time_t, which is 32-bit on
        // this newlib and WRAPS in Jan 2038 before the widening cast - this
        // must be sourced from a 64-bit RTC read (it also only resolves whole
        // seconds; read the RTC sub-second register for ms).
        return (uint64_t)time(NULL) * 1000;
    }

    void set_wall_ms(uint64_t unix_ms) {
        // Programs the STM32 RTC, which is what time() reads back. Whole
        // seconds only: Mbed's set_time() takes a time_t, so the sub-second
        // part of an NTP sync is dropped (see WALLCLOCK-WRAP-2038).
        set_time((time_t)(unix_ms / 1000u));
    }

    void sleep_ms(uint32_t ms) {
        rtos::ThisThread::sleep_for(std::chrono::milliseconds(ms));
    }

    }  // namespace Clock

    namespace Debug {

    void breakpoint() {
        __BKPT(0);  // CMSIS breakpoint intrinsic, provided via mbed.h
    }

    bool under_debugger() {
        // C_DEBUGEN is set while a halting debugger is attached to the core.
        return (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0;
    }

    }  // namespace Debug

    namespace Serial {

    /** @brief Hardware UART transport. */
    class UartRaw : public Raw {
        mbed::BufferedSerial _port;
    public:
        UartRaw(PinName tx, PinName rx) : _port(tx, rx) {
            _port.set_blocking(false);   // the main loop must never stall on IO
        }
        void begin(uint32_t baud) override { _port.set_baud((int)baud); }
        int read() override {
            uint8_t c;
            return _port.read(&c, 1) == 1 ? (int)c : -1;
        }
        size_t write(uint8_t c) override {
            return _port.write(&c, 1) == 1 ? (size_t)1 : (size_t)0;
        }
        uint16_t availableForWrite() override { return _port.writable() ? 1 : 0; }
    };

    /** @brief USB CDC transport. */
    class UsbRaw : public Raw {
        USBSerial _port;
    public:
        // connect_blocking = false: without it construction waits for a host,
        // which would hang boot whenever nothing is plugged in.
        UsbRaw() : _port(false) {}
        void begin(uint32_t) override {}          // CDC has no line rate
        int read() override {
            return _port.available() ? _port._getc() : -1;
        }
        size_t write(uint8_t c) override {
            // With no host attached, discard rather than stall: this is a
            // diagnostic port, and blocking the log drain would take the loop
            // down with it.
            if (!_port.connected()) return 1;
            return _port._putc(c) >= 0 ? (size_t)1 : (size_t)0;
        }
        uint16_t availableForWrite() override { return 1; }
    };

    Raw* get(Port port) {
        static UartRaw console(CONSOLE_TX, CONSOLE_RX);
        static UsbRaw  monitor;
        return (port == MONITOR) ? static_cast<Raw*>(&monitor)
                                 : static_cast<Raw*>(&console);
    }

    bool is_output_tty(const Raw*) { return false; }

    }  // namespace Serial

    namespace System {

    void reset() {
        // NVIC_SystemReset is the portable CMSIS reset (always available via
        // mbed.h); mbed_reset() is an alternative if a softer Mbed-managed
        // reset is wanted. Compile-reasoned only - confirm on a real
        // arm-none-eabi build.
        NVIC_SystemReset();
    }

    void shutdown(int /*code*/) {
        // Firmware has no clean "halt and return to OS" - there is no OS. The
        // closest sane behaviour is to reboot, so shutdown mirrors reset here.
        // There is no process exit status to carry, so @p code is ignored.
        reset();
    }

    }  // namespace System

    namespace Watchdog {

    void start(uint32_t timeout_ms) {
        // Clamp to the peripheral's max window; the hardware rejects anything
        // above it.
        uint32_t max = mbed::Watchdog::get_instance().get_max_timeout();
        mbed::Watchdog::get_instance().start(timeout_ms > max ? max : timeout_ms);
    }

    void stop() {
        mbed::Watchdog::get_instance().stop();
    }

    }  // namespace Watchdog

}}

#else

// Simulator (Linux/host) backend
// ===========================================================================

#include <atomic>       // std::atomic_thread_fence (via the header's inlines)
#include <chrono>       // std::chrono
#include <csignal>      // signal / SIGINT / SIGTERM / SIGSEGV / SIGTRAP
#include <cstdint>      // uint8_t/uint64_t/uintptr_t
#include <cstdlib>      // atexit / exit / getenv
#include <cstring>      // strstr

#include <fcntl.h>      // fcntl / O_NONBLOCK / O_APPEND
#include <limits.h>     // PATH_MAX
#include <pthread.h>    // pthread_getattr_np
#include <stdio.h>      // perror / FILE
#include <sys/ioctl.h>  // ioctl / FIONREAD / TIOCOUTQ
#include <termios.h>    // termios / tcgetattr / tcsetattr
#include <unistd.h>     // read/write/close/isatty/readlink/usleep/execve/_exit

extern char** environ;

/** @name Console teardown
 *
 *  HAL-internal: nothing outside this file calls them, which is why they moved
 *  out of the backend header along with <termios.h> and <csignal>.
 *  @{ */

namespace {

/** @brief Pre-raw-mode terminal settings. */
struct termios* saved_console_termios() {
    static struct termios saved;  // NOLINT(bugprone-signal-handler)
    return &saved;
}

}  // namespace

/** @brief Restore the terminal to its pre-raw-mode settings. Async-signal-safe:
 *  TCSAFLUSH drops queued output, so a final transmission must already have
 *  drained (see restore_console_clean) before this runs. extern "C" for the
 *  atexit / signal-handler call chain. */
extern "C" void botanik_restore_console() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, saved_console_termios());
}

/** @brief Restore the terminal before exiting so Ctrl+C or a crash doesn't
 *  leave the shell in raw mode. Async-signal-safe (tcsetattr + _exit). */
extern "C" void botanik_console_signal_handler(int) {
    botanik_restore_console();
    _exit(0);
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

    /** @brief Process-start reference for the monotonic uptime clocks; captured
     *  once. */
    std::chrono::steady_clock::time_point _process_start() {
        static const auto t0 = std::chrono::steady_clock::now();
        return t0;
    }

    /** @brief BOTANIK_TIME_SCALE: run the sim clocks (and baud throttle) N
     *  times faster so tests finish sooner with unchanged byte/event ratios.
     *  Default 1, clamped >= 1. */
    uint32_t _time_scale() {
        static const uint32_t scale = []() -> uint32_t {
            const char* s = getenv("BOTANIK_TIME_SCALE");
            long v = s ? atol(s) : 1;
            return v < 1 ? 1u : (uint32_t)v;
        }();
        return scale;
    }


    /** @name Hardware identity
     *  @{ */

    const char* hardware_description() {
        return "x86-64 POSIX simulator";
    }

    namespace Stack {


    uintptr_t base() {
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
    uint32_t depth() {
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
    uint32_t high() { return 0; }

    }  // namespace Stack

    namespace Clock {

    uint64_t real_uptime_ms() {
        // deliberately skips the _time_scale() factor _now_as() applies, so it
        // tracks real wall-clock time even under BOTANIK_TIME_SCALE dilation
        auto elapsed = std::chrono::steady_clock::now() - _process_start();
        return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    }

    uint64_t wall_ms() {
        return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    /// No-op: the host system clock is the wall-clock source on the simulator.
    void set_wall_ms(uint64_t /*unix_ms*/) {}

    void sleep_ms(uint32_t ms) {
        // uint64 product: ms*1000 overflows uint32 for ms > ~71 min.
        // scaled to the sped-up clock
        usleep((useconds_t)((uint64_t)ms * 1000u / _time_scale()));
    }


    }  // namespace Clock

    namespace Debug {


    void breakpoint() {
        // <csignal> is already included above for the console handlers
        raise(SIGTRAP);
    }

    /** @brief True if a debugger (or any ptrace tracer) is attached: Linux
     *  reports a non-zero TracerPid in /proc/self/status. Re-read on each call
     *  (cheap, and
     *  a debugger may attach late). */
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
        _slot_us = baud ? 10000000ULL / baud : 0;
        _max_bank_us = _slot_us * FIFO_BYTES;
        _next_write_us = _now_as<std::chrono::microseconds>();
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
            const uint64_t now = _now_as<std::chrono::microseconds>();
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
        if (_slot_us > 0 && _next_write_us > _now_as<std::chrono::microseconds>()) return 0;
        int pending_bytes = 0;
        if (::ioctl(_fd_out, TIOCOUTQ, &pending_bytes) < 0) return 0;
        // small simulated UART buffer
        int space = FIFO_BYTES - pending_bytes;
        return (space < 0) ? 0 : space;
    }

    Raw* get(Port port) {
        static Raw console, monitor;
        static bool initialized = false;
        if (!initialized) {
            console.set_fds(STDIN_FILENO, STDOUT_FILENO);
            monitor.set_fds(-1, STDERR_FILENO);
            initialized = true;
        }
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
        signal(SIGSEGV, botanik_console_signal_handler);
        struct termios raw = saved;
        // Clear ISIG so the app sees Ctrl-C/Z/\ as raw bytes (it decodes them as
        // InputEvents) rather than as signals. Restore handlers still fire on
        // exit.
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);
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
     *  reset() re-execs this path rather than the /proc/self/exe magic symlink:
     *  the symlink pins the running inode, so after a relink (a new inode at the
     *  same path) it would re-run the old, now-deleted image. Re-resolving the
     *  path picks up a rebuilt binary - this is what lets a SIGUSR1 after a build
     *  hot-restart the sim. */
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

    void start(uint32_t timeout_ms) {
        // seed kick before timeout, so a poll seeing the new timeout can't see a
        // stale kick
        _wd_last_kick_ms() = Clock::uptime_ms();
        Memory::dsb();
        _wd_timeout_ms() = timeout_ms;
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

#endif // TARGET_OPTA / simulator
