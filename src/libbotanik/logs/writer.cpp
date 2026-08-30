// REVIEW_35: TODO
/**
 * @file writer.cpp
 * @brief LogWriter implementations - queueing and incremental drain.
 *
 * drain() formats one queued entry at a time into _message and tracks _sent so
 * a partial write resumes without re-formatting. SerialLogWriter caps each
 * write at the TX buffer's available space so it never blocks the main loop.
 */
#include "libbotanik/logs/writer.h"

#include <cstring>

#include "libbotanik/logs/format.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/utils/serial.h"


namespace Botanik { namespace Logs {

    // Queue interface

    void BufferedLogWriter::push(const Logs::LogEntry* entry) {
        // Sink-local floor, before the queue and before the drop counters: an
        // entry this sink was never meant to show is not a loss to report.
        if (entry->level < _min_level) return;
        uint8_t nextIndex = (_writeIndex + 1) % Config::LOG_QUEUE_SIZE;
        bool    crit      = entry->level >= Logs::LOG_CRITICAL;
        if (nextIndex == _readIndex) {   // completely full
            // drop non-critical messages
            if (!crit) { _dropped += 1; return; }
            // otherwise, drop oldest message
            _readIndex = (_readIndex + 1) % Config::LOG_QUEUE_SIZE;
            _dropped += 1;
        } else if (!crit) {
            // Reserve the last K usable slots for criticals, so a critical
            // (e.g. the sink-loss report itself) always seats WITHOUT evicting
            // - which stops that report from manufacturing the very drop it
            // then reports.
            if (_used() >= (uint8_t)(Config::LOG_QUEUE_SIZE - 1
                                     - Config::LOG_QUEUE_CRITICAL_RESERVE)) {
                _dropped += 1; return;
            }
        }
        memcpy(&_queue[_writeIndex], entry, sizeof(Logs::LogEntry));
        _writeIndex = nextIndex;
    }

    void BufferedLogWriter::drain() {
        // resume the current message first, yield if still partial
        if (_length > 0 && !_drain_message()) return;
        while (_readIndex != _writeIndex) {
            const Logs::LogEntry* entry = &_queue[_readIndex];
            _readIndex = (_readIndex + 1) % Config::LOG_QUEUE_SIZE;
            _length = format(entry, _message, Config::LOG_FORMATTED_LEN);
            if (_length == 0) { _failed++; continue; }   // unformattable
            if (!_drain_message()) return;               // yield, still partial
        }
    }

    // Internals

    // Drain one message; return whether it was fully sent.
    bool BufferedLogWriter::_drain_message() {
        _sent += write(&_message[_sent], _length - _sent);
        if (_sent < _length) return false;
        _length = 0; _sent = 0; return true;
    }

    // Occupied slots between the read and write cursors (0..LOG_QUEUE_SIZE-1).
    uint8_t BufferedLogWriter::_used() const {
        return (uint8_t)((_writeIndex - _readIndex + Config::LOG_QUEUE_SIZE)
                         % Config::LOG_QUEUE_SIZE);
    }

    // Lifecycle

    SerialLogWriter::SerialLogWriter(const SetupContext& ctx, BufferedSerial& serial,
                                     bool drop_if_tty, const char* name,
                                     const char* sink, const char* level_param) :
        BufferedLogWriter(sink != nullptr ? sink : name, level_param),
        Component(ctx, name),
        _serial(&serial), _suppressed(drop_if_tty && serial.is_output_tty()) {}

    void SerialLogWriter::push(const Logs::LogEntry* entry) {
        if (_suppressed) return;
        BufferedLogWriter::push(entry);
    }

    // Drain into the serial's TX ring; the serial is flushed separately by the
    // main loop.
    void SerialLogWriter::on_present(uint64_t now_ms) {
        (void)now_ms;
        // Bytes in first, as they were when this was monitor_rx at the top of
        // the beat: nothing reads them, the read only keeps the device's FIFO
        // moving. Then the queue into the ring, then the ring onto the wire.
        _serial->receive();
        drain();
        _serial->transmit();
    }


    void SerialLogWriter::register_tasks() {
        _tasks->add(*this, name(), Tasks::EVERY_BEAT,
              Tasks::PHASE_PRESENT);
    }

    // Channel implementation

    size_t BufferedLogWriter::format(const Logs::LogEntry* entry, char* buf, size_t len) {
        return format_log_entry(entry, buf, len);
    }

    size_t SerialLogWriter::format(const Logs::LogEntry* entry, char* buf, size_t len) {
        // The shared formatter emits no terminator; newline-terminate each entry
        // so they do not run together. Format into len-1 to leave the room.
        if (len == 0) return 0;
        size_t written = format_log_entry(entry, buf, len - 1);
        // A '\n' inside the message is the console's fold marker and would end
        // the record early here (@see Logs::fold_newlines).
        written = fold_newlines(buf, written, len - 1);
        buf[written++] = '\n';
        return written;
    }

    // Write up to len bytes, capped by available TX space.
    size_t SerialLogWriter::write(const char* buf, size_t len) {
        int available = std::min((uint16_t)len, _serial->availableForWrite());
        if (available == 0) return 0;
        return _serial->write(buf, available);
    }

}}
