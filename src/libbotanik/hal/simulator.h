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

// PLC simulator I/O pointers
extern void* _sim_ptr_in;
extern void* _sim_ptr_out;    

// internal helpers to simplify the timer functions
template <typename T>
inline uint64_t _now_as() {
    std::chrono::nanoseconds ns = std::chrono::steady_clock::now().time_since_epoch();
    return (uint64_t)std::chrono::duration_cast<T>(ns).count();
}

namespace Botanik { namespace HAL {

    inline void* get_plc_input() { 
        return _sim_ptr_in;
    }

    inline void* get_plc_output() {
        return _sim_ptr_out;
    }

    inline void set_plcio_pointers(void* in, void* out) {
        _sim_ptr_in = in;
        _sim_ptr_out = out;
    }    

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

        inline void begin(uint32_t baud) {
            (void)baud;
            if (_fd_in != -1) {
                int flags = fcntl(_fd_in, F_GETFL, 0);
                fcntl(_fd_in, F_SETFL, flags | O_NONBLOCK);
            }            
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
            return (n > 0) ? (size_t)n : 0;
        }

        inline void set_fds(int in, int out) {
            _fd_in = in;
            _fd_out = out;
        }

        inline int available() {
            if (_fd_in == -1) return 0;
            int bytes_available = 0;
            if (::ioctl(_fd_in, FIONREAD, &bytes_available) < 0) return 0;
            return bytes_available;
        }

        inline int availableForWrite() {
            if (_fd_out == -1) return 0;
            int pending_bytes = 0;
            // TIOCOUTQ gives us the number of bytes currently in the output buffer
            if (::ioctl(_fd_out, TIOCOUTQ, &pending_bytes) < 0) return 0;
            // simulate a small UART buffer
            int space = 64 - pending_bytes;
            return (space < 0) ? 0 : space;
        }        

    private:
        int _fd_in = -1;
        int _fd_out = -1;
    };

}}

#endif // TARGET_SIMULATOR
#endif // HAL_SIMULATOR_H