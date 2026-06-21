/**
 * @file mqtt.cpp
 * @brief MqttClient implementation - non-blocking MQTT 3.1.1 publish over TCP.
 *
 * Hand-rolled wire protocol (CONNECT / CONNACK / PUBLISH QoS-0 / PINGREQ): the Mbed
 * core ships no MQTT client, only a TCPSocket. The TCP/Mbed bits are OPTA-only; on the
 * simulator there is no broker, so the machine never leaves IDLE/RESOLVE and publish()
 * returns false.
 */

#include "libbotanik/network/mqtt.h"

#include <cstring>                     // strlen / memcpy
#include "libbotanik/hal/platform.h"   // HAL::uptime_ms (publish, called off the step path)

#ifdef TARGET_OPTA
#include "netsocket/TCPSocket.h"
#include "netsocket/SocketAddress.h"
#endif

namespace Botanik { namespace Network {

#ifdef TARGET_OPTA
    namespace {
        // The single broker connection (one MqttClient).
        TCPSocket& mqtt_socket() {
            static TCPSocket sock;
            return sock;
        }

        // MQTT "remaining length" varint (1..4 bytes, 7 data bits each). Returns the
        // number of bytes written to @p p.
        int encode_remlen(uint8_t* p, uint32_t len) {
            int n = 0;
            do {
                uint8_t b = (uint8_t)(len % 128);
                len /= 128;
                if (len > 0) b |= 0x80;
                p[n++] = b;
            } while (len > 0 && n < 4);
            return n;
        }
    }  // namespace
#endif

    MqttClient::MqttClient(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger,
                           NameResolver* resolver)
        : ConfigHandler(ctx, logger, "mqtt"),
          _state(MQTT_IDLE), _since_ms(0), _last_ping_ms(0), _resolver(resolver) {
        _broker_addr.bytes[0] = _broker_addr.bytes[1] = 0;
        _broker_addr.bytes[2] = _broker_addr.bytes[3] = 0;
    }

    uint32_t MqttClient::config_serial() const {
        const PlcIO::PlcOutputConfig_t* c = config();
        return c ? c->mqtt.serial : 0;
    }

    void MqttClient::_enter(State s, uint64_t now_ms) {
        _state = s;
        _since_ms = now_ms;
    }

    void MqttClient::_fail(uint64_t now_ms) {
#ifdef TARGET_OPTA
        mqtt_socket().close();
#endif
        if (_logger != nullptr) _logger->warning(PlcIO::NETWORK, "MQTT disconnected");
        _enter(MQTT_ERROR, now_ms);
    }

    void MqttClient::on_config_changed(uint64_t now_ms) {
#ifdef TARGET_OPTA
        mqtt_socket().close();
#endif
        const PlcIO::PlcOutputConfig_t* c = config();
        _enter((c != nullptr && c->mqtt.enabled) ? MQTT_RESOLVE : MQTT_IDLE, now_ms);
    }

    void MqttClient::on_step(uint64_t now_ms) {
        const PlcIO::PlcOutputConfig_t* c = config();
        switch (_state) {
            case MQTT_IDLE:
                break;

            case MQTT_RESOLVE: {
                if (c == nullptr || !c->mqtt.enabled) { _enter(MQTT_IDLE, now_ms); break; }
                // Needs the live interface (held by the resolver) for the TCP socket.
                if (_resolver == nullptr || _resolver->interface() == nullptr) break;
                if (c->mqtt.broker[0] == '\0') { _fail(now_ms); break; }

                ResolveStatus rs = _resolver->resolve(c->mqtt.broker, &_broker_addr);
                if (rs == RESOLVE_PENDING) break;
                if (rs == RESOLVE_FAILED)  { _fail(now_ms); break; }
#ifdef TARGET_OPTA
                if (_tcp_begin()) _enter(MQTT_CONNECT, now_ms);
                else              _fail(now_ms);
#else
                _fail(now_ms);   // no TCP on the sim
#endif
                break;
            }

            case MQTT_CONNECT:
#ifdef TARGET_OPTA
            {
                int r = _tcp_poll();
                if (r > 0)      { if (_send_connect()) _enter(MQTT_HANDSHAKE, now_ms); else _fail(now_ms); }
                else if (r < 0) _fail(now_ms);
                else if (now_ms - _since_ms > Config::MQTT_CONNECT_TIMEOUT_MS) _fail(now_ms);
            }
#endif
                break;

            case MQTT_HANDSHAKE:
#ifdef TARGET_OPTA
            {
                int r = _poll_connack();
                if (r > 0) {
                    _enter(MQTT_READY, now_ms);
                    _last_ping_ms = now_ms;
                    if (_logger) _logger->notice(PlcIO::NETWORK, "MQTT connected");
                } else if (r < 0) {
                    _fail(now_ms);
                } else if (now_ms - _since_ms > Config::MQTT_CONNECT_TIMEOUT_MS) {
                    _fail(now_ms);
                }
            }
#endif
                break;

            case MQTT_READY:
#ifdef TARGET_OPTA
                _service(now_ms);
#endif
                break;

            case MQTT_ERROR:
                if (now_ms - _since_ms > Config::MQTT_RETRY_MS) _enter(MQTT_RESOLVE, now_ms);
                break;
        }
    }

