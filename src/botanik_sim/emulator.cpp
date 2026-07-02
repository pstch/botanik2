/**
 * @file emulator.cpp
 * @brief PLC emulator implementation - the PLC-side log writer.
 */
#include <cstring>
#include <algorithm>

#include "botanik_sim/emulator.h"

namespace Botanik { namespace PlcIO {

    // Footprint contract for the emulator types. They live in the sim harness
    // now (not the library), so the size pins follow them here rather than
    // libbotanik_sizes.h. Sim-only build, so the host figure is the only one.
    namespace {
        struct ProbePlcIn  {};                    // empty payload (log fields only)
        struct ProbePlcOut { uint32_t _w[15]; };  // 60 B, 4-aligned
    }
    static_assert(sizeof(PlcEmulatorContext)                   == 8608,
                  "PlcEmulatorContext footprint changed");
    static_assert(sizeof(PlcEmulator<ProbePlcIn, ProbePlcOut>) == 8704,
                  "PlcEmulator footprint changed");

    // Lifecycle
    // ------------------------------------------------------------------------

    PlcLogWriter::PlcLogWriter(PlcEmulatorContext* ctx, uint32_t* timestamp_ms)
        : _ctx(ctx), _timestamp_ms(timestamp_ms)
    {
        _logBuffer  = ctx->output_logs()->logBuffer;
        _writeIndex = &ctx->output_logs()->writeIndex;
    }

    void PlcLogWriter::push_log(Logs::LogLevel level, Logs::LogTopic topic, const char* msg) {
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
        _ctx->write();
    }

}}
