// REVIEW_35: DONE
/**
 * @file serial.cpp
 * @brief BufferedSerial implementation - pumps the RX/TX rings to and from
 *        HAL::Serial::Raw without blocking the main loop.
 */
#include "libbotanik/utils/serial.h"    // BufferedSerial

#include <cstdint>                      // UINT32_MAX and the fixed-width ring counters

#include "libbotanik/config/serial.h"     // Config::SERIAL_* / *_BAUD

namespace Botanik { namespace Utils {

    // Lifecycle
    // ------------------------------------------------------------------------

    BufferedSerial::BufferedSerial(HAL::Serial::Raw* serial, Logs::Logger* logger,
                                   const char* name) :
        Component(logger, name), _serial(serial) {
        memset(_rx_buf, 0, sizeof(_rx_buf));
        memset(_tx_buf, 0, sizeof(_tx_buf));
    }

    bool BufferedSerial::is_output_tty() const { return HAL::Serial::is_output_tty(_serial); }

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

    // Application I/O
    // ------------------------------------------------------------------------

    uint16_t BufferedSerial::write(const char* buf, uint16_t len) {
        uint16_t pos = 0;
        while (pos < len) {
            uint16_t next_head = (_tx_head + 1) & (TX_BUF_SIZE - 1);
            // full: count the unwritten bytes (saturating) then yield
            if (next_head == _tx_tail) {
                uint32_t drop = (uint32_t)(len - pos);
                _tx_dropped = (_tx_dropped > UINT32_MAX - drop) ? UINT32_MAX : _tx_dropped + drop;
                return pos;
            }
            _tx_buf[_tx_head] = buf[pos++];
            _tx_head = next_head;
        }
        return pos;
    }

    void BufferedSerial::tx_discard() {
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
            // RX buffer is FULL: yield *before* consuming a device byte we can't store.
            if (head == _rx_tail) {
                if (_rx_overrun != UINT32_MAX) _rx_overrun++;
                return rcvd;
            }
            int byte = _serial->read();
            if (byte == -1) return rcvd;
            _rx_buf[_rx_head] = static_cast<char>(byte);
            _rx_head = head;
            rcvd++;
        }
    }

    uint16_t BufferedSerial::transmit() {
        uint16_t sent = 0;
        if (!_serial) return sent;
        // Bounded per activation. A UART bounds itself - availableForWrite()
        // reflects a real FIFO, so the loop stops when the hardware is full -
        // but USB CDC with no host attached answers 1 forever, and this drained
        // the whole 4095-byte ring in one beat. With a host it is worse: the
        // send blocks per byte. So the cap is what makes the WORST case a
        // property of the code rather than of whatever is plugged in.
        // Whatever is left goes out next beat; the ring keeps it.
        const uint16_t cap = Config::SERIAL_TX_PER_STEP;
        while (_tx_head != _tx_tail && _serial->availableForWrite() > 0) {
            if (cap != 0 && sent >= cap) break;
            if (_serial->write(static_cast<uint8_t>(_tx_buf[_tx_tail])) == 0) break;  // full: yield
            _tx_tail = (_tx_tail + 1) & (TX_BUF_SIZE - 1);
            sent++;
        }
        return sent;
    }

    void BufferedSerial::on_step(uint64_t now_ms) {
        (void)now_ms;
        receive();
        transmit();
    }

}}