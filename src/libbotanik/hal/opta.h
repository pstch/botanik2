#ifndef HAL_OPTA_H
#ifdef TARGET_OPTA
#define HAL_OPTA_H

#include <cstdint>
#include <chrono>

/**
 * This include provides :
 *  - micros()
 *  - millis()
 *  - mbed::LowPowerClock
 *  - arduino::UART
 *  
 */
#include "mbed.h"

struct PLCSharedVarsInput_t;
struct PLCSharedVarsOutput_t;

extern "C" void* m_PLCSharedVarsInputBuf;
extern "C" void* m_PLCSharedVarsOutputBuf;    

namespace Botanik { namespace HAL {

    // Get the pointer to the PLC input memory region
    inline PLCSharedVarsInput_t* get_plc_input() {
        return static_cast<PLCSharedVarsInput_t*>(m_PLCSharedVarsInputBuf);
    }

    // Get the pointer to the PLC output memory region
    inline PLCSharedVarsOutput_t* get_plc_output() {
        return static_cast<PLCSharedVarsOutput_t*>(m_PLCSharedVarsOutputBuf);
    }

    inline uint32_t current_ms() { 
        return millis();
    }

    inline uint32_t current_us() { 
        return micros();
    }

    inline uint64_t uptime_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            mbed::LowPowerClock::now().time_since_epoch()
        ).count();
    }

    inline uint64_t uptime_us() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            mbed::LowPowerClock::now().time_since_epoch()
        ).count();
    }

    inline void dsb() {
        __DSB();
    }

    template <typename T>
    inline void ordered_copy(T* dst, const T* src) {
        ARM_MPU_OrderedMemcpy((void*)dst, (const void*)src, sizeof(T)); 
        __DSB();
    }

    template<typename T>
    inline void ordered_move(T* dst, T src) {
        *dst = src; 
        __DSB();
    }

    using RawSerial = arduino::UART;

}}

#endif // TARGET_OPTA
#endif // HAL_OPTA_H