// REVIEW_35: TODO
/**
 * @file mqtt.cpp
 * @brief MqttClient - non-blocking MQTT 3.1.1 publish over HAL::Net TCP.
 *
 * Hand-rolled wire protocol (CONNECT / CONNACK / PUBLISH QoS-0 / PINGREQ),
 * gated on NetworkHandler::is_up() before a connect.
 */

#include "libbotanik/network/mqtt.h"

#include <cstring>                     // strlen / memcpy
// HAL::Clock::uptime_ms (publish, called off the step path)
#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Network {

    MqttClient::MqttClient(const Cfg::ConfigHandler* cfg, Logs::Logger* logger,
                           NameResolver* resolver, const NetworkHandler* net)
        : Component(logger, "MqttClient"), _cfg(cfg),
          _state(MQTT_IDLE), _since_ms(0), _last_ping_ms(0), _last_pub_ms(0),
          _resolver(resolver), _net(net), _sock(-1) {
        _broker_addr = HAL::Network::Ip4();   // 0.0.0.0 until resolved
    }

    uint32_t MqttClient::config_serial() const {
        return Cfg::cfg_group_serial(_cfg, Cfg::CFG_MQTT);
    }

    uint16_t MqttClient::peer_port() const {
        uint16_t port = (uint16_t)Cfg::cfg_int(_cfg, "mqtt_port");
        return port ? port : Config::MQTT_PORT_DEFAULT;
    }

    const char* MqttClient::status_str() const {
        switch (_state) {
            case MQTT_RESOLVE:   return "resolving";
            case MQTT_CONNECT:   return "connecting";
            case MQTT_HANDSHAKE: return "handshake";
            case MQTT_READY:     return "ready";
            case MQTT_ERROR:     return "error";
            default:             return "off";
        }
    }

    void MqttClient::_enter(State s, uint64_t now_ms) { _state = s; _since_ms = now_ms; }

    void MqttClient::_fail(uint64_t now_ms) {
        if (_sock >= 0) { HAL::Network::tcp_close(_sock); _sock = -1; }
        // Enter ERROR before logging: a log sink that publishes the line (see
        // MqttLogWriter) would otherwise re-enter publish() while _state is still
        // READY (socket now closed), fail the send, and recurse back into _fail().
        _enter(MQTT_ERROR, now_ms);
        if (_logger != nullptr) _logger->error(Logs::NETWORK, "MQTT connection lost");
    }

    void MqttClient::on_config_changed(uint64_t now_ms) {
        if (_sock >= 0) { HAL::Network::tcp_close(_sock); _sock = -1; }
        _enter(Cfg::cfg_int(_cfg, "mqtt_enabled") ? MQTT_RESOLVE : MQTT_IDLE, now_ms);
    }

    void MqttClient::on_step(uint64_t now_ms) {
        switch (_state) {
            case MQTT_IDLE:
                break;

            case MQTT_RESOLVE: {
                if (!Cfg::cfg_int(_cfg, "mqtt_enabled")) { _enter(MQTT_IDLE, now_ms); break; }
                // Needs the link to be up before opening a TCP socket.
                if (_net == nullptr || !_net->is_up()) break;
                const char* broker = Cfg::cfg_str(_cfg, "mqtt_broker");
                if (broker[0] == '\0') { _fail(now_ms); break; }

                if (_resolver == nullptr) { _fail(now_ms); break; }
                ResolveStatus rs = _resolver->resolve(broker, &_broker_addr);
                if (rs == RESOLVE_PENDING) break;
                if (rs == RESOLVE_FAILED)  { _fail(now_ms); break; }

                if (_tcp_begin()) _enter(MQTT_CONNECT, now_ms);
                else              _fail(now_ms);
                break;
            }

            case MQTT_CONNECT:
            {
                int r = _tcp_poll();
                if (r > 0) {
                    if (_send_connect()) _enter(MQTT_HANDSHAKE, now_ms);
                    else                 _fail(now_ms);
                }
                else if (r < 0) _fail(now_ms);
                else if (now_ms - _since_ms > Config::MQTT_CONNECT_TIMEOUT_MS)
                    _fail(now_ms);
            }
                break;

            case MQTT_HANDSHAKE:
            {
                int r = _poll_connack();
                if (r > 0) {
                    _enter(MQTT_READY, now_ms);
                    _last_ping_ms = now_ms;
                    if (_logger) {
                        char ab[16];
                        _logger->notice(Logs::NETWORK, "MQTT connected (%s:%u)",
                                        HAL::Network::ip4_to_str(_broker_addr, ab, sizeof(ab)),
                                        (unsigned)peer_port());
                    }
                } else if (r < 0) {
                    _fail(now_ms);
                } else if (now_ms - _since_ms > Config::MQTT_CONNECT_TIMEOUT_MS) {
                    _fail(now_ms);
                }
            }
                break;

            case MQTT_READY:
                _service(now_ms);
                break;

            case MQTT_ERROR:
                if (now_ms - _since_ms > Config::MQTT_RETRY_MS) _enter(MQTT_RESOLVE, now_ms);
                break;
        }
    }

    bool MqttClient::publish(const char* topic, const char* payload, bool retain) {
        if (_state != MQTT_READY || topic == nullptr || payload == nullptr) return false;
        uint16_t tlen = (uint16_t)strlen(topic);
        uint32_t plen = (uint32_t)strlen(payload);
        uint8_t buf[Config::MQTT_TX_BUF];
        uint32_t remlen = 2u + tlen + plen;
        int i = 0;
        buf[i++] = retain ? 0x31 : 0x30;
        i += encode_remlen(&buf[i], remlen);
        buf[i++] = (uint8_t)(tlen >> 8);
        buf[i++] = (uint8_t)(tlen & 0xFF);
        if ((size_t)i + tlen + plen > sizeof(buf)) return false;
        // NOLINTNEXTLINE(bugprone-not-null-terminated-result): binary MQTT packet, not a C string
        memcpy(&buf[i], topic, tlen);    i += tlen;
        // NOLINTNEXTLINE(bugprone-not-null-terminated-result): binary MQTT packet, not a C string
        memcpy(&buf[i], payload, plen);  i += (int)plen;
        if (HAL::Network::tcp_send(_sock, buf, i) != i) { _fail(HAL::Clock::uptime_ms()); return false; }
        _last_pub_ms = HAL::Clock::uptime_ms();
        return true;
    }

    bool MqttClient::_tcp_begin() {
        if (_sock >= 0) HAL::Network::tcp_close(_sock);
        _sock = HAL::Network::tcp_open();
        if (_sock < 0) return false;
        // 1 done, 0 pending, <0 err
        int r = HAL::Network::tcp_connect(_sock, _broker_addr, peer_port());
        if (r >= 0) return true;
        HAL::Network::tcp_close(_sock); _sock = -1;
        return false;
    }

    int MqttClient::_tcp_poll() {
        // 1 connected, 0 waiting, -1 failed
        return HAL::Network::tcp_connect(_sock, _broker_addr, peer_port());
    }

    bool MqttClient::_send_connect() {
        const char* cid = Config::SITE_NAME;
        uint16_t cid_len = (uint16_t)strlen(cid);
        uint16_t keepalive = (uint16_t)Cfg::cfg_int(_cfg, "mqtt_keepalive_s");
        if (keepalive == 0) keepalive = Config::MQTT_KEEPALIVE_DEFAULT_S;

        uint8_t buf[Config::MQTT_TX_BUF];
        // variable header (10) + client-id (2-byte len prefix + cid_len)
        uint32_t remlen = 10u + 2u + cid_len;
        int i = 0;
        buf[i++] = 0x10;                             // CONNECT
        i += encode_remlen(&buf[i], remlen);
        buf[i++] = 0x00; buf[i++] = 0x04;           // protocol name "MQTT"
        buf[i++] = 'M'; buf[i++] = 'Q'; buf[i++] = 'T'; buf[i++] = 'T';
        // protocol level 4 (MQTT 3.1.1)
        buf[i++] = 0x04;
        // connect flags: clean session
        buf[i++] = 0x02;
        buf[i++] = (uint8_t)(keepalive >> 8);
        buf[i++] = (uint8_t)(keepalive & 0xFF);
        buf[i++] = (uint8_t)(cid_len >> 8);
        buf[i++] = (uint8_t)(cid_len & 0xFF);
        if ((size_t)i + cid_len > sizeof(buf)) return false;
        memcpy(&buf[i], cid, cid_len); i += cid_len;

        return HAL::Network::tcp_send(_sock, buf, i) == i;
    }

    int MqttClient::_poll_connack() {
        uint8_t buf[4];
        int r = HAL::Network::tcp_recv(_sock, buf, sizeof(buf));
        if (r == 0) return 0;            // would-block
        if (r < 4) return -1;
        if (buf[0] != 0x20 || buf[3] != 0x00) return -1;
        return 1;
    }

    void MqttClient::_service(uint64_t now_ms) {
        uint8_t scratch[32];
        for (;;) {
            int r = HAL::Network::tcp_recv(_sock, scratch, sizeof(scratch));
            if (r == 0) break;           // would-block: drained
            if (r < 0) { _fail(now_ms); return; }   // closed/error
        }
        uint32_t ka_s = (uint32_t)Cfg::cfg_int(_cfg, "mqtt_keepalive_s");
        if (ka_s == 0) ka_s = Config::MQTT_KEEPALIVE_DEFAULT_S;
        if (now_ms - _last_ping_ms > (uint64_t)ka_s * 1000ull / 2ull) {
            uint8_t ping[2] = { 0xC0, 0x00 };
            if (HAL::Network::tcp_send(_sock, ping, 2) != 2) { _fail(now_ms); return; }
            _last_ping_ms = now_ms;
        }
    }

}}
