// REVIEW_35: TODO
#ifndef NETWORK_MQTT_LOG_H
#define NETWORK_MQTT_LOG_H

/**
 * @file mqtt_log.h
 * @brief MqttLogWriter - a LogWriter that publishes each entry to the MQTT broker.
 *
 * Each entry is published QoS-0 under
 * "<MQTT_TOPIC_PREFIX>/<MQTT_LOG_TOPIC>/<level>/<topic>"
 * (e.g. ".../logs/debug/modbus") with a "<timestamp> <message>" payload.
 * Publish-only, best-effort: an entry that can't go out now (link down, broker
 * gone) is dropped, not queued - MQTT already owns the socket TX, so a second
 * queue here would be redundant. A sink-local min level keeps low-severity
 * chatter off the broker.
 */

#include <cstddef>                      // size_t

#include "libbotanik/config/identity.h"   // Config::SITE_NAME / FIRMWARE_VERSION / MQTT_TOPIC_PREFIX
#include "libbotanik/config/net.h"        // Config::NET_* / DNS_* / NTP_* / MQTT_*

#include "libbotanik/config/logging.h"    // Config::LOG_MESSAGE_LEN
#include "libbotanik/logs/topics.h"     // Logs::LogLevel / LogTopic
#include "libbotanik/logs/writer.h"     // Logs::LogWriter
#include "libbotanik/network/mqtt.h"    // Network::MqttClient

namespace Botanik { namespace Network {

    // This writer is what puts a log line on an MQTT topic, so it is where the
    // two bounds have to agree. Asserted here rather than in either config
    // header: neither concern owns the constraint on its own.
    static_assert(Config::MQTT_TX_BUF >=
                      Config::MQTT_TOPIC_LEN + Config::LOG_MESSAGE_LEN + 32,
                  "MQTT_TX_BUF cannot carry a full log line under the longest topic");

    /** @brief LogWriter that publishes entries to the MQTT broker (see file
     *  header). Bind the transport with set_publisher() once the client exists
     *  (firmware setup); a null transport drops everything. */
    class MqttLogWriter : public Logs::LogWriter {
    private:

        /** @name State
         *  @{ */
        MqttClient*    _mqtt = nullptr;   ///< transport (null = drop)
        bool           _busy  = false;    ///< reentrancy guard (publish -> log -> push)
        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief @p min_level seeds the base's sink-local floor (criticals
         *  always pass); defaults from Config::MQTT_LOG_MIN_LEVEL. Adjust it
         *  later with LogWriter::set_min_level(). */
        explicit MqttLogWriter(Logs::LogLevel min_level =
                                   (Logs::LogLevel)Config::MQTT_LOG_MIN_LEVEL);
        /** @brief Bind the transport (called once at firmware setup). */
        void set_publisher(MqttClient* mqtt) { _mqtt = mqtt; }

        /** @} */

        /** @name Queue interface
         *  @{ */

        /** @brief Filter, format, and publish @p entry now (best-effort). */
        void push(const Logs::LogEntry* entry) override;
        /** @brief No-op: nothing is buffered. */
        void drain() override {}

        /** @} */

    protected:
        /** @name Channel implementation
         *  @{ */

        /** @brief Format the payload ("<timestamp> <message>") into @p buf;
         *  returns the bytes written (excluding the terminator). */
        size_t format(const Logs::LogEntry* entry, char* buf, size_t len) override;
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
