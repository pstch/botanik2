#ifndef LOGS_ENTRY_H
#define LOGS_ENTRY_H

/**
 * @file entry.h
 * @brief LogEntry - the one log record the whole firmware passes around.
 *
 * Plain POD, sized by Config::LOG_MESSAGE_LEN; no wire contract to honour.
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
