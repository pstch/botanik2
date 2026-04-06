#include <cstdint>
#include <cstring>
#include <algorithm>

#include "libbotanix/plcio/logs.h"

using std::min;
using std::max;

namespace Botanix { namespace PlcIO {

    // handle sequence number to detect dropped PLC log entries
    void PlcLogReader::_detect_dropped_entries(const LogEntry_t* entry) {
        uint32_t _next = _last_seq + 1;
        if (entry->seqNum != _next) _dropped += max((unsigned int)0, entry->seqNum - _next);
        _last_seq = entry->seqNum;
    }

    // build an actual log entry, and push it to the logger instance
    void PlcLogReader::_push_plc_entry(const LogEntry_t* plcEntry) {
        LogEntry_t entry;
        memcpy(&entry, plcEntry, sizeof(entry.message));
        entry.length = (uint8_t)min(entry.length, (uint16_t)(LOG_PLC_MESSAGE_LEN-1));
        entry.message[entry.length] = '\0';
        _logger->push_entry(entry);
    }

    // construct a new PlcLogReader instance
    PlcLogReader::PlcLogReader(PlcSyncHandler* handler, Logger* logger, uint32_t* timestamp_ms) : 
    _logger(logger), _handler(handler), _timestamp_ms(timestamp_ms) {
        _logBuffer = handler->output()->logBuffer;
        _writeIndex = &handler->output()->writeIndex;
        _readIndex = &handler->input()->readIndex;
    }

    // drain the PLC log buffer
    void PlcLogReader::drain() {
        while (*_writeIndex != *_readIndex) {
            const LogEntry_t* entry = &_logBuffer[min(*_readIndex, (uint32_t)LOG_PLC_BUFFER_SIZE)];
            _detect_dropped_entries(entry);
            _push_plc_entry(entry);
            *_readIndex = (*_readIndex + 1) % LOG_PLC_BUFFER_SIZE;
            _handler->write();
        }

    };
}}
