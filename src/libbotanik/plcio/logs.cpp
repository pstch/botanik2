/**
 * @file logs.cpp
 * @brief PlcLogReader implementation - sequence-gap (dropped-entry) detection
 *        and forwarding of PLC log entries into the local Logger.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>

#include "libbotanik/plcio/logs.h"

namespace Botanik { namespace PlcIO {

    // Lifecycle
    // ------------------------------------------------------------------------

    PlcLogReader::PlcLogReader(PlcSyncContext* ctx, Logger* logger)
        : _logger(logger), _ctx(ctx)
    {
        _logBuffer  = ctx->output_logs()->logBuffer;
        _writeIndex = &ctx->output_logs()->writeIndex;
        _readIndex  = &ctx->input_logs()->readIndex;
    }

    void PlcLogReader::drain() {
        while (*_writeIndex != *_readIndex) {
            uint32_t readIndex = *_readIndex % Config::LOG_PLC_BUFFER_SIZE;
            const LogEntry_t* entry = &_logBuffer[readIndex];
            _detect_dropped_entries(entry);
            _push_plc_entry(entry);
            *_readIndex = (*_readIndex + 1) % Config::LOG_PLC_BUFFER_SIZE;
            _ctx->write();
        }
    }

    void PlcLogReader::step() {
        drain();
    }

    // Internals
    // ------------------------------------------------------------------------

    void PlcLogReader::_detect_dropped_entries(const LogEntry_t* entry) {
        if (_last_seq == 0) { _last_seq = entry->seqNum; return; }
        uint32_t expected = _last_seq + 1;
        if (entry->seqNum != expected) {
            uint32_t dropped = entry->seqNum - expected;
            _dropped += dropped;
            LogEntry_t warn = {};
            warn.timestamp = entry->timestamp;
            warn.level     = LOG_WARNING;
            warn.topic     = PLC_IO;
            warn.length    = (uint16_t)snprintf(warn.message, sizeof(warn.message),
                "%u PLC log entries dropped (seq %u..%u)",
                (unsigned)dropped, (unsigned)expected, (unsigned)(entry->seqNum - 1));
            _logger->push_entry(warn);
        }
        _last_seq = entry->seqNum;
    }

    void PlcLogReader::_push_plc_entry(const LogEntry_t* plcEntry) {
        LogEntry_t entry;
        memcpy(&entry, plcEntry, sizeof(LogEntry_t));
        entry.length = std::min(entry.length, (uint16_t)(Config::LOG_PLC_MESSAGE_LEN-1));
        entry.message[entry.length] = '\0';
        _logger->push_entry(entry);
    }
}}
