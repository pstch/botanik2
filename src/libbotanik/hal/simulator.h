#ifndef HAL_SIMULATOR_H
#ifdef TARGET_SIMULATOR
#define HAL_SIMULATOR_H

#include <cstdint>
#include <chrono>
#include <cstring>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <pthread.h>

// internal helpers to simplify the timer functions
inline std::chrono::steady_clock::time_point _process_start() {
    static const auto t0 = std::chrono::steady_clock::now();
    return t0;
}

template <typename T>
inline uint64_t _now_as() {
    auto duration = std::chrono::steady_clock::now() - _process_start();
    return (uint64_t)std::chrono::duration_cast<T>(duration).count();
}

namespace Botanik { namespace HAL {

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

    // Returns the total allocated stack size for the calling thread.
    // Must NOT return base - current_sp (that is the used amount, not the depth).
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

    inline void* get_plc_input()  { return nullptr; }
    inline void* get_plc_output() { return nullptr; }

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

    class RawSerial {
    public:

        RawSerial() = default;

        inline void begin(uint32_t baud) {
            _baud = baud;
            _next_write_us = _now_as<std::chrono::microseconds>();
        }

        inline int read() {
            if (_fd_out == -1) return 0;
            uint8_t c;
            if (_fd_in != -1 && ::read(_fd_in, &c, 1) > 0) return c;
            return -1;
        }

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

        inline int available() {
            if (_fd_in == -1) return 0;
            int bytes_available = 0;
            if (::ioctl(_fd_in, FIONREAD, &bytes_available) < 0) return 0;
            return bytes_available;
        }

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

    private:
        int _fd_in = -1;
        int _fd_out = -1;
        uint32_t _baud = 0;
        uint64_t _next_write_us = 0;
    };

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

}}

#endif // TARGET_SIMULATOR
#endif // HAL_SIMULATOR_H