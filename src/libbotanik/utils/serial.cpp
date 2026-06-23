/**
 * @file serial.cpp
 * @brief BufferedSerial implementation - pumps the RX/TX rings to and from
 *        HAL::RawSerial without blocking the main loop.
 */
#include <cstdint>

#include "serial.h"

namespace Botanik { namespace Utils {

    // Lifecycle
    // ------------------------------------------------------------------------

    BufferedSerial::BufferedSerial(HAL::RawSerial* serial) :
        _serial(serial) {
        memset(_rx_buf, 0, sizeof(_rx_buf));
        memset(_tx_buf, 0, sizeof(_tx_buf));
    }

    // Status & capacity
    // ------------------------------------------------------------------------

    uint16_t BufferedSerial::available() const {
        return (_rx_head - _rx_tail) & (RX_BUF_SIZE - 1);
    }

    uint16_t BufferedSerial::availableForWrite() const {
        uint16_t occupied = (_tx_head - _tx_tail) & (TX_BUF_SIZE - 1);
        return (TX_BUF_SIZE - 1) - occupied;
    }

    bool BufferedSerial::rx_empty() const { return _rx_head == _rx_tail; }
    bool BufferedSerial::tx_empty() const { return _tx_head == _tx_tail; }
    bool BufferedSerial::is_output_tty() const { return HAL::serial_is_output_tty(_serial); }

    // Application I/O
    // ------------------------------------------------------------------------

    uint16_t BufferedSerial::write(const char* buf, uint16_t len) {
        uint16_t pos = 0;
        while (pos < len) {
            uint16_t next_head = (_tx_head + 1) & (TX_BUF_SIZE - 1);
            // full: count the unwritten bytes (saturating), yield
            if (next_head == _tx_tail) {
                uint32_t drop = (uint32_t)(len - pos);
                _tx_dropped = (_tx_dropped > UINT32_MAX - drop) ? UINT32_MAX : _tx_dropped + drop;
                return pos;
            }
            _tx_buf[_tx_head] = buf[pos++];
            // no barrier: single-thread ring, see serial.h
            _tx_head = next_head;
        }
        return pos;
    }

    void BufferedSerial::tx_discard() {
        // Producer-side reset: snap our head back to the consumer's tail so the
        // ring reads empty. No barrier: producer and consumer are the same
        // thread (serial.h).
        _tx_head = _tx_tail;
    }

    uint16_t BufferedSerial::read(char* buf, uint16_t len) {
        uint16_t pos = 0;
        while (pos < len && _rx_head != _rx_tail) {
            // no barrier: single-thread ring, see serial.h
            buf[pos++] = _rx_buf[_rx_tail];
            _rx_tail = (_rx_tail + 1) & (RX_BUF_SIZE - 1);
        }
        return pos;
    }

    bool BufferedSerial::peek(uint8_t* b) const {
        if (_rx_head == _rx_tail) return false;
        *b = _rx_buf[_rx_tail];
        return true;
    }

    // Hardware I/O
    // ------------------------------------------------------------------------

    uint16_t BufferedSerial::receive() {
        uint16_t rcvd = 0;
        if (!_serial) return rcvd;
        while (true) {
            uint16_t head = (_rx_head + 1) & (RX_BUF_SIZE - 1);
            // Full: yield *before* consuming a device byte we can't store. Flag
            // it - the device FIFO may now overflow unseen (saturating, one per
            // stalled cycle).
            if (head == _rx_tail) {
                if (_rx_overrun != UINT32_MAX) _rx_overrun++;
                return rcvd;
            }
            int byte = _serial->read();
            if (byte == -1) return rcvd;
            _rx_buf[_rx_head] = static_cast<char>(byte);
            _rx_head = head;   // no barrier: single-thread ring, see serial.h
            rcvd++;
        }
    }

    uint16_t BufferedSerial::transmit() {
        uint16_t sent = 0;
        if (!_serial) return sent;
        while (_tx_head != _tx_tail && _serial->availableForWrite() > 0) {
            if (_serial->write(static_cast<uint8_t>(_tx_buf[_tx_tail])) == 0) break;  // full: yield
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