#include <cstdint>
#include <cstring>
#include <algorithm>

#include "libbotanik/plcio/logs.h"

namespace Botanik { namespace PlcIO {

    namespace {
        using std::min;
        using std::max;
    }

    void PlcLogReader::_detect_dropped_entries(const LogEntry_t* entry) {
        if (_last_seq == 0) { _last_seq = entry->seqNum; return; }
        uint16_t expected = (uint16_t)(_last_seq + 1);
        _last_seq = entry->seqNum;
        if (entry->seqNum == expected) return;
        _dropped += (uint16_t)(entry->seqNum - expected);
    }

    void PlcLogReader::_push_plc_entry(const LogEntry_t* plcEntry) {
        LogEntry_t entry;
        memcpy(&entry, plcEntry, sizeof(LogEntry_t));
        entry.length = min(entry.length, (uint16_t)(LOG_PLC_MESSAGE_LEN-1));
        entry.message[entry.length] = '\0';
        _logger->push_entry(entry);
    }

    PlcLogReader::PlcLogReader(PlcSyncHandler* handler, Logger* logger, uint32_t* timestamp_ms) : 
    _logger(logger), _handler(handler), _timestamp_ms(timestamp_ms) {
        _logBuffer = handler->output()->logBuffer;
        _writeIndex = &handler->output()->writeIndex;
        _readIndex = &handler->input()->readIndex;
    }

    void PlcLogReader::drain() {
        while (*_writeIndex != *_readIndex) {
            uint32_t readIndex = *_readIndex % LOG_PLC_BUFFER_SIZE;
            const LogEntry_t* entry = &_logBuffer[readIndex];
            _detect_dropped_entries(entry);
            _push_plc_entry(entry);
            *_readIndex = (*_readIndex + 1) % LOG_PLC_BUFFER_SIZE;
            _handler->write();
        }

    };
}}
