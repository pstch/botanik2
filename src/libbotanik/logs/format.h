#ifndef LOGS_FORMAT_H
#define LOGS_FORMAT_H

#include <cstdint>
#include "libbotanik/plcio/vars.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Logs {

    // Format the log level
    const char* format_log_level(PlcIO::LogLevel level) { switch (level) {
        case PlcIO::LOG_DEBUG:      return "[~]";
        case PlcIO::LOG_INFO:       return "[-]";
        case PlcIO::LOG_NOTICE:     return "[+]";
        case PlcIO::LOG_WARNING:    return "[!]";
        case PlcIO::LOG_ERROR:      return "[§]";
        case PlcIO::LOG_CRITICAL:   return "[@]";
        default:                    return "";
    }; }

    // Format the log topic
    const char* format_log_topic(PlcIO::LogTopic topic) { switch (topic) {
        case PlcIO::NONE:       return "";
        case PlcIO::BOOT:       return "BOOT:";
        case PlcIO::NETWORK:    return "NETWORK:";
        case PlcIO::TIME:       return "TIME:";
        case PlcIO::STATUS:     return "STATUS:";
        case PlcIO::PLC_IO:     return "SHARED_IO";
        default:                return "";
    }; }

    // Format a log entry
    size_t format_log_entry(const PlcIO::LogEntry_t* entry, char* buf, size_t len) {
        using namespace Botanik::Utils;
        const char* level = format_log_level(entry->level);
        const char* topic = format_log_topic(entry->topic);
        std::array<char, 22> time_buf;
        format_ms_timestamp(absolute_ms(entry->timestamp), &time_buf);
        int written = snprintf(buf, len, "%s %s %s %s", time_buf.data(), level, topic, entry->message);
        return std::min((size_t)written, len);
    };

}}

#endif // LOGS_FORMAT_H