    bool MqttClient::publish(const char* topic, const char* payload, bool retain) {
        if (_state != MQTT_READY || topic == nullptr || payload == nullptr) return false;
#ifdef TARGET_OPTA
        TCPSocket& s = mqtt_socket();
        uint16_t tlen = (uint16_t)strlen(topic);
        uint32_t plen = (uint32_t)strlen(payload);

        uint8_t buf[Config::MQTT_TX_BUF];
        uint32_t remlen = 2u + tlen + plen;        // topic-len field + topic + payload (QoS 0: no packet id)
        int i = 0;
        buf[i++] = retain ? 0x31 : 0x30;           // PUBLISH, QoS 0
        i += encode_remlen(&buf[i], remlen);
        buf[i++] = (uint8_t)(tlen >> 8);
        buf[i++] = (uint8_t)(tlen & 0xFF);
        if ((size_t)i + tlen + plen > sizeof(buf)) return false;   // too big for the scratch buffer
        memcpy(&buf[i], topic, tlen);    i += tlen;
        memcpy(&buf[i], payload, plen);  i += (int)plen;

        if (s.send(buf, i) != i) { _fail(HAL::uptime_ms()); return false; }
        return true;
#else
        (void)topic; (void)payload; (void)retain;
        return false;
#endif
    }

#ifdef TARGET_OPTA
    bool MqttClient::_tcp_begin() {
        TCPSocket& s = mqtt_socket();
        s.close();
        if (s.open(_resolver->interface()) != NSAPI_ERROR_OK) return false;
        s.set_blocking(false);

        const PlcIO::PlcOutputConfig_t* c = config();
        uint16_t port = (c && c->mqtt.port) ? c->mqtt.port : Config::MQTT_PORT_DEFAULT;
        SocketAddress addr;
        addr.set_ip_bytes(_broker_addr.bytes, NSAPI_IPv4);
        addr.set_port(port);

        nsapi_error_t r = s.connect(addr);   // non-blocking: IN_PROGRESS is the normal "started"
        if (r == NSAPI_ERROR_OK || r == NSAPI_ERROR_IN_PROGRESS) return true;
        s.close();
        return false;
    }

    int MqttClient::_tcp_poll() {
        TCPSocket& s = mqtt_socket();
        const PlcIO::PlcOutputConfig_t* c = config();
        uint16_t port = (c && c->mqtt.port) ? c->mqtt.port : Config::MQTT_PORT_DEFAULT;
        SocketAddress addr;
        addr.set_ip_bytes(_broker_addr.bytes, NSAPI_IPv4);
        addr.set_port(port);

        nsapi_error_t r = s.connect(addr);   // re-poll: IS_CONNECTED when done
        if (r == NSAPI_ERROR_IS_CONNECTED || r == NSAPI_ERROR_OK) return 1;
        if (r == NSAPI_ERROR_IN_PROGRESS || r == NSAPI_ERROR_ALREADY) return 0;
        return -1;
    }

    bool MqttClient::_send_connect() {
        TCPSocket& s = mqtt_socket();
        const char* cid = Config::SITE_NAME;
        uint16_t cid_len = (uint16_t)strlen(cid);
        const PlcIO::PlcOutputConfig_t* c = config();
        uint16_t keepalive = (c && c->mqtt.keepalive_s) ? c->mqtt.keepalive_s
                                                        : Config::MQTT_KEEPALIVE_DEFAULT_S;

        uint8_t buf[Config::MQTT_TX_BUF];
        uint32_t remlen = 10u + 2u + cid_len;        // variable header (10) + client-id (2 + len)
        int i = 0;
        buf[i++] = 0x10;                             // CONNECT
        i += encode_remlen(&buf[i], remlen);
        buf[i++] = 0x00; buf[i++] = 0x04;           // protocol name "MQTT"
        buf[i++] = 'M'; buf[i++] = 'Q'; buf[i++] = 'T'; buf[i++] = 'T';
        buf[i++] = 0x04;                            // protocol level 4 (MQTT 3.1.1)
        buf[i++] = 0x02;                            // connect flags: clean session
        buf[i++] = (uint8_t)(keepalive >> 8);
        buf[i++] = (uint8_t)(keepalive & 0xFF);
        buf[i++] = (uint8_t)(cid_len >> 8);
        buf[i++] = (uint8_t)(cid_len & 0xFF);
        if ((size_t)i + cid_len > sizeof(buf)) return false;
        memcpy(&buf[i], cid, cid_len); i += cid_len;

        return s.send(buf, i) == i;
    }

    int MqttClient::_poll_connack() {
        TCPSocket& s = mqtt_socket();
        uint8_t buf[4];
        nsapi_size_or_error_t r = s.recv(buf, sizeof(buf));
        if (r == NSAPI_ERROR_WOULD_BLOCK) return 0;
        if (r < 4) return -1;                        // error or short read
        // CONNACK: 0x20, len 0x02, ack flags, return code (0 = accepted).
        if (buf[0] != 0x20 || buf[3] != 0x00) return -1;
        return 1;
    }

    void MqttClient::_service(uint64_t now_ms) {
        TCPSocket& s = mqtt_socket();
        // Drain and discard anything inbound (PINGRESP, etc.); a closed peer -> fail.
        uint8_t scratch[32];
        for (;;) {
            nsapi_size_or_error_t r = s.recv(scratch, sizeof(scratch));
            if (r == NSAPI_ERROR_WOULD_BLOCK) break;
            if (r <= 0) { _fail(now_ms); return; }   // 0 = peer closed, <0 = error
        }
        // Keepalive: PINGREQ at half the negotiated interval.
        const PlcIO::PlcOutputConfig_t* c = config();
        uint32_t ka_s = (c && c->mqtt.keepalive_s) ? c->mqtt.keepalive_s
                                                   : Config::MQTT_KEEPALIVE_DEFAULT_S;
        if (now_ms - _last_ping_ms > (uint64_t)ka_s * 1000ull / 2ull) {
            uint8_t ping[2] = { 0xC0, 0x00 };        // PINGREQ
            if (s.send(ping, 2) != 2) { _fail(now_ms); return; }
            _last_ping_ms = now_ms;
        }
    }
#endif  // TARGET_OPTA

}}
