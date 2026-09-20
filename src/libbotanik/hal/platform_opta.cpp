/** @file platform_opta.cpp
 *  @brief The cold half of the OPTA HAL: everything the per-beat path does not call.
 *  @note The only place mbed.h is allowed in; it alone preprocesses to 97,696
 *        lines, and hal/platform.h is reached by ~30 TUs. */

#include "libbotanik/hal/platform.h"

#ifdef TARGET_OPTA

// OPTA backend

#include <cstdint>
#include <ctime>

// mbed.h lives HERE and not in opta.h (see the file header): the backend header
// carries only the CMSIS intrinsics it must inline, and the two pieces of Mbed
// C++ the HAL needs (LowPowerClock, the Watchdog singleton) are defined below.
#include "mbed.h"
#include "USBSerial.h"

// Arduino.h defines `Serial` as a macro (#define Serial _UART_USB_), and the HAL
// has a namespace by that name - so everything below would be parsed as
// `namespace _UART_USB_`. Must be dropped here: after the vendor headers, before
// any of our own code.
#ifdef Serial
#undef Serial
#endif

namespace Botanik { namespace HAL {

    const char* hardware_description() {
        return "Arduino OPTA";
    }

    namespace Stack {

    StackUsage high_water() {
        // The RTOS thread, not the MSP stack _estack bounds: setup()/loop() run
        // in the Arduino Mbed main thread. RTX paints each thread's stack at
        // creation, so the watermark is already there to read and there is
        // nothing to paint here.
        const osThreadId_t id = osThreadGetId();
        const uint32_t size  = static_cast<uint32_t>(osThreadGetStackSize(id));
        const uint32_t space = static_cast<uint32_t>(osThreadGetStackSpace(id));
        StackUsage u;
        u.depth = size;
        // space is min free EVER; 0 means RTX tracking is off, not a full stack.
        u.used  = (space > 0 && space <= size) ? (size - space) : 0;
        return u;
    }

    }  // namespace Stack

    namespace Clock {

    // Mbed's clock, not Arduino's millis(): we link Mbed without the Arduino
    // core, so millis() would not resolve.
    uint32_t current_ms() {
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                mbed::LowPowerClock::now().time_since_epoch()).count());
    }

