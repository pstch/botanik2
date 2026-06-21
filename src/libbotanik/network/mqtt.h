#ifndef NETWORK_MQTT_H
#define NETWORK_MQTT_H

/**
 * @file mqtt.h
 * @brief MqttClient - minimal non-blocking MQTT 3.1.1 publisher over TCP.
 *
 * Config-driven (ConfigHandler, the `mqtt` block): it connects to the broker, keeps the
 * session alive with PINGREQ, and publishes QoS-0 messages. It restarts on a config
 * change and backs off / reconnects on failure. The broker name is resolved through the
 * shared NameResolver (which also holds the live interface set by NetworkHandler).
 *
 * Publish-only - no subscribe; it carries notifications outward (e.g. alarm transitions
 * from MqttAlarmNotifier). The client id is derived from Config::SITE_NAME.
 *
 * Mbed TCPSocket lives in mqtt.cpp (OPTA only); on the simulator there is no broker, so
 * the client stays idle and publish() reports not-connected.
 */

#include <cstdint>

#include "libbotanik/config_handler.h"
#include "libbotanik/network/resolver.h"

namespace Botanik { namespace Network {

    /** @brief Non-blocking MQTT 3.1.1 publisher. */
    class MqttClient : public ConfigHandler {
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
        State        _state;
        uint64_t     _since_ms;       ///< when the current state was entered (timeouts)
        uint64_t     _last_ping_ms;   ///< last keepalive PINGREQ
        NameResolver* _resolver;      ///< DNS + the live interface (set by NetworkHandler)
        ResolvedAddr _broker_addr;    ///< resolved broker address
        /** @} */

    public:
        MqttClient(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr,
                   NameResolver* resolver = nullptr);

        /** @brief True when connected and ready to publish. */
        bool connected() const { return _state == MQTT_READY; }
        /** @brief Publish a QoS-0 message now if connected; returns false otherwise (the
         *  caller may drop or retry later). @p retain sets the MQTT retain flag. */
        bool publish(const char* topic, const char* payload, bool retain = false);

    protected:
        uint32_t config_serial() const override;
        void on_config_changed(uint64_t now_ms) override;
        void on_step(uint64_t now_ms) override;

    private:
        /** @name Internals (the Mbed-touching ones are OPTA-only)
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
