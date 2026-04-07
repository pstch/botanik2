#ifndef LOGS_FORMAT_H
#define LOGS_FORMAT_H

#include <cstdint>
#include "libbotanik/plcio/vars.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Logs {

    namespace {
        using namespace Botanik::PlcIO;
        using namespace Botanik::Utils;
        using std::min;
        using std::max;
    }

    // Format the log level
    const char* format_log_level(LogLevel level) { switch (level) {
        case LOG_DEBUG:     return "[~]";
        case LOG_INFO:      return "[-]";
        case LOG_NOTICE:    return "[+]";
        case LOG_WARNING:   return "[!]";
        case LOG_ERROR:     return "[§]";
        case LOG_CRITICAL:  return "[@]";
    }; }

    // Format the log topic
    const char* format_log_topic(LogTopic topic) { switch (topic) {
        case NONE:      return "";
        case BOOT:      return "BOOT:";
        case NETWORK:   return "NETWORK:";
        case TIME:      return "TIME:";
        case STATUS:    return "STATUS:";
        case PLC_IO:    return "SHARED_IO";
    }; }

    // Format a log entry
    size_t format_log_entry(const LogEntry_t* entry, char* buf, size_t len) {
        const char* level = format_log_level(entry->level);
        const char* topic = format_log_topic(entry->topic);
        std::array<char, 22> time_buf; 
        format_ms_timestamp(absolute_ms(entry->timestamp), &time_buf);
        int written = snprintf(buf, len, "%s %s %s", time_buf.data(), level, topic, entry->message);
        return min((size_t)written, len);
    };

}}

#endif // LOGS_FORMAT_H