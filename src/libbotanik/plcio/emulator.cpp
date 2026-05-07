#include <cstring>
#include <algorithm>

#include "libbotanik/plcio/emulator.h"

namespace Botanik { namespace PlcIO {

    PlcLogWriter::PlcLogWriter(PlcEmulatorBase* emulator, uint32_t* timestamp_ms)
        : _emulator(emulator), _timestamp_ms(timestamp_ms)
    {
        _logBuffer  = emulator->output_logs()->logBuffer;
        _writeIndex = &emulator->output_logs()->writeIndex;
        _readIndex  = &emulator->input_logs()->readIndex;
    }

    void PlcLogWriter::push_log(LogLevel level, LogTopic topic, const char* msg) {
        uint32_t writeIndex = *_writeIndex % Config::LOG_PLC_BUFFER_SIZE;
        LogEntry_t& entry = _logBuffer[writeIndex];

        entry.timestamp = *_timestamp_ms;
        entry.level     = level;
        entry.topic     = topic;
        entry.seqNum    = _next_seq++;

        uint16_t len = (uint16_t)std::min(
            std::strlen(msg), (size_t)(Config::LOG_PLC_MESSAGE_LEN - 1));
        std::memcpy(entry.message, msg, len);
        entry.message[len] = '\0';
        entry.length = len;

        *_writeIndex = (*_writeIndex + 1) % Config::LOG_PLC_BUFFER_SIZE;
        _emulator->write();
    }

}}
