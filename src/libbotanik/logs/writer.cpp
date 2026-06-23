/**
 * @file writer.cpp
 * @brief LogWriter implementations - queueing and incremental drain.
 *
 * drain() formats one queued entry at a time into _message and tracks _sent so
 * a partial write resumes without re-formatting. SerialLogWriter caps each
 * write at the TX buffer's available space so it never blocks the main loop.
 */
#include "libbotanik/utils/time.h"
#include "libbotanik/utils/serial.h"

#include "libbotanik/logs/writer.h"
#include "libbotanik/logs/format.h"


namespace Botanik { namespace Logs {

    // Queue interface
    // ------------------------------------------------------------------------

    void BufferedLogWriter::push(const LogEntry_t* entry) {
        uint8_t nextIndex = (_writeIndex + 1) % Config::LOG_QUEUE_SIZE;
        bool    crit      = entry->level >= PlcIO::LOG_CRITICAL;
        if (nextIndex == _readIndex) {   // completely full
            // Critical reserve exhausted: a normal drops; a critical evicts the
            // oldest as a last resort. drain() consumes _readIndex at format
            // time, so advancing it can't corrupt an in-flight send.
            if (!crit) { _dropped += 1; return; }
            // drop oldest
            _readIndex = (_readIndex + 1) % Config::LOG_QUEUE_SIZE;
            _dropped += 1;
        } else if (!crit) {
            // Reserve the last K usable slots for criticals, so a critical
            // (e.g. the sink-loss report itself) always seats WITHOUT evicting
            // - which stops that report from manufacturing the very drop it
            // then reports.
            uint8_t used = (uint8_t)((_writeIndex - _readIndex + Config::LOG_QUEUE_SIZE)
                                     % Config::LOG_QUEUE_SIZE);
            if (used >= (uint8_t)(Config::LOG_QUEUE_SIZE - 1
                                  - Config::LOG_QUEUE_CRITICAL_RESERVE)) {
                _dropped += 1; return;
            }
        }
        memcpy(&_queue[_writeIndex], entry, sizeof(LogEntry_t));
        _writeIndex = nextIndex;
    }

    void BufferedLogWriter::drain() {
        // Resume an in-flight message first; yield if it is still partial.
        if (_length > 0 && !_drain_message()) return;
        while (_readIndex != _writeIndex) {
            const LogEntry_t* entry = &_queue[_readIndex];
            _length = format(entry, _message, Config::LOG_FORMATTED_LEN);
            // Consume now: _message is decoupled from the queue slot.
            _readIndex = (_readIndex + 1) % Config::LOG_QUEUE_SIZE;
            if (_length == 0) { _failed++; continue; }   // unformattable: skip
            // yield if not fully sent
            if (!_drain_message()) return;
        }
    }

    // Internals
    // ------------------------------------------------------------------------

    // Drain one message; return whether it was fully sent.
    bool BufferedLogWriter::_drain_message() {
        _sent += write(&_message[_sent], _length - _sent);
        if (_sent < _length) return false;
        _length = 0; _sent = 0; return true;
    }

    // Lifecycle
    // ------------------------------------------------------------------------

    SerialLogWriter::SerialLogWriter(BufferedSerial& serial) :
        _serial(&serial) {}

    // Drain into the serial's TX ring; the serial is flushed separately by the
    // main loop.
    void SerialLogWriter::step() {
        drain();
    }

    // Channel implementation
    // ------------------------------------------------------------------------

    size_t SerialLogWriter::format(const LogEntry_t* entry, char* buf, size_t len) {
        // The shared formatter emits no terminator; on a serial sink,
        // newline-terminate each entry so they don't run together. Format into
        // len-1 to always leave room.
        if (len == 0) return 0;
        size_t written = format_log_entry(entry, buf, len - 1);
        buf[written++] = '\n';
        return written;
    }

    // Write up to len bytes, capped by available TX space.
    size_t SerialLogWriter::write(const char* buf, size_t len) {
        int available = std::min((uint16_t)len, _serial->availableForWrite());
        if (available == 0) return 0;
        return _serial->write(buf, available);
    }

    // MonitorLogWriter
    // ------------------------------------------------------------------------

    // Suppress when the output is a tty: it shares the screen with the console
    // UI, so log entries would garble the display.
    MonitorLogWriter::MonitorLogWriter(BufferedSerial& serial) :
        SerialLogWriter(serial) {
            _suppressed = serial.is_output_tty();
        }

    void MonitorLogWriter::push(const LogEntry_t* entry) {
        if (_suppressed) return;
        SerialLogWriter::push(entry);
    }

}}
