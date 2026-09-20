/**
 * @file mqtt.cpp
 * @brief MqttClient - non-blocking MQTT 3.1.1 publish over HAL::Net TCP.
 *
 * Hand-rolled wire protocol (CONNECT / CONNACK / PUBLISH QoS-0 / PINGREQ),
 * gated on NetworkHandler::is_up() before a connect.
 */

#include "libbotanik/tasks/tasks.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_MQTT
#include "libbotanik/network/mqtt.h"

#include <cstring>

#include "libbotanik/config/identity.gen.h"

#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Network {

    MqttClient::MqttClient(const SetupContext& ctx,
                           NameResolver* resolver, const NetworkHandler* net)
        : Component(ctx, "MqttClient"),
          _state(MQTT_IDLE), _since_ms(0), _last_ping_ms(0), _last_pong_ms(0),
          _connack(), _connack_len(0), _last_pub_ms(0),
          _tx(), _tx_len(0), _tx_since_ms(0), _fault(FAULT_NONE),
          _resolver(resolver), _net(net), _sock(-1) {
        _broker_addr = HAL::Network::Ip4();   // 0.0.0.0 until resolved
    }

    void MqttClient::set_config(const MqttConfig& cfg) {
        _cfg = cfg;
        _pending = true;
    }

    uint16_t MqttClient::peer_port() const { return _cfg.port; }

    const char* MqttClient::fault_str() const {
        switch (_fault) {
            case FAULT_NO_BROKER:  return "no broker";
            case FAULT_DNS:        return "name failed";
            case FAULT_SOCKET:     return "refused";
            case FAULT_TIMEOUT:    return "timed out";
            case FAULT_REFUSED:    return "rejected";
            case FAULT_TX_STALLED: return "send stalled";
            case FAULT_SILENT:     return "broker silent";
            default:               return "none";
        }
    }

    Plc::Health MqttClient::verdict() const {
        switch (_state) {
            case MQTT_READY:  return Plc::Health::HEALTHY;
            // Nobody asked for a broker, so nothing is wrong with not having
            // one. The same reading as connected, on purpose.
            case MQTT_IDLE:   return Plc::Health::HEALTHY;
            case MQTT_ERROR:  return Plc::Health::FAULTED;
            default:          return Plc::Health::DEGRADED;   // on the way up
        }
    }

    const char* MqttClient::status_str() const {
        switch (_state) {
            case MQTT_RESOLVE:   return "resolving";
            case MQTT_CONNECT:   return "connecting";
            case MQTT_HANDSHAKE: return "handshake";
            case MQTT_READY:     return "ready";
            case MQTT_ERROR:     return fault_str();
            default:             return "off";
        }
    }

    void MqttClient::_enter(State s, uint64_t now_ms) { _state = s; _since_ms = now_ms; }

    void MqttClient::_fail(uint64_t now_ms, Fault why) {
        if (_sock >= 0) { HAL::Network::tcp_close(_sock); _sock = -1; }
        _tx_len = 0;                     // the stream is gone, and so is its tail
        _fault  = why;
        // Enter ERROR before logging: a log sink that publishes the line (see
        // MqttLogWriter) would otherwise re-enter publish() while _state is still
        // READY (socket now closed), fail the send, and recurse back into _fail().
        _enter(MQTT_ERROR, now_ms);
        if (_logger != nullptr) _logger->error(Logs::NETWORK, "MQTT down: %s", fault_str());
    }

    void MqttClient::on_service(uint64_t now_ms) {
        // A config landed at CONFIG, earlier in this same beat: drop the socket
        // and reconnect against whatever the broker is now.
        if (_pending) {
            _pending = false;
            if (_sock >= 0) { HAL::Network::tcp_close(_sock); _sock = -1; }
            _enter(_cfg.enabled ? MQTT_RESOLVE : MQTT_IDLE, now_ms);
        }

        switch (_state) {
            case MQTT_IDLE:
                break;

            case MQTT_RESOLVE: {
                if (!_cfg.enabled) { _enter(MQTT_IDLE, now_ms); break; }
                // Needs the link to be up before opening a TCP socket.
                if (_net == nullptr || !_net->is_up()) break;
                const char* broker = _cfg.host;
                if (broker[0] == '\0') { _fail(now_ms, FAULT_NO_BROKER); break; }

                if (_resolver == nullptr) { _fail(now_ms, FAULT_NO_BROKER); break; }
                ResolveStatus rs = _resolver->resolve(broker, &_broker_addr);
                if (rs == RESOLVE_PENDING) break;
                if (rs == RESOLVE_FAILED)  { _fail(now_ms, FAULT_DNS); break; }

                if (_tcp_begin()) _enter(MQTT_CONNECT, now_ms);
                else              _fail(now_ms, FAULT_SOCKET);
                break;
            }

            case MQTT_CONNECT:
            {
                int r = _tcp_poll();
                if (r > 0) {
                    if (_send_connect(now_ms)) _enter(MQTT_HANDSHAKE, now_ms);
                    else                       _fail(now_ms, FAULT_SOCKET);
                }
                else if (r < 0) _fail(now_ms, FAULT_SOCKET);
                else if (now_ms - _since_ms > Config::MQTT_CONNECT_TIMEOUT_MS)
                    _fail(now_ms, FAULT_TIMEOUT);
            }
                break;

            case MQTT_HANDSHAKE:
            {
                // The CONNECT may still be half on the wire; the broker has
                // nothing to answer until all of it is.
                const int f = _flush_tx(now_ms);
                if (f < 0)  { _fail(now_ms, FAULT_SOCKET); break; }
                int r = f == 0 ? 0 : _poll_connack();
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
                    _fail(now_ms, FAULT_SOCKET);
                } else if (now_ms - _since_ms > Config::MQTT_CONNECT_TIMEOUT_MS) {
                    _fail(now_ms, FAULT_TIMEOUT);
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
        const uint64_t now_ms = HAL::Clock::uptime_ms();
        if (!_send_packet(buf, i, now_ms)) return false;
        _last_pub_ms = now_ms;
        return true;
    }

    int MqttClient::_flush_tx(uint64_t now_ms) {
        if (_tx_len == 0) return 1;
        const int n = HAL::Network::tcp_send(_sock, _tx, _tx_len);
        if (n < 0) return -1;
        if (n > 0) {
            _tx_len = (uint16_t)(_tx_len - n);
            if (_tx_len != 0) memmove(_tx, _tx + n, _tx_len);
            _tx_since_ms = now_ms;       // progress: the stall clock restarts
        }
        return _tx_len == 0 ? 1 : 0;
    }

    bool MqttClient::_send_packet(const uint8_t* buf, int len, uint64_t now_ms) {
        // Behind a tail there is nowhere for a new packet to go: appending it
        // would need a queue, and a QoS-0 publisher is allowed to lose the
        // message instead. The tail's own stall timer is what ends the session.
        if (_tx_len != 0) return false;
        const int n = HAL::Network::tcp_send(_sock, buf, len);
        if (n < 0) { _fail(now_ms, FAULT_SOCKET); return false; }
        if (n == 0) return false;        // would-block: nothing written, drop it
        if (n < len) {
            _tx_len      = (uint16_t)(len - n);
            _tx_since_ms = now_ms;
            memcpy(_tx, buf + n, _tx_len);
        }
        return true;
    }

    bool MqttClient::_tcp_begin() {
        if (_sock >= 0) HAL::Network::tcp_close(_sock);
        _sock = HAL::Network::tcp_open();
        if (_sock < 0) return false;
        // A new socket is a new handshake: whatever the last one had collected
        // is not part of this CONNACK, and the tail of a packet the dead
        // session never finished is not part of this stream.
        _connack_len = 0;
        _last_pong_ms = 0;
        _tx_len = 0;
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

    bool MqttClient::_send_connect(uint64_t now_ms) {
        const char* cid = Config::SITE_NAME;
        uint16_t cid_len = (uint16_t)strlen(cid);
        // mqtt_ka_sec IS the setting (band starts at 10).
        uint16_t keepalive = _cfg.ka_sec;

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

        return _send_packet(buf, i, now_ms);
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
            if (r < 0) { _fail(now_ms, FAULT_SOCKET); return; }   // closed/error
            // A PINGRESP anywhere in what we drained is proof the broker is
            // still answering. Only the packet type is checked: the byte after
            // it is a zero remaining-length, and nothing else the broker sends
            // us on this connection begins 0xD0.
            for (int j = 0; j < r; ++j)
                if (scratch[j] == 0xD0) { _last_pong_ms = now_ms; break; }
        }

        // The rest of a half-written packet goes before anything new. A broker
        // that never drains it leaves the session unusable - MQTT frames back
        // to back, so the bytes after an unfinished packet are unreadable - and
        // that is a link to give up on rather than publish into.
        const int f = _flush_tx(now_ms);
        if (f < 0) { _fail(now_ms, FAULT_SOCKET); return; }
        if (f == 0 && now_ms - _tx_since_ms > Config::MQTT_TX_STALL_MS) {
            _fail(now_ms, FAULT_TX_STALLED);
            return;
        }

        // 0 is what MQTT 3.1.1 calls "no keep-alive", and it is what the
        // CONNECT already told the broker - so honour it on BOTH halves and do
        // nothing here. The band starts at 10, but an override is deliberately
        // unbanded: a maintenance session may want the pings off, and "off"
        // must not mean a PINGREQ every beat.
        const uint64_t ka_ms = (uint64_t)_cfg.ka_sec * 1000ull;
        if (ka_ms == 0) return;

        // A wedged broker, or a path that black-holes, leaves the socket open
        // and answering nothing; without this the client reports connected for
        // ever while everything it publishes vanishes. This is what makes
        // mqtt_ka_sec the dead-broker detection. Half a keep-alive of slack, so
        // a miss is noticed inside one and a half.
        if (_last_pong_ms != 0 &&
            now_ms - _last_pong_ms > ka_ms + ka_ms / 2ull) {
            _fail(now_ms, FAULT_SILENT);
            return;
        }

        if (now_ms - _last_ping_ms > ka_ms / 2ull) {
            uint8_t ping[2] = { 0xC0, 0x00 };
            // A socket with no room is backpressure, not a dead broker: leave
            // _last_ping_ms alone and the next beat tries again. The silence
            // check above is what ends a session that never clears.
            if (_send_packet(ping, 2, now_ms)) _last_ping_ms = now_ms;
        }
    }

}}

namespace Botanik { namespace Network {


    void MqttClient::register_tasks() {
        _tasks->add(*this, "sys_mqtt", Tasks::EVERY_BEAT,
              Tasks::PHASE_SERVICE);
    }

}}  // namespace Botanik::Network

#endif // BOTANIK_FEATURE_MQTT
