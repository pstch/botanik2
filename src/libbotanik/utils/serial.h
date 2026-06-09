// REVIEW_34: OK
#ifndef UTILS_SERIAL_H
#define UTILS_SERIAL_H

/**
 * @file serial.h
 * @brief BufferedSerial — non-blocking UART wrapper with RX/TX ring buffers
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
     *  The implementation is lockless : the hardware-facing side 
     *  (receive()/transmit()) may run from an UART ISR while the 
     *  main loop runs read()/write(), using volatile head/tail 
     *  indices and HAL::dsb() barriers to make them safe. */
    class BufferedSerial {
    private:
        // Wrapped serial handler
        HAL::RawSerial* _serial;

        // RX/TX buffer sizes
        static const uint16_t RX_BUF_SIZE = 256;
        static const uint16_t TX_BUF_SIZE = 4096;

        // Assert valid buffer sizes
        static_assert(is_power_of_two(RX_BUF_SIZE), "RX_BUF_SIZE must be a power of two");
        static_assert(is_power_of_two(TX_BUF_SIZE), "TX_BUF_SIZE must be a power of two");

        // RX/TX buffers
        char        _rx_buf[RX_BUF_SIZE];
        char        _tx_buf[TX_BUF_SIZE];

        // Ring buffer pointers
        volatile uint16_t    _rx_head = 0, _rx_tail = 0;
        volatile uint16_t    _tx_head = 0, _tx_tail = 0;

    public:
        /** @brief Bind to the given raw serial port */
        BufferedSerial(HAL::RawSerial* serial);

        /** @brief Number of bytes available to read from the RX buffer */
        uint16_t available() const;
        /** @brief Number of bytes that can still be written to the TX buffer */
        uint16_t availableForWrite() const;

        /** @brief True if the RX buffer contains no data */
        bool rx_empty() const;
        /** @brief True if the TX buffer contains no data */
        bool tx_empty() const;
        /** @brief Copy up to @p len bytes into the TX ring buffer.
         *  @return Number of bytes actually written; may be less than @p len
         *          if the buffer is full (never blocks). */

        uint16_t write(const char* buf, uint16_t len);
        /** @brief Consume up to @p len bytes from the RX ring buffer */
        uint16_t read(char* buf, uint16_t len);
        /** @brief Read the next RX byte without consuming it.
         *  @return false if the RX buffer is empty */
        bool peek(uint8_t* b) const;

        /** @brief Drain the hardware UART FIFO into the RX ring buffer */
        uint16_t receive();
        /** @brief Flush the TX ring buffer to the hardware UART FIFO */
        uint16_t transmit();
        /** @brief Run one receive + transmit cycle (called from the main loop) */
        void step();
    };

}}

#endif // UTILS_SERIAL_H