#include "libbotanik/utils/time.h"
#include "libbotanik/utils/serial.h"

#include "libbotanik/logs/writer.h"
#include "libbotanik/logs/format.h"


namespace Botanik { namespace Logs {

    namespace {
        using namespace Botanik::Utils;
    }

    // Attempt to drain a single message, return whether it was fully sent
    bool BufferedLogWriter::_drain_message() {
        _sent += write(&_message[_sent], _length - _sent);          // write remaining bytes, record actually written bytes
        if (_sent < _length) return false;                          // yield if not fully sent
        _length = 0; _sent = 0; return true;                        // otherwise reset the writer state
    }

    // Add a new entry to the writer's queue
    void BufferedLogWriter::push(const LogEntry_t* entry) {
        uint8_t nextIndex = (_writeIndex + 1) % LOG_QUEUE_SIZE;     // determine next write index
        if (nextIndex == _readIndex) { _dropped += 1; return; }     // drop message if queue is full
        memcpy(&_queue[_writeIndex], entry, sizeof(LogEntry_t));    // otherwise, store the log entry
        _writeIndex = nextIndex;                                    // and record the incremented write index
    }
    
    // Attempt to drain entries from the queue and print them
    void BufferedLogWriter::drain() {
        if (_length > 0) {                                          // check if we are already sending a message
            if (!_drain_message()) return;                          // drain the current message, yield if not complete
            _readIndex = (_readIndex + 1) % LOG_QUEUE_SIZE;         // increment the read index
        }
        while (_readIndex != _writeIndex) {                         // check for new entries in the queue,
            const LogEntry_t* entry = &_queue[_readIndex];          // get the next entry in the queue
            _length = format(entry, _message, LOG_FORMATTED_LEN);   // try to format the queue's next message
            if (_length <= 0) { _failed++; continue; }              // if failed, record failure and skip message
            if (!_drain_message()) return;                          // drain the formatted message, yield if not complete
            _readIndex = (_readIndex + 1) % LOG_QUEUE_SIZE;         // increment the read index
        }
    }

    // TODO
    size_t SerialLogWriter::format(const LogEntry_t* entry, char* buf, size_t len) {
        // format log level and topic
        const char* level = format_log_level(entry->level);
        const char* topic = format_log_topic(entry->topic);
        // format the timestamp
        std::array<char, 22> time_buf; 
        format_ms_timestamp(absolute_ms(entry->timestamp), &time_buf);
        // format the log entry, and return the length of the message
        int written = snprintf(buf, len, "%s %s %s", time_buf.data(), level, topic, entry->message);
        return min((size_t)written, len);
    };

    // TODO
    size_t SerialLogWriter::write(const char* buf, size_t len) {
        int available = max((uint16_t)len, _serial.available());
        if (available == 0) return 0;
        return _serial.write(buf, available);
    }

    // TODO
    SerialLogWriter::SerialLogWriter(BufferedSerial& serial) : 
        _serial(serial) {}

    // TODO
    DebugLogWriter::DebugLogWriter(BufferedSerial& serial): 
        SerialLogWriter(serial) {}

    // TODO
    MonitorLogWriter::MonitorLogWriter(BufferedSerial& serial): 
        SerialLogWriter(serial) {}

    // TODO
    void MonitorLogWriter::push(const LogEntry_t* entry) {
        if (entry->level != LOG_DEBUG) SerialLogWriter::push(entry);
    }

}}