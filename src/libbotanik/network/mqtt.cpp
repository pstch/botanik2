// REVIEW_35: TODO
/**
 * @file mqtt.cpp
 * @brief MqttClient - non-blocking MQTT 3.1.1 publish over HAL::Net TCP.
 *
 * Hand-rolled wire protocol (CONNECT / CONNACK / PUBLISH QoS-0 / PINGREQ),
 * gated on NetworkHandler::is_up() before a connect.
 */

#include "libbotanik/tasks/tasks.h"
#include "libbotanik/network/mqtt.h"

#include <cstring>

#include "libbotanik/config/identity.h"

#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Network {

    MqttClient::MqttClient(const SetupContext& ctx, const Vars::Bank* cfg,
                           NameResolver* resolver, const NetworkHandler* net)
        : Component(ctx, "MqttClient"), _io(cfg),
          _state(MQTT_IDLE), _since_ms(0), _last_ping_ms(0), _last_pong_ms(0),
          _connack(), _connack_len(0), _last_pub_ms(0),
          _resolver(resolver), _net(net), _sock(-1) {
        _broker_addr = HAL::Network::Ip4();   // 0.0.0.0 until resolved
    }

    uint32_t MqttClient::param_serial() const {
        return Vars::group_serial(_io, "mqtt");
    }

    uint16_t MqttClient::peer_port() const {
        // The mqtt_port parameter IS the setting: its band starts at 1, so
        // there is no zero to substitute for. A fallback here would be a second
        // default, free to drift from the one on the PARAMS page.
        return (uint16_t)Vars::get_int(_io, "mqtt_port");
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

    void MqttClient::on_param_changed(uint64_t now_ms) {
        if (_sock >= 0) { HAL::Network::tcp_close(_sock); _sock = -1; }
        _enter(Vars::get_int(_io, "mqtt_enable") ? MQTT_RESOLVE : MQTT_IDLE, now_ms);
    }

    void MqttClient::on_service(uint64_t now_ms) {
        switch (_state) {
            case MQTT_IDLE:
                break;

            case MQTT_RESOLVE: {
                if (!Vars::get_int(_io, "mqtt_enable")) { _enter(MQTT_IDLE, now_ms); break; }
                // Needs the link to be up before opening a TCP socket.
                if (_net == nullptr || !_net->is_up()) break;
                const char* broker = Vars::get_str(_io, "mqtt_host");
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
                    // The broker has just answered, so the liveness clock
                    // starts here rather than at 0 (which would read as a
                    // silence stretching back to boot).
                    _last_pong_ms = now_ms;
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
        // A new socket is a new handshake: whatever the last one had collected
        // is not part of this CONNACK.
        _connack_len = 0;
        _last_pong_ms = 0;
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
        // mqtt_ka_sec IS the setting (band starts at 10).
        uint16_t keepalive = (uint16_t)Vars::get_int(_io, "mqtt_ka_sec");

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
        // Reassembled across beats, never failed on a short read: TCP promises a
        // byte stream, not segment boundaries, so a CONNACK split 2+2 by a proxy
        // or a loaded broker is legal. The in-process mock cannot produce one,
        // so no test covers it. MQTT_CONNECT_TIMEOUT_MS bounds the wait.
        while (_connack_len < sizeof(_connack)) {
            int r = HAL::Network::tcp_recv(_sock, _connack + _connack_len,
                                           (int)(sizeof(_connack) - _connack_len));
            if (r == 0) return 0;                       // would-block: more later
            if (r < 0) return -1;                       // closed / error
            _connack_len = (uint8_t)(_connack_len + r);
        }
        // Fixed header 0x20, remaining length 2, then the session flag and the
        // return code; only a zero return code is an accepted connection.
        if (_connack[0] != 0x20 || _connack[1] != 0x02 || _connack[3] != 0x00)
            return -1;
        return 1;
    }

    void MqttClient::_service(uint64_t now_ms) {
        // Bounded: we only ever expect CONNACK (4 B) and PINGRESP (2 B), so
        // MQTT_RX_PER_STEP x 32 B is ample headroom, and a broker spewing bytes
        // cannot stall the beat. Any remainder drains next beat.
        uint8_t scratch[32];
        for (uint8_t i = 0; i < Config::MQTT_RX_PER_STEP; ++i) {
            int r = HAL::Network::tcp_recv(_sock, scratch, sizeof(scratch));
            if (r == 0) break;           // would-block: drained
            if (r < 0) { _fail(now_ms); return; }   // closed/error
            // A PINGRESP anywhere in what we drained is proof the broker is
            // still answering. Only the packet type is checked: the byte after
            // it is a zero remaining-length, and nothing else the broker sends
            // us on this connection begins 0xD0.
            for (int j = 0; j < r; ++j)
                if (scratch[j] == 0xD0) { _last_pong_ms = now_ms; break; }
        }

        const uint32_t ka_s = (uint32_t)Vars::get_int(_io, "mqtt_ka_sec");
        const uint64_t ka_ms = (uint64_t)ka_s * 1000ull;

        // A wedged broker, or a path that black-holes, leaves the socket open
        // and answering nothing; without this the client reports connected for
        // ever while everything it publishes vanishes. This is what makes
        // mqtt_ka_sec the dead-broker detection. Half a keep-alive of slack, so
        // a miss is noticed inside one and a half.
        if (ka_ms != 0 && _last_pong_ms != 0 &&
            now_ms - _last_pong_ms > ka_ms + ka_ms / 2ull) {
            _fail(now_ms);
            return;
        }

        if (now_ms - _last_ping_ms > ka_ms / 2ull) {
            uint8_t ping[2] = { 0xC0, 0x00 };
            if (HAL::Network::tcp_send(_sock, ping, 2) != 2) { _fail(now_ms); return; }
            _last_ping_ms = now_ms;
        }
    }

}}

namespace Botanik { namespace Network {

    void MqttClient::_service_task(Component* c, const Tasks::Tick& t) {
        static_cast<MqttClient*>(c)->on_service(t.now_ms);
    }

    void MqttClient::register_tasks() {
        _tasks->add(*this, &MqttClient::_service_task, "mqtt", Tasks::GROUP_FAST,
              Tasks::PHASE_SERVICE);
    }

}}  // namespace Botanik::Network
