#ifndef LOGS_ENTRY_H
#define LOGS_ENTRY_H

/**
 * @file entry.h
 * @brief LogEntry - the one log record the whole firmware passes around.
 *
 * This used to live in plcio/vars.h as LogEntry_t, because it doubled as the
 * PLC log ring's wire format. That made a wire constant (the message width) an
 * ABI-critical macro every translation unit had to agree on, and made the
 * logging subsystem depend on the PLC header. With the PLC interface gone it is
 * simply the host's log record: a plain struct, sized by one Config value, with
 * no external contract to honour.
 */

#include <cstdint>

#include "../../libbotanik_config.h"
#include "topics.h"   // LogLevel / LogTopic

namespace Botanik { namespace Logs {

    /** @brief One log record: a small fixed header plus an inline message
     *  buffer. Kept a POD with an inline buffer (rather than a pointer) so
     *  entries can be copied into ring buffers without any allocation. */
    struct LogEntry {
        uint64_t  timestamp;    ///< uptime ms when the entry was raised
        LogLevel  level;
        LogTopic  topic;
        uint16_t  length;       ///< bytes used in message, excluding the NUL
        uint32_t  seqNum;       ///< monotonic; gaps mean entries were dropped
        char      message[Config::LOG_MESSAGE_LEN];
    };

}}  // namespace Botanik::Logs

#endif // LOGS_ENTRY_H
