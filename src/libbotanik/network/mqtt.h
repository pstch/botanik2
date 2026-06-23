#ifndef NETWORK_MQTT_H
#define NETWORK_MQTT_H

/**
 * @file mqtt.h
 * @brief MqttClient - minimal non-blocking MQTT 3.1.1 publisher over TCP.
 *
 * Config-driven (ConfigHandler, the `mqtt` block): connects, keeps alive with PINGREQ,
 * publishes QoS-0; restarts on a config change, backs off / reconnects on failure. The
 * broker name resolves through the shared NameResolver; the link must be up
 * (NetworkHandler::is_up()) before a connect.
 *
 * Publish-only (carries notifications outward, e.g. alarm transitions). Client id is
 * Config::SITE_NAME.
 */

#include <cstdint>

#include "libbotanik/config_handler.h"
#include "libbotanik/hal/net.h"
#include "libbotanik/network/network.h"
#include "libbotanik/network/resolver.h"
#include "libbotanik/alarms/publisher.h"   // Alarms::Publisher (alarm transport)

namespace Botanik { namespace Network {

    /** @brief Non-blocking MQTT 3.1.1 publisher. Also serves as the alarm transport
     *  (Alarms::Publisher), so HealthMonitor's notifier publishes through it. */
    class MqttClient : public ConfigHandler, public Alarms::Publisher {
        /** @name State
         *  @{ */
        enum State : uint8_t {
            MQTT_IDLE = 0,    ///< disabled / no config
            MQTT_RESOLVE,     ///< resolving the broker name through the resolver
            MQTT_CONNECT,     ///< non-blocking TCP connect in progress
            MQTT_HANDSHAKE,   ///< CONNECT sent; awaiting CONNACK
            MQTT_READY,       ///< connected; keepalive + publishes
            MQTT_ERROR,       ///< failed; backing off before a retry
        };
        State                   _state;
        uint64_t                _since_ms;       ///< when the current state was entered (timeouts)
        uint64_t                _last_ping_ms;   ///< last keepalive PINGREQ
        NameResolver*           _resolver;       ///< DNS resolver
        ResolvedAddr            _broker_addr;    ///< resolved broker address
        const NetworkHandler*   _net;            ///< link gate (null = always blocked)
        HAL::Net::TcpSocket     _sock;           ///< open TCP socket (-1 = none)
        /** @} */

    public:
        MqttClient(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr,
                   NameResolver* resolver = nullptr, const NetworkHandler* net = nullptr);

        /** @brief True when connected and ready to publish. */
        bool connected() const { return _state == MQTT_READY; }
        /** @brief Publish a QoS-0 message now if connected; returns false otherwise (the
         *  caller may drop or retry later). @p retain sets the MQTT retain flag. */
        bool publish(const char* topic, const char* payload, bool retain);
        /** @brief Alarms::Publisher: publish non-retained (QoS 0). */
        bool publish(const char* topic, const char* payload) override {
            return publish(topic, payload, false);
        }

    protected:
        uint32_t config_serial() const override;
        void on_config_changed(uint64_t now_ms) override;
        void on_step(uint64_t now_ms) override;

    private:
        /** @name Internals
         *  @{ */
        void _enter(State s, uint64_t now_ms);
        void _fail(uint64_t now_ms);     ///< close the socket and back off (-> ERROR)
        bool _tcp_begin();               ///< open + start a non-blocking TCP connect
        int  _tcp_poll();                ///< 1 connected, 0 waiting, -1 failed
        bool _send_connect();            ///< send the MQTT CONNECT packet
        int  _poll_connack();            ///< 1 accepted, 0 waiting, -1 failed
        void _service(uint64_t now_ms);  ///< READY: keepalive ping + drain/health-check
        /** @} */
    };

}}

#endif // NETWORK_MQTT_H
