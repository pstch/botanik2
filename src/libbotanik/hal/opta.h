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
#include <cassert>      // assert
#include <chrono>       // std::chrono
#include <cstdint>      // uint8_t/uint64_t/uintptr_t
#include <ctime>        // time / time_t
#include <type_traits>  // std::is_empty

// Arduino/Mbed core headers - resolved from the installed mbed_opta core, not this tree.
#include "mbed.h"                     // mbed::LowPowerClock / BufferedSerial / CMSIS intrinsics (__DSB, WDT)
#include "USBSerial.h"                // USBSerial - the MONITOR transport (USB CDC)
#include "hal/pinmap.h"               // pin_function / pinmap_peripheral - the RS-485 DE pin's alternate function
#include "hal/serial_api.h"           // serial_tx_pinmap - which USART Mbed bound the field bus to

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

    // @warning dst and src must not alias: ARM_MPU_OrderedMemcpy declares src
    // __RESTRICT, so an in-place/overlapping copy is UB on the optimized path.
    template <typename T>
    inline void ordered_copy(T* dst, const T* src) {
        // ARM_MPU_OrderedMemcpy counts 32-bit words, not bytes.
        static_assert(std::is_empty<T>::value || sizeof(T) % sizeof(uint32_t) == 0,
                      "ordered_copy needs a word-multiple T");
        if (std::is_empty<T>::value) return;   // empty payload carries no data
#if defined(DEBUG)
        assert((const void*)dst != (const void*)src);   // no aliasing (see @warning)
#endif
        ARM_MPU_OrderedMemcpy((volatile uint32_t*)dst, (const uint32_t*)src,
                              sizeof(T) / sizeof(uint32_t));
        __DSB();
    }

    template <typename T>
    inline void ordered_move(T* dst, T src) {
        static_assert(std::is_empty<T>::value || sizeof(T) % sizeof(uint32_t) == 0,
                      "ordered_move needs a word-multiple T");
        if (std::is_empty<T>::value) return;   // empty payload carries no data
        *dst = src;
        __DSB();
    }

    template <typename T>
    inline T ordered_read(const volatile T* src) {
        T value = *src;   // volatile load: the compiler must re-read it
        __DMB();          // acquire: order later accesses after this read
        return value;
    }

    // Cache discipline for a cross-core shared region. clean (write-back) is
    // always safe - it rounds to whole lines and only ever writes back, losing
    // nothing. invalidate DISCARDS whole lines, so it is safe ONLY on a region
    // we treat as read-only: invalidate what the other core produces, clean
    // what we produce. Sub-region calls are fine - they round to that region's
    // own lines - provided the two regions never share one (align each to 32).
    // @warning never invalidate a line holding data we wrote; that write is
    // silently dropped.
    inline void cache_clean(const void* addr, size_t len) {
    #if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        // CMSIS rounds to lines
        SCB_CleanDCache_by_Addr((void*)addr, (int32_t)len);
    #else
        (void)addr; (void)len;
    #endif
    }

    inline void cache_invalidate(const void* addr, size_t len) {
    #if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        SCB_InvalidateDCache_by_Addr((void*)addr, (int32_t)len);
    #else
        (void)addr; (void)len;
    #endif
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

    /** @brief Line format parity. The field bus needs it (Modbus RTU's default
     *  is 8E1); CONSOLE and MONITOR are 8N1 and never ask. */
    enum Parity : uint8_t { PARITY_NONE = 0, PARITY_EVEN, PARITY_ODD };

    /** @name RS-485 field bus
     *
     *  The third endpoint, and the only one that is a BUS rather than a
     *  point-to-point link. Two things follow from that, and both are hidden
     *  here so nothing above this file learns what RS-485 is:
     *
     *  - **Only one device may drive the pair at a time.** A transceiver's
     *    driver is switched on by DE and its receiver by RE (active low), and
     *    getting the handover wrong is not a soft failure: drop DE early and the
     *    last byte is truncated on the wire, drop it late and we are still
     *    driving when the slave answers.
     *  - **We cannot see when our own frame has left.** A UART's software
     *    buffer going empty is not the shift register going empty; up to two
     *    characters are still in flight.
     *
     *  @section fb_de Why the silicon drives DE and not us
     *
     *  PB_14 is not an arbitrary GPIO the board happened to wire to DE: it is
     *  USART3's own RTS/DE pin (Arduino picked it for exactly that reason, and
     *  then drove it by hand anyway). Putting it in its alternate function and
     *  setting CR3.DEM hands the whole handover to the peripheral, which asserts
     *  DE a guard time before the start bit and drops it a guard time after the
     *  last stop bit - timing we could not reproduce from a 10 ms beat, and
     *  would not want to try.
     *
     *  RE is held asserted, so the receiver is always listening. That means we
     *  hear our own transmission come back; see fieldbus_flush_input(), which is
     *  how the caller is meant to deal with it and which works whether or not
     *  the echo turns out to be real on this board.
     *  @{ */

    /** @brief The RS-485 transceiver on USART3.
     *
     *  Pins are Mbed's, not the Arduino variant's: TX PB_10, RX PB_11, DE PB_14,
     *  RE PB_13. HAL is pure Mbed by rule, so pins_arduino.h is not in scope
     *  here even though it names the same four. */
    class Rs485Raw : public Raw {

        /** @name State
         *  @{ */

        mbed::BufferedSerial _port;
        /** @brief Receiver enable, active low: 0 listens. Held there for the
         *  life of the port - see the note above. */
        mbed::DigitalOut     _re;
        /** @brief One character's time on the wire, derived from the baud and
         *  the frame format. What lets tx_idle() answer without a TX-complete
         *  interrupt we do not have. */
        uint32_t             _char_us = 0;
        /** @brief When the last byte handed to the UART will have finished
         *  leaving. Accumulated per byte rather than measured, because there is
         *  nothing to measure it with. */
        uint64_t             _tx_done_us = 0;

        /** @} */

    public:
        Rs485Raw() : _port(PB_10, PB_11), _re(PB_13, 0) {
            _port.set_blocking(false);   // the main loop must never stall on IO
            format(19200, 8, PARITY_NONE, 1);
            _driver_enable();
        }

        /** @brief Baud only, keeping the current frame format. */
        void begin(uint32_t baud) override { format(baud, 8, PARITY_NONE, 1); }

        /** @brief Set the whole line format. Modbus RTU's default is 8E1, and
         *  the wire spends the same eleven bits either way: a no-parity link
         *  spends its eleventh on a second stop bit. */
        void format(uint32_t baud, uint8_t data_bits, Parity parity, uint8_t stop_bits) {
            _port.set_baud((int)baud);
            _port.set_format((int)data_bits,
                             (parity == PARITY_EVEN) ? mbed::SerialBase::Even
                           : (parity == PARITY_ODD)  ? mbed::SerialBase::Odd
                                                     : mbed::SerialBase::None,
                             (int)stop_bits);
            // Start bit + data + parity + stop(s). At 19200 with 11 bits that is
            // 573 us, comfortably above the ~30 us the low-power clock resolves.
            const uint32_t bits = 1u + data_bits +
                                  ((parity == PARITY_NONE) ? 0u : 1u) + stop_bits;
            _char_us = baud ? (uint32_t)((1000000ULL * bits) / baud) : 0;
            _tx_done_us = Clock::uptime_us();
        }

        int read() override {
            uint8_t c;
            return _port.read(&c, 1) == 1 ? (int)c : -1;
        }

        size_t write(uint8_t c) override {
            if (_port.write(&c, 1) != 1) return 0;
            // Accumulate from wherever the wire had got to, so a burst of bytes
            // lands end to end rather than each one a character after "now".
            const uint64_t now = Clock::uptime_us();
            if (_tx_done_us < now) _tx_done_us = now;
            _tx_done_us += _char_us;
            return 1;
        }

        uint16_t availableForWrite() override { return _port.writable() ? 1 : 0; }

        /** @brief Whether everything written has finished leaving the wire.
         *
         *  Computed, not observed. The caller waits for this before listening,
         *  which is what makes the echo question moot: by the time this is true
         *  our frame is gone, and a slave cannot have answered yet (its
         *  turnaround is milliseconds), so anything in the receive path is ours
         *  or noise, and either way wants discarding. */
        bool tx_idle() const { return Clock::uptime_us() >= _tx_done_us; }

        /** @brief Throw away whatever is sitting in the receive path.
         *
         *  Bounded, because an unbounded drain inside the beat is exactly the
         *  thing this codebase does not do. Two full frames is far more than a
         *  turnaround can have delivered. */
        void flush_input() {
            uint8_t c;
            for (uint16_t i = 0; i < 512; ++i) {
                if (_port.read(&c, 1) != 1) return;
            }
        }

    private:
        /** @brief Put PB_14 into its USART3_DE alternate function and switch the
         *  peripheral's Driver Enable mode on.
         *
         *  Mbed configures TX and RX and nothing else, so the alternate function
         *  is ours to set. The guard-time fields live in CR1, which is
         *  write-protected while the USART is enabled, hence the disable /
         *  modify / restore dance. */
        void _driver_enable() {
            pin_function(PB_14, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_NOPULL,
                                             GPIO_AF7_USART3));
            // UARTName values are the peripheral's base address on STM32, so the
            // pinmap tells us which register block Mbed bound TX to. Derived
            // rather than hardcoded to USART3: if the pin ever moves, this moves
            // with it instead of quietly poking the wrong peripheral.
            USART_TypeDef* u =
                (USART_TypeDef*)pinmap_peripheral(PB_10, serial_tx_pinmap());
            if (u == nullptr) return;

            // Assertion and de-assertion guard times, in sample-time units
            // (1/16 of a bit at the default oversampling). Half a bit each way
            // is generous for a transceiver whose enable settles in nanoseconds.
            const uint32_t guard = 8;
            const uint32_t was_enabled = u->CR1 & USART_CR1_UE;
            u->CR1 &= ~USART_CR1_UE;
            u->CR3 |= USART_CR3_DEM;
            u->CR1 = (u->CR1 & ~(USART_CR1_DEAT_Msk | USART_CR1_DEDT_Msk)) |
                     (guard << USART_CR1_DEAT_Pos) |
                     (guard << USART_CR1_DEDT_Pos);
            u->CR1 |= was_enabled;
        }
    };

    /** @} */

    /** @brief Logical serial endpoints: CONSOLE drives the interactive
     *  terminal, MONITOR the secondary diagnostic/log output, FIELDBUS the
     *  RS-485 bus the Modbus RTU client talks on. */
    enum Port { CONSOLE, MONITOR, FIELDBUS };

    /** @brief The field bus transport, constructed on first use.
     *
     *  Its own accessor rather than a third static inside get(), and that is
     *  load-bearing: the statics in one function are all initialised on the
     *  first call, so putting it there would claim USART3 and four pins on every
     *  build that so much as writes to the console. Here, a firmware that never
     *  asks for the field bus never builds one. */
    inline Rs485Raw* fieldbus() { static Rs485Raw fb; return &fb; }

    /** @brief Resolve a logical Port to its transport. Swapping them is a change
     *  to these lines and nothing else. Function-local statics, so each is
     *  constructed on first use (after the RTOS is up) rather than before
     *  main(). */
    inline Raw* get(Port port) {
        static UartRaw console(CONSOLE_TX, CONSOLE_RX);
        static UsbRaw  monitor;
        if (port == FIELDBUS) return static_cast<Raw*>(fieldbus());
        return (port == MONITOR) ? static_cast<Raw*>(&monitor)
                                 : static_cast<Raw*>(&console);
    }

    /** @brief Neither transport is an interactive terminal, so diagnostic
     *  output is never suppressed on OPTA. Mirrors the simulator query so sinks
     *  (e.g. the MONITOR SerialLogWriter) stay platform-independent. */
    inline bool is_output_tty(const Raw*) { return false; }

    /** @name Field bus control
     *
     *  Free functions rather than virtuals on Raw, deliberately. Raw is the
     *  surface BufferedSerial drives for every port, and widening it would make
     *  CONSOLE and MONITOR implement a line format and a turnaround they do not
     *  have. These three are the field bus's alone, and the simulator backend
     *  declares the same three, so a caller never branches on the target.
     *  @{ */

    /** @brief Bring the bus up at @p baud with the given frame format. */
    inline void fieldbus_begin(uint32_t baud, uint8_t data_bits, Parity parity,
                               uint8_t stop_bits) {
        fieldbus()->format(baud, data_bits, parity, stop_bits);
    }

    /** @brief Whether everything written has finished leaving the wire. */
    inline bool fieldbus_tx_idle() { return fieldbus()->tx_idle(); }

    /** @brief Discard whatever is sitting in the receive path.
     *
     *  Called once a request has drained and before its answer is expected. On a
     *  bus where the receiver never stops listening, that is the moment our own
     *  echo is sitting there and the slave has not yet spoken - so this is what
     *  keeps a write echo from being mistaken for the response to it, which for
     *  FC05 and FC06 would be byte-identical and silently wrong. */
    inline void fieldbus_flush_input() { fieldbus()->flush_input(); }

    /** @} */

    }  // namespace Serial


}}

#endif // TARGET_OPTA
#endif // HAL_OPTA_H