    uint64_t uptime_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            mbed::LowPowerClock::now().time_since_epoch()
        ).count();
    }

    uint64_t uptime_us() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            mbed::LowPowerClock::now().time_since_epoch()
        ).count();
    }

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

    namespace {

    /** @brief DWT cycles per microsecond: 0 before the first probe, HRT_NONE
     *  when the counter is not there and the low-power clock stands in. */
    const uint32_t HRT_NONE = 0xFFFFFFFFu;
    uint32_t g_hrt_per_us = 0;

    void hrt_probe() {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        // DWT_LAR. Some ARMv7-M parts lock the unit; written by ADDRESS because
        // CMSIS declares the register for some cores and not others, and a
        // write where it is reserved is harmless.
        *(volatile uint32_t*)0xE0001FB0u = 0xC5ACCE55u;
        DWT->CYCCNT = 0;
        DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
        // The enable bit not sticking is how a part with no cycle counter
        // answers. Fall back rather than report zeros for ever.
        if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0) { g_hrt_per_us = HRT_NONE; return; }
        const uint32_t per = SystemCoreClock / 1000000u;
        g_hrt_per_us = (per != 0) ? per : 1u;
    }

    }  // namespace

    uint32_t hrt_ticks() {
        // Lazily, not from a constructor: this pokes the debug unit, and the
        // first caller is the beat rather than static init.
        if (g_hrt_per_us == 0) hrt_probe();
        if (g_hrt_per_us == HRT_NONE) return (uint32_t)uptime_us();
        return DWT->CYCCNT;
    }

    uint32_t hrt_us(uint32_t ticks) {
        return (g_hrt_per_us == 0 || g_hrt_per_us == HRT_NONE)
                   ? ticks : (ticks / g_hrt_per_us);
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

    /** @name RS-485 field bus
     *
     *  The only endpoint that is a BUS, and the two constraints stay hidden here:
     *  only one device may drive the pair at a time (drop DE early and the last
     *  byte truncates, late and we are still driving when the slave answers), and
     *  a UART's software buffer going empty is not the shift register going empty.
     *
     *  PB_14 is USART3's own RTS/DE pin, so the handover is the peripheral's -
     *  alternate function plus CR3.DEM, the silicon asserting DE a guard time
     *  before the start bit and dropping it a guard time after the last stop bit.
     *  RE is held asserted, so we hear our own transmission come back exactly as
     *  long as what we wrote: that is what fieldbus_echoes() reports.
     *  @{ */

    /** @brief The RS-485 transceiver on USART3. Pins are Mbed's, not the Arduino
     *  variant's: TX PB_10, RX PB_11, DE PB_14, RE PB_13. */
    class Rs485Raw : public Raw {

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

    public:
        Rs485Raw() : _port(PB_10, PB_11), _re(PB_13, 0) {
            _port.set_blocking(false);   // the main loop must never stall on IO
            format(19200, 8, PARITY_NONE, 1);   // ...which arms DE
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
            // Re-armed after every reformat, not only in the constructor.
            // set_baud/set_format go through HAL_UART_Init, and whether CR3.DEM
            // and the CR1 guard times survive that is a property of the Mbed
            // and ST HAL versions in the tree rather than of this code. Losing
            // DE means the transceiver never drives and the bus goes silent, so
            // it is not a thing to be right about by reading.
            _driver_enable();
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
         *  Computed, not observed - there is no TX-complete interrupt - from the
         *  character-time arithmetic write() accumulates.
         *  @note Gates the START of the next frame, not listening: the master
         *  listens from the moment the frame is queued. */
        bool tx_idle() const { return Clock::uptime_us() >= _tx_done_us; }

        /** @brief Throw away whatever is sitting in the receive path. Called at
         *  bring-up and immediately before each frame goes out, NEVER between
         *  the frame leaving and the answer arriving. Bounded at two full frames.
         *  @return how many bytes were discarded, so the caller can report a
         *  device that answered after its turn had passed. */
        uint16_t flush_input() {
            uint8_t c;
            uint16_t n = 0;
            for (uint16_t i = 0; i < 512; ++i) {
                if (_port.read(&c, 1) != 1) break;
                n++;
            }
            return n;
        }

    private:
        /** @brief Put PB_14 into its USART3_DE alternate function and switch the
         *  peripheral's Driver Enable mode on. Mbed configures TX and RX only.
         *  IDEMPOTENT, and called after every format() for that reason.
         *  @note The guard-time fields live in CR1, write-protected while the
         *  USART is enabled - hence the disable / modify / restore dance. */
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

    namespace {
    /** @brief The one RS-485 port. A function-local static, like the other two
     *  transports: constructed on first use (after the RTOS is up) rather than
     *  before main(), which matters here because the constructor touches the
     *  USART registers. */
    Rs485Raw* fieldbus() {
        static Rs485Raw bus;
        return &bus;
    }
    }  // namespace

    Raw* get(Port port) {
        static UartRaw console(CONSOLE_TX, CONSOLE_RX);
        static UsbRaw  monitor;
        if (port == FIELDBUS) return static_cast<Raw*>(fieldbus());
        return (port == MONITOR) ? static_cast<Raw*>(&monitor)
                                 : static_cast<Raw*>(&console);
    }

    bool is_output_tty(const Raw*) { return false; }

    // Field bus control (declared in opta.h). Here rather than inline in the
    // header because Rs485Raw is only complete in this file.

    void fieldbus_begin(uint32_t baud, uint8_t data_bits, Parity parity,
                        uint8_t stop_bits) {
        fieldbus()->format(baud, data_bits, parity, stop_bits);
    }

    bool fieldbus_tx_idle() { return fieldbus()->tx_idle(); }

    bool fieldbus_echoes() { return true; }   // RE is tied low; see opta.h

    uint16_t fieldbus_flush_input() { return fieldbus()->flush_input(); }

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

    void kick() {
        mbed::Watchdog::get_instance().kick();
    }

    void stop() {
        mbed::Watchdog::get_instance().stop();
    }

    }  // namespace Watchdog

}}

#endif // TARGET_OPTA
