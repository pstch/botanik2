// REVIEW_35: TODO
#ifndef HAL_OPTA_H
#ifdef TARGET_OPTA
#define HAL_OPTA_H

/**
 * @file opta.h
 * @brief HAL backend for the Arduino OPTA (STM32H747 dual-core ARM).
 *
 * Implements the platform.h interface on top of Mbed OS and the Arduino core.
 * Selected automatically when TARGET_OPTA is defined; see platform.h.
 */

// assert() - HAL sits below the logger, so it can't use BOTANIK_ASSERT
#include <cassert>
#include <cstdint>
#include <chrono>
#include <ctime>
#include <type_traits>

// mbed.h: mbed::LowPowerClock, mbed::BufferedSerial, CMSIS intrinsics
#include "mbed.h"
#include "USBSerial.h"    // MONITOR transport (USB CDC)

/// Linker symbol for the top of the MSP/ISR stack (the ~1KB exception stack,
/// NOT the RTOS thread stack the firmware runs on).
extern "C" char _estack;


namespace Botanik { namespace HAL {

    /** @name Hardware identity
     *  @{ */

    inline const char* hardware_description() {
        return "Arduino OPTA";
    }

    /** @} */

    /** @brief Stack region introspection. */
    namespace Stack {

    inline uintptr_t base() {
        return reinterpret_cast<uintptr_t>(&_estack);
    }

    inline uint32_t depth() {
        // Size of the CURRENT RTOS thread's stack: setup()/loop() run in the
        // Arduino Mbed main thread, NOT on the ~1KB MSP stack _estack bounds,
        // so ask the RTOS.
        return static_cast<uint32_t>(osThreadGetStackSize(osThreadGetId()));
    }

    /** @brief High-water stack usage (bytes) of the current RTOS thread, from
     *  RTX's own watermark (RTX paints each thread's stack at creation, so no
     *  manual painting on hardware). Returns 0 if the watermark is unavailable
     *  (RTX tracking disabled). */
    inline uint32_t high() {
        osThreadId_t id = osThreadGetId();
        uint32_t size  = static_cast<uint32_t>(osThreadGetStackSize(id));
        // space: min free ever
        uint32_t space = static_cast<uint32_t>(osThreadGetStackSpace(id));
        return (space > 0 && space <= size) ? (size - space) : 0;
    }

    }  // namespace Stack

    /** @brief Timing and clocks. */
    namespace Clock {

    inline uint32_t current_ms() {
        // Mbed's clock, not Arduino's millis(): we link Mbed without the
        // Arduino core, so millis() would not resolve.
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                mbed::LowPowerClock::now().time_since_epoch()).count());
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

    inline uint64_t wall_ms() {
        // TODO [WALLCLOCK-WRAP-2038]: time() returns time_t, which is 32-bit on
        // this newlib and WRAPS in Jan 2038 before the widening cast - this
        // must be sourced from a 64-bit RTC read (it also only resolves whole
        // seconds; read the RTC sub-second register for ms).
        return (uint64_t)time(NULL) * 1000;
    }

    inline void set_wall_ms(uint64_t unix_ms) {
        // Programs the STM32 RTC, which is what time() reads back. Whole
        // seconds only: Mbed's set_time() takes a time_t, so the sub-second
        // part of an NTP sync is dropped (see WALLCLOCK-WRAP-2038).
        set_time((time_t)(unix_ms / 1000u));
    }

    inline void sleep_ms(uint32_t ms) {
        rtos::ThisThread::sleep_for(std::chrono::milliseconds(ms));
    }

    }  // namespace Clock

    /** @brief Memory-ordering primitives and D-cache maintenance. */
    namespace Memory {

    inline void dsb() {
        __DSB();
    }

    inline void dmb() {
        __DMB();
    }

    template <typename T>
    inline T ordered_read(const volatile T* src) {
        T value = *src;   // volatile load: the compiler must re-read it
        __DMB();          // acquire: order later accesses after this read
        return value;
    }

    }  // namespace Memory

    /** @brief Debugger interaction. */
    namespace Debug {

    inline void breakpoint() {
        __BKPT(0);  // CMSIS breakpoint intrinsic, provided via mbed.h
    }

