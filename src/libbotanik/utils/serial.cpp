/**
 * @file serial.cpp
 * @brief BufferedSerial implementation - pumps the RX/TX ring buffers to and
 *        from the underlying HAL::RawSerial without blocking the main loop.
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
        // while we still have bytes to write
        while (pos < len) {
            // get next head position, wrapping at TX_BUF_SIZE - 1
            uint16_t next_head = (_tx_head + 1) & (TX_BUF_SIZE - 1);
            // if the buffer is full, count the unwritten bytes and yield
            if (next_head == _tx_tail) { _tx_dropped += (uint32_t)(len - pos); return pos; }
            // otherwise, store the byte and increment pos
            _tx_buf[_tx_head] = buf[pos++];
            HAL::dsb();
            // update current TX head
            _tx_head = next_head;
        }
        return pos;
    }

    void BufferedSerial::tx_discard() {
        // Producer-side reset: snap our head back to the consumer's tail so the
        // ring reads empty. dsb() so we observe the latest tail before the store
        // and the store is visible before any follow-up write().
        HAL::dsb();
        _tx_head = _tx_tail;
        HAL::dsb();
    }

    uint16_t BufferedSerial::read(char* buf, uint16_t len) {
        uint16_t pos = 0;
        // while we haven't reached the RX head
        while (pos < len && _rx_head != _rx_tail) {
            // memory barrier
            HAL::dsb(); 
            // read the byte and increment pos
            buf[pos++] = _rx_buf[_rx_tail];
            // update current RX tail
            _rx_tail = (_rx_tail + 1) & (RX_BUF_SIZE - 1);
        }
        return pos;
    }

    bool BufferedSerial::peek(uint8_t* b) const {
        // if buffer empty, return false early
        if (_rx_head == _rx_tail) return false;
        // memory barrier
        HAL::dsb();
        // read the byte at the RX tail and return true
        *b = _rx_buf[_rx_tail];
        return true;
    }

    // Hardware I/O
    // ------------------------------------------------------------------------

    uint16_t BufferedSerial::receive() {
        uint16_t rcvd = 0;
        if (!_serial) return rcvd;
        while (true) {
            // get the next RX head
            uint16_t head = (_rx_head + 1) & (RX_BUF_SIZE - 1);
            // if the buffer is full, yield to the caller *before* consuming a byte
            // from the device, so we never drop one we can't store
            if (head == _rx_tail) return rcvd;
            // pull the next byte from the serial device; stop if there is none
            int byte = _serial->read();
            if (byte == -1) return rcvd;
            // store the read byte in the buffer
            _rx_buf[_rx_head] = static_cast<char>(byte);
            // memory barrier
            HAL::dsb();
            // update the current RX head
            _rx_head = head;
            // increment the received bytes count
            rcvd++;
        }
    }

    uint16_t BufferedSerial::transmit() {
        uint16_t sent = 0;
        if (!_serial) return sent;
        // while there is space in the serial device's TX buffer
        while (_tx_head != _tx_tail && _serial->availableForWrite() > 0) {
            // memory barrier
            HAL::dsb();
            // try to write the byte to the serial device, if it's full, yield to caller
            if (_serial->write(static_cast<uint8_t>(_tx_buf[_tx_tail])) == 0) break;
            // update the current TX tail
            _tx_tail = (_tx_tail + 1) & (TX_BUF_SIZE - 1);
            // increment the sent bytes count
            sent++;
        }
        return sent;
    }

    void BufferedSerial::step() {
        receive();
        transmit();
    }

}}