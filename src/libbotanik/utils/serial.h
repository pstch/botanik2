// REVIEW_35: DONE
#ifndef UTILS_SERIAL_H
#define UTILS_SERIAL_H

/**
 * @file serial.h
 * @brief BufferedSerial - non-blocking UART wrapper with RX/TX ring buffers
 *        over HAL::Serial::Raw.
 */

#include <cstdint>                      // uint8_t/uint16_t/uint64_t

#include "libbotanik/config/serial.h"     // Config::SERIAL_* / *_BAUD

#include "libbotanik/core/component.h"  // Component
#include "libbotanik/hal/platform.h"    // HAL::Serial
#include "libbotanik/utils/byte_sink.h" // Utils::ByteSink

namespace Botanik { namespace Utils {

    /** @brief Ring sizes must be powers of two (the wrap is a mask). */
    constexpr bool is_power_of_two(uint16_t n) {
        return (n > 0) && ((n & (n - 1)) == 0);
    }

    /** @class BufferedSerial
     *  @brief Non-blocking buffered serial for Arduino UART.
     *
     *  Both rings are single-producer/single-consumer :
     *   - RX: receive() fills, read() drains
     *   - TX:  write() fills,   transmit() drains.
     *
     *  Every method runs on the main loop, the ring is NOT ISR-safe.
     *  Producer and consumer being the same thread, no barrier is needed. */
    class BufferedSerial : public Botanik::Component, public ByteSink {
    private:

        /** @name State
         *  @{ */

        HAL::Serial::Raw* _serial;   // wrapped raw port

        // Buffered serial RX/TX ring sizes, per port.
        static const uint16_t RX_BUF_SIZE = Config::SERIAL_RX_BUF;
        static const uint16_t TX_BUF_SIZE = Config::SERIAL_TX_BUF;

        // assert buffer sizes are power-of-two
        static_assert(is_power_of_two(RX_BUF_SIZE),
                      "RX_BUF_SIZE must be a power of two");
        static_assert(is_power_of_two(TX_BUF_SIZE),
                      "TX_BUF_SIZE must be a power of two");

        char _rx_buf[RX_BUF_SIZE];
        char _tx_buf[TX_BUF_SIZE];

        // head: producer, tail: consumer
        volatile uint16_t _rx_head = 0, _rx_tail = 0;
        volatile uint16_t _tx_head = 0, _tx_tail = 0;

        /** @brief Cumulative bytes that didn't fit in the TX ring on write().
         *  @note This counter is saturating. */
        uint32_t _tx_dropped = 0;

        /** @brief Count of receive() cycles aborted with the RX ring full.
         *  @note This counter is saturating. */
        uint32_t _rx_overrun = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        BufferedSerial(HAL::Serial::Raw* serial, Logs::Logger* logger = nullptr,
                       const char* name = "BufferedSerial");

        /** @brief True if the bound port's output is an interactive terminal
         *  (always false on hardware UARTs). */
        bool is_output_tty() const;

        /** @}
         *  @name Status & capacity
         *  @{ */

        /** @brief Bytes available to read from the RX buffer. */
        uint16_t available() const;
        /** @brief Bytes that can still be written to the TX buffer (ByteSink). */
        uint16_t availableForWrite() const override;

        /** @brief True when the RX ring holds no unread bytes. */
        bool rx_empty() const;
        /** @brief True when the TX ring has nothing left to transmit. */
        bool tx_empty() const;

        /** @brief RX ring size, bytes (usable depth is this - 1: one slot
         *  reserved to tell full from empty). */
        static constexpr uint16_t rx_capacity() { return RX_BUF_SIZE; }
        /** @brief TX ring size, bytes (usable depth is this - 1). */
        static constexpr uint16_t tx_capacity() { return TX_BUF_SIZE; }

        /** @brief Cumulative bytes dropped because the TX ring was full (see
         *  above). */
        uint32_t tx_dropped() const { return _tx_dropped; }
        /** @brief Count of receive() cycles aborted with a full RX ring (see
         *  above). */
        uint32_t rx_overrun() const { return _rx_overrun; }

        /** @}
         *  @name Application I/O
         *  @{ */

        /** @brief Copy up to @p len bytes into the TX ring (ByteSink). @return
         *  Bytes written; < @p len if the ring filled (never blocks). */
        uint16_t write(const char* buf, uint16_t len) override;

        /** @brief Drop every queued TX byte, leaving the ring empty.
         *  @note Used before a full-screen repaint.
         *  @warning Single-producer reset; must run in the main-loop. */
        void tx_discard();

        /** @brief Consume up to @p len bytes from the RX ring. */
        uint16_t read(char* buf, uint16_t len);

        /** @brief Peek the next RX byte without consuming it; false if empty. */
        bool peek(uint8_t* b) const;

        /** @}
         *  @name Hardware I/O
         *  @{ */

        /** @brief Drain the hardware UART FIFO into the RX ring. */
        uint16_t receive();
        /** @brief Flush the TX ring to the hardware UART FIFO. */
        uint16_t transmit();

        /** @} */

    protected:
        /** @brief Take the wire's bytes into the RX ring (PHASE_INPUT).
         *  Main-loop only (not ISR-safe). */
        void on_input(uint64_t now_ms) override;
        /** @brief Push the TX ring out at the link's pace (PHASE_PRESENT). */
        void on_present(uint64_t now_ms) override;
    };

}}

#endif // UTILS_SERIAL_H