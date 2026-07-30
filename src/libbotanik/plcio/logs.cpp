// REVIEW_35: TODO
/**
 * @file logs.cpp
 * @brief PlcLogReader implementation - sequence-gap detection and forwarding.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>

#include "logs.h"

namespace Botanik { namespace PlcIO {

    // Lifecycle
    // ------------------------------------------------------------------------

    PlcLogReader::PlcLogReader(PlcSyncContext* ctx, Logger* logger)
        : Component(logger, "PlcLogReader"), _ctx(ctx)
    {
        _logBuffer  = ctx->output_logs()->logBuffer;
        _writeIndex = &ctx->output_logs()->writeIndex;
        _readIndex  = &ctx->input_logs()->readIndex;
    }

    void PlcLogReader::drain() {
        // Coherency: refresh the PLC's write cursor before checking it; bail
        // cheaply when there's nothing new so we don't invalidate the whole
        // ring every tick.
        HAL::Memory::cache_invalidate(_writeIndex, sizeof(*_writeIndex));
        if (HAL::Memory::ordered_read(_writeIndex) == *_readIndex) return;

        HAL::Memory::cache_invalidate(_ctx->output_logs(), sizeof(PlcOutputLogs_t));
        // Bound the work per tick to one full ring: that is the most the host
        // can legitimately be behind, so a runaway or corrupt writeIndex can't
        // spin here and burn the tick budget. Any real backlog is caught next
        // tick.
        uint32_t processed = 0;
        while (processed < Config::LOG_PLC_BUFFER_SIZE
               && HAL::Memory::ordered_read(_writeIndex) != *_readIndex) {
            uint32_t readIndex = *_readIndex % Config::LOG_PLC_BUFFER_SIZE;
            const Logs::LogEntry* entry = &_logBuffer[readIndex];
            _detect_dropped_entries(entry);
            _push_plc_entry(entry);
            *_readIndex = (*_readIndex + 1) % Config::LOG_PLC_BUFFER_SIZE;
            _ctx->write();
            processed++;
        }
        // publish readIndex
        HAL::Memory::cache_clean(_ctx->input_logs(), sizeof(PlcInputLogs_t));
    }

    void PlcLogReader::on_step(uint64_t now_ms) {
        (void)now_ms;
        drain();
    }

    // Internals
    // ------------------------------------------------------------------------

    void PlcLogReader::_detect_dropped_entries(const Logs::LogEntry* entry) {
        // First entry seen this session: anchor, don't treat as a gap. (A bool
        // flag, not _last_seq==0, so a legitimate seq 0 at the 2^32 wrap isn't
        // misread as "first".)
        if (!_have_seq) { _have_seq = true; _last_seq = entry->seqNum; return; }
        // modular: wraps with seqNum
        uint32_t expected = _last_seq + 1;
        if (entry->seqNum != expected) {
            // A gap larger than the ring means the producer lapped us; the
            // true count is unknowable, so clamp it - one corrupt/lapped seqNum
            // can't poison the stat.
            uint32_t gap     = entry->seqNum - expected;
            bool     lapped  = gap > Config::LOG_PLC_BUFFER_SIZE;
            uint32_t dropped = lapped ? Config::LOG_PLC_BUFFER_SIZE : gap;
            _dropped += dropped;
            Logs::LogEntry warn = {};
            warn.timestamp = entry->timestamp;
            warn.level     = Logs::LOG_WARNING;
            warn.topic     = Logs::PLC_IO;
            int n = snprintf(warn.message, sizeof(warn.message),
                "%s%u PLC log entries dropped (seq %u..%u)",
                lapped ? ">=" : "", (unsigned)dropped,
                (unsigned)expected, (unsigned)(entry->seqNum - 1));
            // snprintf returns the would-be length: clamp so a truncation never
            // records a length past the buffer.
            if (n < 0) n = 0;
            warn.length = (uint16_t)std::min(n, (int)sizeof(warn.message) - 1);
            _logger->push_entry(warn);
        }
        _last_seq = entry->seqNum;
    }

    void PlcLogReader::_push_plc_entry(const Logs::LogEntry* plcEntry) {
        Logs::LogEntry entry;
        memcpy(&entry, plcEntry, sizeof(Logs::LogEntry));
        entry.length = std::min(entry.length,
                                (uint16_t)(Config::LOG_MESSAGE_LEN - 1));
        entry.message[entry.length] = '\0';
        _logger->push_entry(entry);
    }
}}
