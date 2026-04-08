#ifndef UTILS_SERIAL_H
#define UTILS_SERIAL_H

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string.h>

#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Utils {

    /* Const expression to determine if a number is a power of two */
    constexpr bool _is_power_of_two(uint16_t n) {
        return (n > 0) && ((n & (n - 1)) == 0);
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
        
        static_assert(_is_power_of_two(RX_BUF_SIZE), "RX_BUF_SIZE must be a power of two");
        static_assert(_is_power_of_two(TX_BUF_SIZE), "TX_BUF_SIZE must be a power of two");

        char        _rx_buf[RX_BUF_SIZE];
        char        _tx_buf[TX_BUF_SIZE];

        // Ring buffer pointers
        volatile uint16_t    _rx_head = 0, _rx_tail = 0;
        volatile uint16_t    _tx_head = 0, _tx_tail = 0;

    public:
        // Constructor
        BufferedSerial(HAL::RawSerial* serial);
        // Get available space in RX buffer
        uint16_t available() const;
        // Get available space in TX buffer
        uint16_t availableForWrite() const;
        // Determine whether the RX buffer is empty
        bool rx_empty() const;
        // Determine whether the TX buffer is empty
        bool tx_empty() const;
        // Write to the serial TX buffer
        uint16_t write(const char* buf, uint16_t len);
        // Read bytes from the RX buffer
        uint16_t read(char* buf, uint16_t len);
        // Peek the next byte in the character
        bool peek(uint8_t* b) const;
        // Read bytes from the serial device into the RX buffer
        uint16_t receive();
        // Transmit bytes from the TX buffer to the serial device
        uint16_t transmit();
        // Step through the TX/RX handlers
        void step();
    };

}}

#endif // UTILS_SERIAL_H