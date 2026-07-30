// REVIEW_35: TODO
#ifndef NETWORK_MQTT_LOG_H
#define NETWORK_MQTT_LOG_H

/**
 * @file mqtt_log.h
 * @brief MqttLogWriter - a LogWriter that publishes each entry to the MQTT broker.
 *
 * Each entry is published QoS-0 under
 * "<MQTT_TOPIC_PREFIX>/<MQTT_LOG_TOPIC>/<level>/<topic>"
 * (e.g. ".../logs/debug/shared_io") with a "<timestamp> <message>" payload.
 * Publish-only, best-effort: an entry that can't go out now (link down, broker
 * gone) is dropped, not queued - MQTT already owns the socket TX, so a second
 * queue here would be redundant. A sink-local min level keeps low-severity
 * chatter off the broker.
 */

#include <cstddef>

#include "../../libbotanik_config.h"
#include "../logs/writer.h"    // LogWriter
#include "../logs/topics.h"    // LogLevel / LogTopic
#include "mqtt.h"   // MqttClient

namespace Botanik { namespace Network {

    /** @brief LogWriter that publishes entries to the MQTT broker (see file
     *  header). Bind the transport with set_publisher() once the client exists
     *  (firmware setup); a null transport drops everything. */
    class MqttLogWriter : public Logs::LogWriter {
    private:
        using LogEntry_t = PlcIO::LogEntry_t;

        /** @name State
         *  @{ */
        MqttClient*    _mqtt = nullptr;   ///< transport (null = drop)
        Logs::LogLevel _min_level;        ///< sink-local level gate
        bool           _busy  = false;    ///< reentrancy guard (publish -> log -> push)
        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief @p min_level is the sink's own threshold (criticals always
         *  pass); defaults from Config::MQTT_LOG_MIN_LEVEL. */
        explicit MqttLogWriter(Logs::LogLevel min_level =
                                   (Logs::LogLevel)Config::MQTT_LOG_MIN_LEVEL);
        /** @brief Bind the transport (called once at firmware setup). */
        void set_publisher(MqttClient* mqtt) { _mqtt = mqtt; }
        /** @brief Adjust the sink-local level gate. */
        void set_min_level(Logs::LogLevel level) { _min_level = level; }

        /** @} */

        /** @name Queue interface
         *  @{ */

        /** @brief Filter, format, and publish @p entry now (best-effort). */
        void push(const LogEntry_t* entry) override;
        /** @brief No-op: nothing is buffered. */
        void drain() override {}

        /** @} */

    protected:
        /** @name Channel implementation
         *  @{ */

        /** @brief Format the payload ("<timestamp> <message>") into @p buf;
         *  returns the bytes written (excluding the terminator). */
        size_t format(const LogEntry_t* entry, char* buf, size_t len) override;
        /** @brief Unused: this sink is message-oriented (publishes in push()). */
        size_t write(const char* buf, size_t len) override { (void)buf; (void)len; return 0; }
        /** @brief Build "<MQTT_TOPIC_PREFIX>/<MQTT_LOG_TOPIC>/<level>/<topic>"
         *  into @p buf; returns the length, or 0 on truncation. */
        static size_t build_topic(char* buf, size_t len,
                                  Logs::LogLevel level, Logs::LogTopic topic);

        /** @} */
    };

}}

#endif // NETWORK_MQTT_LOG_H
