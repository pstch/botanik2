
#include <cstdint>

#include "serial.h"

/* Const expression to determine if a number is a power of two */
static constexpr bool is_power_of_two(uint16_t n) {
    return (n > 0) && ((n & (n - 1)) == 0);
}

namespace Botanix { namespace Utils {


    BufferedSerial::BufferedSerial(RawSerial* serial) : 
        _serial(serial) {}

    uint16_t BufferedSerial::available() {
        uint16_t occupied = (_tx_head - _tx_tail) & (TX_BUF_SIZE - 1);
        return (TX_BUF_SIZE - 1) - occupied;
    }

    uint16_t BufferedSerial::write(const char* buf, uint16_t len) {
        uint16_t pos = 0;
        while (pos < len) {
            uint16_t next_head = (_tx_head + 1) & (TX_BUF_SIZE - 1);
            if (next_head == _tx_tail) return pos;
            _tx_buf[_tx_head] = buf[pos++];
            dsb();
            _tx_head = next_head;
        }
        return pos;
    }

    uint16_t BufferedSerial::read(char* buf, uint16_t len) {
        uint16_t pos = 0; while (pos < len && _rx_head != _rx_tail) {
            buf[pos++] = _rx_buf[_rx_tail];
            _rx_tail = (_rx_tail + 1) & (RX_BUF_SIZE - 1);
        }
        return pos;
    }

    uint16_t BufferedSerial::receive() {
        int byte; uint16_t rcvd = 0;
        if (!_serial) return rcvd; 
        while ((byte = _serial->read()) != -1) {
            uint16_t head = (_rx_head + 1) & (RX_BUF_SIZE - 1);
            if (head == _rx_tail) return rcvd;
            _rx_buf[_rx_head] = static_cast<char>(byte);
            dsb();
            _rx_head = head;
            rcvd++;
        }
        return rcvd;
    }

    uint16_t BufferedSerial::transmit() {
        uint16_t sent = 0;
        if (!_serial) return sent;
        while (_tx_head != _tx_tail && _serial->availableForWrite() > 0) {
            _serial->write(static_cast<uint8_t>(_tx_buf[_tx_tail]));
            _tx_tail = (_tx_tail + 1) & (TX_BUF_SIZE - 1);
            sent++;
        }
        return sent;
    }
    
    void BufferedSerial::step() {
        receive();
        transmit();
    }

}}