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
     *  Both rings are single-producer/single-consumer (RX: receive() fills, read()
     *  drains; TX: write() fills, transmit() drains). Every method runs on the main
     *  loop - there are no serial ISRs - so the ring is NOT ISR-safe as wired (never
     *  call from interrupt context). Producer and consumer being the same thread, no
     *  barrier is needed: program order keeps head/tail and the byte stores consistent.
     *  head/tail stay `volatile` as an intent marker (a future ISR/second-thread
     *  rewiring would then re-add the barriers, as a localized change). The only genuine
     *  async boundary here is the PLC shared memory, which carries its own barriers. */
    class BufferedSerial {
    private:

        /** @name State
         *  @{ */

        HAL::RawSerial* _serial;   // wrapped raw port

        static const uint16_t RX_BUF_SIZE = 256;    // power of two (masking arithmetic)
        static const uint16_t TX_BUF_SIZE = 4096;
        static_assert(is_power_of_two(RX_BUF_SIZE), "RX_BUF_SIZE must be a power of two");
        static_assert(is_power_of_two(TX_BUF_SIZE), "TX_BUF_SIZE must be a power of two");

        char        _rx_buf[RX_BUF_SIZE];
        char        _tx_buf[TX_BUF_SIZE];

        volatile uint16_t    _rx_head = 0, _rx_tail = 0;   // head: producer, tail: consumer
        volatile uint16_t    _tx_head = 0, _tx_tail = 0;

        /** @brief Cumulative bytes that didn't fit in the TX ring on write(). Callers
         *  that resume a partial write (the log sink) inflate it, so only watch it on a
         *  serial whose writers don't resume. Saturating. */
        uint32_t    _tx_dropped = 0;

        /** @brief Count of receive() cycles aborted with the RX ring full: the loop fell
         *  behind, so bytes may have been lost in the device FIFO. Saturating. */
        uint32_t    _rx_overrun = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        BufferedSerial(HAL::RawSerial* serial);

        /** @} */

        /** @name Status & capacity
         *  @{ */

        /** @brief Bytes available to read from the RX buffer. */
        uint16_t available() const;
        /** @brief Bytes that can still be written to the TX buffer. */
        uint16_t availableForWrite() const;
        bool rx_empty() const;
        bool tx_empty() const;
        /** @brief True if the bound port's output is an interactive terminal
         *  (always false on hardware UARTs). */
        bool is_output_tty() const;
        /** @brief RX ring size, bytes (usable depth is this - 1: one slot reserved to
         *  tell full from empty). */
        static constexpr uint16_t rx_capacity() { return RX_BUF_SIZE; }
        /** @brief TX ring size, bytes (usable depth is this - 1). */
        static constexpr uint16_t tx_capacity() { return TX_BUF_SIZE; }
        /** @brief Cumulative bytes dropped because the TX ring was full (see above). */
        uint32_t tx_dropped() const { return _tx_dropped; }
        /** @brief Count of receive() cycles aborted with a full RX ring (see above). */
        uint32_t rx_overrun() const { return _rx_overrun; }

        /** @} */

        /** @name Application I/O
         *  @{ */

        /** @brief Copy up to @p len bytes into the TX ring.
         *  @return Bytes written; < @p len if the ring filled (never blocks). */
        uint16_t write(const char* buf, uint16_t len);
        /** @brief Drop every queued TX byte, leaving the ring empty. Used before a
         *  full-screen repaint (pending bytes would paint onto a display about to be
         *  wiped, and emptying first guarantees the clear sequence fits).
         *  @warning Single-producer reset; must run in transmit()'s context (the main
         *  loop). Safe here because all access is main-loop only. */
        void tx_discard();
        /** @brief Consume up to @p len bytes from the RX ring. */
        uint16_t read(char* buf, uint16_t len);
        /** @brief Peek the next RX byte without consuming it; false if empty. */
        bool peek(uint8_t* b) const;

        /** @} */

        /** @name Hardware I/O
         *  @{ */

        /** @brief Drain the hardware UART FIFO into the RX ring. */
        uint16_t receive();
        /** @brief Flush the TX ring to the hardware UART FIFO. */
        uint16_t transmit();
        /** @brief Run one receive + transmit cycle. Main-loop only (not ISR-safe). */
        void step();

        /** @} */
    };

}}

#endif // UTILS_SERIAL_H