#ifndef UTILS_SERIAL_H
#define UTILS_SERIAL_H

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string.h>

#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Utils {

    namespace detail {
        constexpr bool is_power_of_two(uint16_t n) {
            return (n > 0) && ((n & (n - 1)) == 0);
        }
    }

    /**
     * Non-blocking buffered serial for Arduino UART.
     * Thread-safe on 32-bit platforms.
     */
    class BufferedSerial {
    private:
        // Wrapped serial handler
        HAL::RawSerial* _serial;

        // RX/TX buffers
        static const uint16_t RX_BUF_SIZE = 256;
        static const uint16_t TX_BUF_SIZE = 1024;

        static_assert(detail::is_power_of_two(RX_BUF_SIZE), "RX_BUF_SIZE must be a power of two");
        static_assert(detail::is_power_of_two(TX_BUF_SIZE), "TX_BUF_SIZE must be a power of two");

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