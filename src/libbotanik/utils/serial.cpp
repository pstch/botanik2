
#include <cstdint>

#include "serial.h"

namespace Botanik { namespace Utils {

    BufferedSerial::BufferedSerial(HAL::RawSerial* serial) :
        _serial(serial) {
        memset(_rx_buf, 0, sizeof(_rx_buf));
        memset(_tx_buf, 0, sizeof(_tx_buf));
    }

    uint16_t BufferedSerial::available() const {
        return (_rx_head - _rx_tail) & (RX_BUF_SIZE - 1);
    }

    uint16_t BufferedSerial::availableForWrite() const {
        uint16_t occupied = (_tx_head - _tx_tail) & (TX_BUF_SIZE - 1);
        return (TX_BUF_SIZE - 1) - occupied;
    }

    bool BufferedSerial::rx_empty() const {
        return _rx_head == _rx_tail;
    }

    bool BufferedSerial::tx_empty() const {
        return _tx_head == _tx_tail;
    }

    uint16_t BufferedSerial::write(const char* buf, uint16_t len) {
        uint16_t pos = 0;
        while (pos < len) {
            uint16_t next_head = (_tx_head + 1) & (TX_BUF_SIZE - 1);
            if (next_head == _tx_tail) return pos;
            _tx_buf[_tx_head] = buf[pos++];
            HAL::dsb(); _tx_head = next_head;
        }
        return pos;
    }

    uint16_t BufferedSerial::read(char* buf, uint16_t len) {
        uint16_t pos = 0; while (pos < len && _rx_head != _rx_tail) {
            HAL::dsb(); buf[pos++] = _rx_buf[_rx_tail];
            _rx_tail = (_rx_tail + 1) & (RX_BUF_SIZE - 1);
        }
        return pos;
    }

    bool BufferedSerial::peek(uint8_t* b) const {
        if (_rx_head == _rx_tail)           return false;
        HAL::dsb(); *b = _rx_buf[_rx_tail];      return true;
    }

    uint16_t BufferedSerial::receive() {
        int byte; uint16_t rcvd = 0;
        if (!_serial) return rcvd;
        while ((byte = _serial->read()) != -1) {
            uint16_t head = (_rx_head + 1) & (RX_BUF_SIZE - 1);
            if (head == _rx_tail) return rcvd;
            _rx_buf[_rx_head] = static_cast<char>(byte);
            HAL::dsb(); _rx_head = head;
            rcvd++;
        }
        return rcvd;
    }

    uint16_t BufferedSerial::transmit() {
        uint16_t sent = 0;
        if (!_serial) return sent;
        while (_tx_head != _tx_tail && _serial->availableForWrite() > 0) {
            HAL::dsb();
            // Stop if the byte couldn't be sent (non-blocking write hit a full
            // buffer); leave it queued and retry on the next cycle rather than
            // dropping it and tearing the output stream.
            if (_serial->write(static_cast<uint8_t>(_tx_buf[_tx_tail])) == 0) break;
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