// REVIEW_35: TODO
/**
 * @file mqtt_log.cpp
 * @brief MqttLogWriter - publish log entries to the MQTT broker.
 */
#include "libbotanik/network/mqtt_log.h"

#include <algorithm>   // std::min
#include <cstdio>      // snprintf
#include <cstring>     // strlen

#include "libbotanik/logs/format.h"   // format_log_level_name / _topic_name
#include "libbotanik/utils/time.h"    // absolute_ms / format_ms_timestamp

namespace Botanik { namespace Network {

    MqttLogWriter::MqttLogWriter(Logs::LogLevel min_level)
        : _min_level(min_level) {}

    void MqttLogWriter::push(const LogEntry_t* entry) {
        // sink-local level gate (criticals always pass: LOG_CRITICAL is the max)
        if (entry->level < _min_level) return;
        // best-effort: drop when the link is down, and bail on reentrancy - a log
        // emitted from inside publish() would otherwise re-enter push().
        if (_mqtt == nullptr || !_mqtt->connected() || _busy) return;

        _busy = true;
        char topic[96];
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

    size_t MqttLogWriter::format(const LogEntry_t* entry, char* buf, size_t len) {
        if (len == 0) return 0;
        uint64_t ts = Utils::absolute_ms(entry->timestamp);
        char time_buf[Utils::TIMESTAMP_LEN];
        Utils::format_ms_timestamp(ts, time_buf, sizeof(time_buf));
        int written = snprintf(buf, len, "%s %s", time_buf, entry->message);
        if (written < 0) return 0;
        return std::min((size_t)written, len - 1);
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
