#ifndef UTILS_SERIAL_H
#define UTILS_SERIAL_H

/**
 * @file serial.h
 * @brief BufferedSerial - non-blocking UART wrapper with RX/TX ring buffers
 *        over HAL::RawSerial.
 */

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string.h>

#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Utils {

    namespace {
        constexpr bool is_power_of_two(uint16_t n) {
            return (n > 0) && ((n & (n - 1)) == 0);
        }
    }

    /**
     * @brief Non-blocking buffered serial for Arduino UART.
     *
     *  Both the TX and RX rings are single-producer/single-consumer :
     *   + RX is filled by receive(), drained by read()
     *   + TX is drained by transmit(), filled by write()
     *
     *  In THIS firmware every method runs on the main loop : we have no serial
     *  ISRs (the Arduino UART driver keeps its own ISR below the HAL). The ring is
     *  therefore NOT ISR-safe as wired - never call a method from interrupt
     *  context. Because producer and consumer are the same (single) thread, no
     *  memory barrier is needed: program order alone keeps the head/tail and the
     *  byte stores consistent. The head/tail stay `volatile` as an intent marker
     *  (and so a future ISR/second-thread rewiring is a localized change, which
     *  would then have to add the barriers back). The only genuine async boundary
     *  in this firmware is the PLC shared memory, which carries its own barriers. */
    class BufferedSerial {
    private:

        /** @name State
         *  @{ */

        /** @brief Wrapped raw serial port. */
        HAL::RawSerial* _serial;

        /** @brief RX ring buffer capacity, bytes (power of two). */
        static const uint16_t RX_BUF_SIZE = 256;
        /** @brief TX ring buffer capacity, bytes (power of two). */
        static const uint16_t TX_BUF_SIZE = 4096;

        // The masking arithmetic for the head/tail indices requires power-of-two sizes.
        static_assert(is_power_of_two(RX_BUF_SIZE), "RX_BUF_SIZE must be a power of two");
        static_assert(is_power_of_two(TX_BUF_SIZE), "TX_BUF_SIZE must be a power of two");

        /** @brief RX ring buffer storage. */
        char        _rx_buf[RX_BUF_SIZE];
        /** @brief TX ring buffer storage. */
        char        _tx_buf[TX_BUF_SIZE];

        /** @brief RX ring head/tail indices (head: producer, tail: consumer). */
        volatile uint16_t    _rx_head = 0, _rx_tail = 0;
        /** @brief TX ring head/tail indices (head: producer, tail: consumer). */
        volatile uint16_t    _tx_head = 0, _tx_tail = 0;

        /** @brief Cumulative bytes that didn't fit in the TX ring on write().
         *  Meaningful for callers that don't retry (console command output drops
         *  them); callers that resume a partial write (the log sink) inflate it,
         *  so only watch this on a serial whose writers don't resume. Saturating. */
        uint32_t    _tx_dropped = 0;

        /** @brief Count of receive() cycles that aborted with the RX ring full: the
         *  main loop fell behind the device, so bytes may have been lost in the device
         *  FIFO (the hardware overrun is otherwise invisible). Saturating. */
        uint32_t    _rx_overrun = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind to the given raw serial port */
        BufferedSerial(HAL::RawSerial* serial);

        /** @} */

        /** @name Status & capacity
         *  @{ */

        /** @brief Number of bytes available to read from the RX buffer */
        uint16_t available() const;
        /** @brief Number of bytes that can still be written to the TX buffer */
        uint16_t availableForWrite() const;
        /** @brief True if the RX buffer contains no data */
        bool rx_empty() const;
        /** @brief True if the TX buffer contains no data */
        bool tx_empty() const;
        /** @brief True if the bound port's output is an interactive terminal
         *  (always false on hardware UARTs). */
        bool is_output_tty() const;
        /** @brief RX ring buffer size, bytes (usable depth is rx_capacity() - 1, i.e.
         *  255: one slot is reserved to tell a full ring from an empty one). */
        static constexpr uint16_t rx_capacity() { return RX_BUF_SIZE; }
        /** @brief TX ring buffer size, bytes (usable depth is tx_capacity() - 1, i.e.
         *  4095: one slot is reserved to tell a full ring from an empty one). */
        static constexpr uint16_t tx_capacity() { return TX_BUF_SIZE; }
        /** @brief Cumulative bytes dropped because the TX ring was full (see above). */
        uint32_t tx_dropped() const { return _tx_dropped; }
        /** @brief Count of receive() cycles aborted with a full RX ring (see above). */
        uint32_t rx_overrun() const { return _rx_overrun; }

        /** @} */

        /** @name Application I/O
         *  @{ */

        /** @brief Copy up to @p len bytes into the TX ring buffer.
         *  @return Number of bytes actually written; may be less than @p len
         *          if the buffer is full (never blocks). */
        uint16_t write(const char* buf, uint16_t len);
        /** @brief Drop every byte queued in the TX ring, leaving it empty.
         *  Used before a full-screen repaint: a clear_screen makes all pending TX
         *  bytes moot (they'd paint onto a display about to be wiped), and emptying
         *  the ring first guarantees the clear sequence fits.
         *  @warning Single-producer reset - snaps the producer head back to the
         *  consumer tail, so it must run in the same context as transmit() (the
         *  main loop). Safe here because all access is main-loop only. */
        void tx_discard();
        /** @brief Consume up to @p len bytes from the RX ring buffer */
        uint16_t read(char* buf, uint16_t len);
        /** @brief Read the next RX byte without consuming it.
         *  @return false if the RX buffer is empty */
        bool peek(uint8_t* b) const;

        /** @} */

        /** @name Hardware I/O
         *  @{ */

        /** @brief Drain the hardware UART FIFO into the RX ring buffer */
        uint16_t receive();
        /** @brief Flush the TX ring buffer to the hardware UART FIFO */
        uint16_t transmit();
        /** @brief Run one receive + transmit cycle. Main-loop only (not ISR-safe). */
        void step();

        /** @} */
    };

}}

#endif // UTILS_SERIAL_H