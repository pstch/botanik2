
#include <cstdint>

#include "serial.h"

/* Const expression to determine if a number is a power of two */
static constexpr bool is_power_of_two(uint16_t n) {
    return (n > 0) && ((n & (n - 1)) == 0);
}

/**
 * Non-blocking buffered serial for Arduino UART.
 * Thread-safe on 32-bit platforms.
 */
class BufferedSerial {
private:

    // Wrapped serial handler
    arduino::UART* _serial;

    // RX/TX buffers
    static const uint16_t RX_BUF_SIZE = 256;
    static const uint16_t TX_BUF_SIZE = 1024;
    
    static_assert(is_power_of_two(RX_BUF_SIZE), "RX_BUF_SIZE must be a power of two");
    static_assert(is_power_of_two(TX_BUF_SIZE), "TX_BUF_SIZE must be a power of two");

    char        _rx_buf[RX_BUF_SIZE];
    char        _tx_buf[TX_BUF_SIZE];

    // Ring buffer pointers
    volatile uint16_t    _rx_head = 0, _rx_tail = 0;
    volatile uint16_t    _tx_head = 0, _tx_tail = 0;

public:
    BufferedSerial(arduino::UART* serial) : 
        _serial(serial) {}

    // Get available space in TX buffer
    uint16_t available() {
        uint16_t occupied = (_tx_head - _tx_tail) & (TX_BUF_SIZE - 1);
        return (TX_BUF_SIZE - 1) - occupied;
    }

    // Write to the serial TX buffer
    uint16_t write(const char* buf, uint16_t len) {
        uint16_t pos = 0;
        while (pos < len) {
            uint16_t next_head = (_tx_head + 1) & (TX_BUF_SIZE - 1);
            if (next_head == _tx_tail) return pos;
            _tx_buf[_tx_head] = buf[pos++];
            __DMB();
            _tx_head = next_head;
        }
        return pos;
    }
    // Read bytes from the RX buffer
    uint16_t read(char* buf, uint16_t len) {
        uint16_t pos = 0; while (pos < len && _rx_head != _rx_tail) {
            buf[pos++] = _rx_buf[_rx_tail];
            _rx_tail = (_rx_tail + 1) & (RX_BUF_SIZE - 1);
        }
        return pos;
    }
    // Read bytes from the serial device into the RX buffer
    uint16_t receive() {
        int byte; uint16_t rcvd = 0;
        if (!_serial) return rcvd; 
        while ((byte = _serial->read()) != -1) {
            uint16_t head = (_rx_head + 1) & (RX_BUF_SIZE - 1);
            if (head == _rx_tail) return rcvd;
            _rx_buf[_rx_head] = static_cast<char>(byte);
            __DMB();
            _rx_head = head;
            rcvd++;
        }
        return rcvd;
    }
    // Transmit bytes from the TX buffer to the serial device
    uint16_t transmit() {
        uint16_t sent = 0;
        if (!_serial) return sent;
        while (_tx_head != _tx_tail && _serial->availableForWrite() > 0) {
            _serial->write(static_cast<uint8_t>(_tx_buf[_tx_tail]));
            _tx_tail = (_tx_tail + 1) & (TX_BUF_SIZE - 1);
            sent++;
        }
        return sent;
    }
    // Step through the TX/RX handlers
    void step() {
        receive();
        transmit();
    }
};