    inline bool under_debugger() {
        // C_DEBUGEN is set while a halting debugger is attached to the core.
        return (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0;
    }

    }  // namespace Debug

    /** @brief System lifecycle (reset / shutdown). */
    namespace System {

    inline void reset() {
        // NVIC_SystemReset is the portable CMSIS reset (always available via
        // mbed.h); mbed_reset() is an alternative if a softer Mbed-managed
        // reset is wanted. Compile-reasoned only - confirm on a real
        // arm-none-eabi build.
        NVIC_SystemReset();
    }

    inline void shutdown(int /*code*/) {  // default (0) declared in platform.h
        // Firmware has no clean "halt and return to OS" - there is no OS. The
        // closest sane behaviour is to reboot, so shutdown mirrors reset here.
        // There is no process exit status to carry, so @p code is ignored.
        reset();
    }

    }  // namespace System

    /** @brief Hardware watchdog. */
    namespace Watchdog {

    // Wrap the Mbed OS Watchdog singleton (the STM32H7 IWDG). Compile-reasoned
    // only - confirm the API and timeout ceiling on a real arm-none-eabi build.

    inline void start(uint32_t timeout_ms) {
        // Clamp to the peripheral's max window; the hardware rejects anything
        // above it.
        uint32_t max = mbed::Watchdog::get_instance().get_max_timeout();
        mbed::Watchdog::get_instance().start(timeout_ms > max ? max : timeout_ms);
    }

    inline void kick() {
        mbed::Watchdog::get_instance().kick();
    }

    inline void stop() {
        mbed::Watchdog::get_instance().stop();
    }

    }  // namespace Watchdog

    /**
     * @brief Serial access.
     *
     * CONSOLE and MONITOR are *logical* endpoints. Each is backed by a concrete
     * transport behind one small interface, so which physical port serves which
     * role is a one-line change in get() rather than a change to any caller.
     * Today: CONSOLE on the hardware UART (Mbed's own CONSOLE_TX/RX pins for
     * this target, PG_14 / PG_9), MONITOR on USB CDC.
     *
     * Pure Mbed - no Arduino core. Virtual dispatch is deliberate and cheap
     * here: these are two long-lived RAM objects, not the flash-resident
     * descriptor tables where a vptr would force the table out of .rodata.
     */
    namespace Serial {

    /** @brief The transport-independent surface Utils::BufferedSerial drives. */
    class Raw {
    public:
        virtual ~Raw() {}
        /** @brief Bring the port up at @p baud. Transports with no line rate
         *  (USB CDC) ignore it. */
        virtual void     begin(uint32_t baud) = 0;
        /** @brief Next buffered byte, or -1 when there is none. */
        virtual int      read() = 0;
        /** @brief Queue one byte; returns the number accepted (0 or 1). */
        virtual size_t   write(uint8_t c) = 0;
        /** @brief Non-zero while the transport can take more output. Gates the
         *  TX drain loop, which writes a byte at a time and re-checks. */
        virtual uint16_t availableForWrite() = 0;
    };

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

    /** @brief Logical serial endpoints: CONSOLE drives the interactive
     *  terminal, MONITOR the secondary diagnostic/log output. */
    enum Port { CONSOLE, MONITOR };

    /** @brief Resolve a logical Port to its transport. Swapping the two is a
     *  change to these two lines and nothing else. Function-local statics, so
     *  each is constructed on first use (after the RTOS is up) rather than
     *  before main(). */
    inline Raw* get(Port port) {
        static UartRaw console(CONSOLE_TX, CONSOLE_RX);
        static UsbRaw  monitor;
        return (port == MONITOR) ? static_cast<Raw*>(&monitor)
                                 : static_cast<Raw*>(&console);
    }

    /** @brief Neither transport is an interactive terminal, so diagnostic
     *  output is never suppressed on OPTA. Mirrors the simulator query so sinks
     *  (e.g. the MONITOR SerialLogWriter) stay platform-independent. */
    inline bool is_output_tty(const Raw*) { return false; }

    }  // namespace Serial


}}

#endif // TARGET_OPTA
#endif // HAL_OPTA_H
