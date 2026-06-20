/**
 * @file writer.cpp
 * @brief LogWriter implementations - queueing and incremental drain.
 *
 * BufferedLogWriter holds a fixed ring buffer of entries; push() drops (and
 * counts) when full. drain() formats one queued entry at a time into _message
 * and writes it out, tracking _sent so a partial write resumes on the next
 * call without re-formatting. SerialLogWriter caps each write at the TX
 * buffer's available space so it never blocks the main loop.
 */
#include "libbotanik/utils/time.h"
#include "libbotanik/utils/serial.h"

#include "libbotanik/logs/writer.h"
#include "libbotanik/logs/format.h"


namespace Botanik { namespace Logs {

    // Queue interface
    // ------------------------------------------------------------------------

    // Add a new entry to the writer's queue
    void BufferedLogWriter::push(const LogEntry_t* entry) {
        uint8_t nextIndex = (_writeIndex + 1) % Config::LOG_QUEUE_SIZE;     // determine next write index
        bool    crit      = entry->level >= PlcIO::LOG_CRITICAL;
        if (nextIndex == _readIndex) {                                      // completely full
            // Even the critical reserve is exhausted. A normal drops; a critical evicts
            // the oldest as a last resort (rare). drain() consumes _readIndex at format
            // time, so advancing it can't corrupt an in-flight send.
            if (!crit) { _dropped += 1; return; }
            _readIndex = (_readIndex + 1) % Config::LOG_QUEUE_SIZE;         // drop oldest
            _dropped += 1;
        } else if (!crit) {
            // Reserve the last K usable slots for criticals: drop a normal that would
            // intrude on them, so a critical (e.g. the sink-loss report itself) can
            // always seat WITHOUT evicting - which stops that report from manufacturing
            // the very drop it then reports.
            uint8_t used = (uint8_t)((_writeIndex - _readIndex + Config::LOG_QUEUE_SIZE)
                                     % Config::LOG_QUEUE_SIZE);
            if (used >= (uint8_t)(Config::LOG_QUEUE_SIZE - 1 - Config::LOG_QUEUE_CRITICAL_RESERVE)) {
                _dropped += 1; return;
            }
        }
        memcpy(&_queue[_writeIndex], entry, sizeof(LogEntry_t));            // store the log entry
        _writeIndex = nextIndex;                                            // and record the incremented write index
    }

    // Attempt to drain entries from the queue and print them
    void BufferedLogWriter::drain() {
        if (_length > 0 && !_drain_message()) return;                       // resume an in-flight message; yield if still partial
        while (_readIndex != _writeIndex) {                                 // check for new entries in the queue,
            const LogEntry_t* entry = &_queue[_readIndex];                  // get the next entry in the queue
            _length = format(entry, _message, Config::LOG_FORMATTED_LEN);   // format it into _message (a copy)
            _readIndex = (_readIndex + 1) % Config::LOG_QUEUE_SIZE;         // consume now: _message is decoupled from the queue
            if (_length == 0) { _failed++; continue; }                      // unformattable: skip
            if (!_drain_message()) return;                                  // drain the formatted message, yield if not complete
        }
    }

    // Internals
    // ------------------------------------------------------------------------

    // Attempt to drain a single message, return whether it was fully sent
    bool BufferedLogWriter::_drain_message() {
        _sent += write(&_message[_sent], _length - _sent);                  // write remaining bytes, record actually written bytes
        if (_sent < _length) return false;                                  // yield if not fully sent
        _length = 0; _sent = 0; return true;                                // otherwise reset the writer state
    }

    // Lifecycle
    // ------------------------------------------------------------------------

    SerialLogWriter::SerialLogWriter(BufferedSerial& serial) :
        _serial(&serial) {}

    // Drain queued entries into the serial's TX ring. The bound serial is
    // stepped separately from the main loop, which flushes the ring to the sink.
    void SerialLogWriter::step() {
        drain();
    }

    // Channel implementation
    // ------------------------------------------------------------------------

    size_t SerialLogWriter::format(const LogEntry_t* entry, char* buf, size_t len) {
        // The shared formatter emits no line terminator (the page writers wrap by
        // width into stored rows). On a serial sink entries would otherwise run
        // together, so terminate each one with a newline when there's room.
        if (len == 0) return 0;
        // Format into len-1 so there is ALWAYS room for the newline: an exact-fill
        // line would otherwise run into the next one on the serial sink.
        size_t written = format_log_entry(entry, buf, len - 1);
        buf[written++] = '\n';
        return written;
    }

    // Write up to len bytes to the serial port, capped by available TX space
    size_t SerialLogWriter::write(const char* buf, size_t len) {
        int available = std::min((uint16_t)len, _serial->availableForWrite());
        if (available == 0) return 0;
        return _serial->write(buf, available);
    }

    // MonitorLogWriter
    // ------------------------------------------------------------------------

    // Decide suppression once: an interactive output shares the screen with the
    // console UI, so its log entries would garble the display.
    MonitorLogWriter::MonitorLogWriter(BufferedSerial& serial) :
        SerialLogWriter(serial) {
            _suppressed = serial.is_output_tty();
        }

    // Drop entries on a shared terminal before they reach the queue; otherwise
    // behave exactly like SerialLogWriter.
    void MonitorLogWriter::push(const LogEntry_t* entry) {
        if (_suppressed) return;
        SerialLogWriter::push(entry);
    }

}}
