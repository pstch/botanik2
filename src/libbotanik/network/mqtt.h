// REVIEW_35: TODO
#ifndef NETWORK_MQTT_H
#define NETWORK_MQTT_H

/**
 * @file mqtt.h
 * @brief MqttClient - minimal non-blocking MQTT 3.1.1 publisher over TCP.
 *
 * Config-driven (Component, the `mqtt` block): connects, keeps alive with
 * PINGREQ, publishes QoS-0; restarts on a config change, backs off / reconnects
 * on failure. The broker name resolves through the shared NameResolver; the
 * link must be up (NetworkHandler::is_up()) before a connect.
 *
 * Publish-only (carries notifications outward, e.g. alarm transitions). Client
 * id is Config::SITE_NAME.
 */

#include <cstdint>

#include "libbotanik/hal/net.h"
#include "libbotanik/network/network.h"
#include "libbotanik/network/resolver.h"
#include "libbotanik/vars/params.h"
// Alarms::Publisher (alarm transport)
#include "libbotanik/alarms/publisher.h"

namespace Botanik { namespace Tasks { struct Tick; class Registry; } }

namespace Botanik { namespace Network {

    /** @brief The broker a MqttClient talks to. keepalive is also the
     *  dead-broker detection: a wedged peer leaves the socket open and answers
     *  nothing, and this is what notices. */
    struct MqttConfig {
        bool     enabled = true;
        uint16_t port    = 1883;
        uint16_t ka_sec  = 60;
        char     host[BOTANIK_HOST_STR_LEN] = {0};
    };

    /** @brief Encode an MQTT "remaining length" varint (1..4 bytes, 7 data
     *  bits each) into @p p; returns the number of bytes written. */
    inline int encode_remlen(uint8_t* p, uint32_t len) {
        int n = 0;
        do {
            uint8_t b = (uint8_t)(len % 128);
            len /= 128;
            if (len > 0) b |= 0x80;
            p[n++] = b;
        } while (len > 0 && n < 4);
        return n;
    }

    /** @brief Non-blocking MQTT 3.1.1 publisher. Also serves as the alarm
     *  transport (Alarms::Publisher), so HealthMonitor's notifier publishes
     *  through it. */
    class MqttClient : public Component, public Alarms::Publisher {
    public:
        void register_tasks() override;
    private:
        /** @brief Registration handler: a static MEMBER, so the cast reaches the
         *  protected hook without a friend and stays beside the class. */
        static void _service_task(Component* c, const Tasks::Tick& t);

        /** @name State
         *  @{ */
        enum State : uint8_t {
            MQTT_IDLE = 0,    ///< disabled / no config
            /// resolving the broker name through the resolver
            MQTT_RESOLVE,
            MQTT_CONNECT,     ///< non-blocking TCP connect in progress
            MQTT_HANDSHAKE,   ///< CONNECT sent; awaiting CONNACK
            MQTT_READY,       ///< connected; keepalive + publishes
            MQTT_ERROR,       ///< failed; backing off before a retry
        };
        /** @brief Config source (may be null in tests). */
        /** @brief The broker in force, and whether a fresh one is waiting for
         *  the next SERVICE beat (set_config() has no clock). */
        MqttConfig    _cfg;
        bool          _pending = false;
        State                 _state;
        /// when this state was entered (timeouts)
        uint64_t              _since_ms;
        uint64_t              _last_ping_ms;   ///< last keepalive PINGREQ
        /** @brief Uptime of the last PINGRESP seen; 0 until the connection is
         *  up. What makes mqtt_ka_sec detect a dead broker. */
        uint64_t              _last_pong_ms;
        /** @brief CONNACK bytes held so far. TCP may split the four across
         *  segments, so the handshake reassembles rather than failing on a
         *  short read. Reset when the socket is opened. */
        uint8_t               _connack[4];
        uint8_t               _connack_len;
        uint64_t              _last_pub_ms;    ///< last successful publish (0 = none)
        NameResolver*         _resolver;       ///< DNS resolver
        ResolvedAddr          _broker_addr;    ///< resolved broker address
        /// link gate (null = always blocked)
        const NetworkHandler* _net;
        HAL::Network::TcpSocket   _sock;           ///< open TCP socket (-1 = none)
        /** @} */

    public:
        /** @brief Adopt @p cfg: drop the socket and reconnect, or idle. */
        void set_config(const MqttConfig& cfg);

        MqttClient(const SetupContext& ctx,
                   NameResolver* resolver = nullptr, const NetworkHandler* net = nullptr);

        /** @brief True when connected and ready to publish. */
        bool connected() const { return _state == MQTT_READY; }
        /** @brief Connection phase as a short display string
         *  (off/resolving/connecting/handshake/ready/error). */
        const char* status_str() const;
        /** @brief Resolved broker IPv4 the socket is using; meaningful while
         *  connected(). */
        ResolvedAddr peer_addr() const { return _broker_addr; }
        /** @brief Broker TCP port in use (config port, else the default). */
        uint16_t peer_port() const;
        /** @brief Uptime ms of the last broker contact (connect or keepalive
         *  ping); 0 if never connected. The status UI shows the age of this as
         *  "time since last sync". */
        uint64_t last_contact_ms() const { return _last_ping_ms; }
        /** @brief Uptime ms of the last successful publish; 0 if nothing has
         *  been published yet. */
        uint64_t last_publish_ms() const { return _last_pub_ms; }
        /** @brief Publish a QoS-0 message now if connected; returns false
         *  otherwise (the caller may drop or retry later). @p retain sets the
         *  MQTT retain flag. */
        bool publish(const char* topic, const char* payload, bool retain);
        /** @brief Alarms::Publisher: publish non-retained (QoS 0). */
        bool publish(const char* topic, const char* payload) override {
            return publish(topic, payload, false);
        }

    protected:
        void on_service(uint64_t now_ms) override;

    private:
        /** @name Internals
         *  @{ */
        void _enter(State s, uint64_t now_ms);
        /// close the socket and back off (-> ERROR)
        void _fail(uint64_t now_ms);
        /// open + start a non-blocking TCP connect
        bool _tcp_begin();
        int  _tcp_poll();                ///< 1 connected, 0 waiting, -1 failed
        bool _send_connect();            ///< send the MQTT CONNECT packet
        int  _poll_connack();            ///< 1 accepted, 0 waiting, -1 failed
        /// READY: keepalive ping + drain/health-check
        void _service(uint64_t now_ms);
        /** @} */
    };

}}

#endif // NETWORK_MQTT_H
