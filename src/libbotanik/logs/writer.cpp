/**
 * @file writer.cpp
 * @brief LogWriter implementations — queueing and incremental drain.
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
        if (nextIndex == _readIndex) { _dropped += 1; return; }             // drop message if queue is full
        memcpy(&_queue[_writeIndex], entry, sizeof(LogEntry_t));            // otherwise, store the log entry
        _writeIndex = nextIndex;                                            // and record the incremented write index
    }

    // Attempt to drain entries from the queue and print them
    void BufferedLogWriter::drain() {
        if (_length > 0) {                                                  // check if we are already sending a message
            if (!_drain_message()) return;                                  // drain the current message, yield if not complete
            _readIndex = (_readIndex + 1) % Config::LOG_QUEUE_SIZE;         // increment the read index
        }
        while (_readIndex != _writeIndex) {                                 // check for new entries in the queue,
            const LogEntry_t* entry = &_queue[_readIndex];                  // get the next entry in the queue
            _length = format(entry, _message, Config::LOG_FORMATTED_LEN);   // try to format the queue's next message
            if (_length == 0) { _failed++; _readIndex = (_readIndex + 1) % Config::LOG_QUEUE_SIZE; continue; } // if failed, skip
            if (!_drain_message()) return;                                  // drain the formatted message, yield if not complete
            _readIndex = (_readIndex + 1) % Config::LOG_QUEUE_SIZE;         // increment the read index
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
        size_t written = format_log_entry(entry, buf, len);
        if (written < len) buf[written++] = '\n';
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
