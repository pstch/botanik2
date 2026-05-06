#ifndef HAL_PLATFORM_H
#define HAL_PLATFORM_H

#include <cstdint>
#include <cstring>

namespace Botanik { namespace HAL {
    // Declared before platform headers so get_serial() can use it inline
    enum SerialPort { CONSOLE, MONITOR };
}}

#ifdef TARGET_OPTA
    #include "libbotanik/hal/opta.h"
#else
    #include "libbotanik/hal/simulator.h"
#endif

namespace Botanik { namespace HAL {

    // Return the high address (base) of the current thread's stack region
    uint32_t get_stack_base();

    // Return the usable size (depth) of the current thread's stack region in bytes
    uint32_t get_stack_depth();

    // Get the pointer to the PLC input memory region
    void* get_plc_input();

    // Get the pointer to the PLC output memory region
    void* get_plc_output();

    // Sawtooth millisecond uptime counter (rolls over every ~49 days), using the low-power clock
    //   (equivalent to Arduino's millis() function)
    inline uint32_t current_ms();

    // Sawtooth microsecond uptime counter (rolls over every ~71 minutes), using the regular clock
    //   (equivalent to Arduino's micros() function)
    //   WARNING: this counter does not run when the device is in SLEEP mode, 
    //            it should only be used for short intervals during which the
    //            device is guaranteed to stay in RUN mode
    inline uint32_t current_us();

    // Monotonic millisecond uptime counter (no rollover), using the low-power clock
    inline uint64_t uptime_ms();

    // Monotonic microsecond uptime counter (no rollover), using the low-power clock
    //   WARNING: the low-power clock used for this counter will generally not
    //            have enough precision for true microseconds. On an Arduino OPTA, 
    //            this clock runs at 32.768 kHz, which means that the smallest interval
    //            it can measure is around ~30 microseconds.
    inline uint64_t uptime_us();

    // Ensure that all precedent explicit memory accesses are completed
    inline void dsb();

    // Copy a block of memory with hardware-enforced ordering.
    template <typename T>
    inline void ordered_copy(T* dst, const T* src);

    // Atomically push a value into a memory location.
    template<typename T>
    inline void ordered_move(T* dst, T src);

}}

#endif // HAL_PLATFORM_H
