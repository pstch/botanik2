// REVIEW_35: TODO
/**
 * @file mqtt_log.cpp
 * @brief MqttLogWriter - publish log entries to the MQTT broker.
 */
#include "libbotanik/network/mqtt_log.h"    // MqttLogWriter

#include <algorithm>                        // std::min
#include <cstdio>                           // snprintf

#include "libbotanik/logs/format.h"         // Logs::format_log_level_name / _topic_name
#include "libbotanik/utils/time.h"          // Utils::format_ms_timestamp

namespace Botanik { namespace Network {

    MqttLogWriter::MqttLogWriter(Logs::LogLevel min_level) {
        set_min_level(min_level);
    }

    void MqttLogWriter::push(const Logs::LogEntry* entry) {
        // sink-local level gate (criticals always pass: LOG_CRITICAL is the max)
        if (entry->level < _min_level) return;
        // best-effort: drop when the link is down, and bail on reentrancy - a log
        // emitted from inside publish() would otherwise re-enter push().
        if (_mqtt == nullptr || !_mqtt->connected() || _busy) return;

        _busy = true;
        char topic[Config::MQTT_TOPIC_LEN];
        size_t tn = build_topic(topic, sizeof(topic), entry->level, entry->topic);
        if (tn > 0) {
            // Keep the whole PUBLISH packet within MQTT_TX_BUF: fixed header (1) +
            // remaining-length (<=4) + 2-byte topic-length prefix + topic bytes.
            const size_t overhead = tn + 7;
            const size_t budget = (Config::MQTT_TX_BUF > overhead)
                                    ? (size_t)Config::MQTT_TX_BUF - overhead : 0;
            char payload[Config::MQTT_TX_BUF];
            size_t cap = std::min(sizeof(payload), budget);
            if (cap > 0) {
                format(entry, payload, cap);
                _mqtt->publish(topic, payload, false);
            }
        }
        _busy = false;
    }

    size_t MqttLogWriter::format(const Logs::LogEntry* entry, char* buf, size_t len) {
        if (len == 0) return 0;
        uint64_t ts = entry->timestamp;
        char time_buf[Utils::TIMESTAMP_LEN];
        Utils::format_ms_timestamp(ts, time_buf, sizeof(time_buf));
        // "<timestamp> <message>", truncated to fit. By hand rather than
        // snprintf's %s: truncation is intended here, and -Wformat-truncation
        // has no way to be told so.
        size_t n = 0;
        for (const char* p = time_buf; *p != 0 && n + 1 < len; ++p) buf[n++] = *p;
        if (n + 1 < len) buf[n++] = ' ';
        for (const char* p = entry->message; *p != 0 && n + 1 < len; ++p) buf[n++] = *p;
        // Same reason as the serial sink: a message's '\n' is the console's
        // fold marker, and a payload split across it is one record pretending
        // to be two. Same helper, so the two wire shapes cannot drift.
        n = Logs::fold_newlines(buf, n, len - 1);
        buf[n] = '\0';
        return n;
    }

    size_t MqttLogWriter::build_topic(char* buf, size_t len,
                                      Logs::LogLevel level, Logs::LogTopic topic) {
        int n = snprintf(buf, len, "%s/%s/%s/%s",
                         Config::MQTT_TOPIC_PREFIX, Config::MQTT_LOG_TOPIC,
                         Logs::format_log_level_name(level),
                         Logs::format_log_topic_name(topic));
        if (n < 0 || (size_t)n >= len) return 0;   // encode error / truncated
        return (size_t)n;
    }

}}
