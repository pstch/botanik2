#ifndef BOTANIK_SIM_NETWORK_H
#define BOTANIK_SIM_NETWORK_H

/**
 * @file network.h
 * @brief Simulator-only mock NTP/MQTT responder.
 *
 * Header-only and guarded by TARGET_SIMULATOR (a near-empty include on OPTA,
 * where the real network plays this role). A tiny cooperative peer on loopback,
 * so the firmware's real NtpClient/MqttClient state machines reach SYNCED/READY
 * without touching the public network: the HAL resolves every name to 127.0.0.1
 * and routes loopback NTP to NTP_MOCK_PORT, and this answers there and on
 * MQTT_MOCK_PORT.
 *
 * The NTP/MQTT wire knowledge (the SNTP reply, the CONNACK/PINGRESP) lives here,
 * above the HAL, so the HAL transport stays protocol-agnostic. Every socket is
 * non-blocking and the main loop drives netd_step() once per beat, so there is
 * no thread; teardown goes through HAL::System::on_reset().
 */

#ifdef TARGET_SIMULATOR

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

// POSIX - the mock responder is a real in-process socket peer, not a stub.
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "libbotanik/config/net.h"
#include "libbotanik/config/scheduler.h"

#include "libbotanik/hal/net.h"
#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Network {

    /** @brief Whether the endpoints are bound and netd_step() has work to do. */
    inline bool& _netd_running() { static bool running = false; return running; }

    namespace detail {
        /** @brief A non-blocking loopback socket bound to @p port (UDP if @p
         *  dgram, else a listening TCP socket). -1 on failure. */
        inline int netd_bind(uint16_t port, bool dgram) {
            int fd = ::socket(AF_INET, dgram ? SOCK_DGRAM : SOCK_STREAM, 0);
            if (fd < 0) return -1;
            int one = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            // This instance's per-process loopback address, matching what the
            // HAL resolver hands the firmware, so concurrent sims do not share
            // the port on 127.0.0.1.
            HAL::Network::Ip4 lo = HAL::Network::mock_loopback_ip();
            sockaddr_in a;
            memset(&a, 0, sizeof(a));
            a.sin_family      = AF_INET;
            a.sin_port        = htons(port);
            memcpy(&a.sin_addr, lo.b, 4);
            if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) < 0) { ::close(fd); return -1; }
            if (!dgram && ::listen(fd, 4) < 0) { ::close(fd); return -1; }
            int fl = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
            // Close-on-exec so a System::reset() re-exec frees the bound port
            // instead of leaking the listener into the new image.
            fcntl(fd, F_SETFD, FD_CLOEXEC);
            return fd;
        }

        /** @brief Answer one SNTP request on @p udp with a reply carrying the
         *  host's current UTC time, so a synced client reads a near-zero offset
         *  (the sim's wall clock is the same host clock). */
        inline void netd_serve_ntp(int udp) {
            uint8_t req[48];
            sockaddr_in src;
            socklen_t sl = sizeof(src);
            ssize_t n = ::recvfrom(udp, req, sizeof(req), 0, reinterpret_cast<sockaddr*>(&src), &sl);
            if (n < 48) return;
            fprintf(stderr, "[mock-ntp] request (%zd bytes) -> reply\n", n);
            uint8_t rep[48];
            memset(rep, 0, sizeof(rep));
            rep[0] = 0x24;                              // LI=0, VN=4, Mode=4 (server)
            // UNIX seconds (UTC) shifted into the NTP epoch (1900).
            const uint32_t secs = (uint32_t)(::time(nullptr) + 2208988800u);
            rep[40] = (uint8_t)(secs >> 24); rep[41] = (uint8_t)(secs >> 16);
            rep[42] = (uint8_t)(secs >> 8);  rep[43] = (uint8_t)secs;
            ::sendto(udp, rep, sizeof(rep), 0, reinterpret_cast<sockaddr*>(&src), sl);
        }

        /** @brief Trace a received MQTT packet to stderr: the control-packet type,
         *  and for a PUBLISH its decoded topic and payload (QoS 0, the only QoS the
         *  publisher emits). */
        inline void netd_trace_mqtt(const uint8_t* pkt, ssize_t n) {
            switch (pkt[0] & 0xF0) {
                case 0x10: fprintf(stderr, "[mock-mqtt] CONNECT\n");  return;
                case 0xC0: fprintf(stderr, "[mock-mqtt] PINGREQ\n");  return;
                case 0xE0: fprintf(stderr, "[mock-mqtt] DISCONNECT\n"); return;
                case 0x30: break;                       // PUBLISH: decoded below
                default:
                    fprintf(stderr, "[mock-mqtt] packet type 0x%02X (%zd bytes)\n",
                            pkt[0] & 0xF0, n);
                    return;
            }
            // PUBLISH: skip the remaining-length varint, then read the topic
            // (2-byte length + bytes); the rest is the payload. QoS 0 -> no packet id.
            ssize_t i = 1;
            while (i < n && (pkt[i] & 0x80)) ++i;       // varint continuation bytes
            ++i;                                        // last varint byte
            if (i + 2 > n) { fprintf(stderr, "[mock-mqtt] PUBLISH (truncated)\n"); return; }
            int tlen = (pkt[i] << 8) | pkt[i + 1];
            i += 2;
            if (i + tlen > n) { fprintf(stderr, "[mock-mqtt] PUBLISH (truncated)\n"); return; }
            fprintf(stderr, "[mock-mqtt] PUBLISH %.*s = %.*s\n",
                    tlen, (const char*)&pkt[i],
                    (int)(n - i - tlen), (const char*)&pkt[i + tlen]);
        }

        /** @brief One MQTT client: its fd plus a reassembly buffer. TCP is a byte
         *  stream, so one read can coalesce several packets or split one;
         *  buffering lets us trace and answer each whole packet.
         *
         *  The buffer holds a WHOLE packet of the largest size the publisher
         *  can build. A smaller one cannot parse the packet that overruns it
         *  and has nothing to do but drop bytes from the middle of the stream,
         *  after which every frame boundary is wrong: the PINGREQs stop being
         *  recognised, no PINGRESP comes back, and the firmware declares a
         *  broker that is right here dead. */
        struct MqttConn {
            int      fd  = -1;
            // At least the firmware's own TX buffer: mqtt_log.h sizes that so
            // a full log line under the longest topic fits, and a mock that
            // cannot hold one drops traffic a real broker would take.
            uint8_t  buf[Config::MQTT_TX_BUF];
            uint16_t len = 0;
        };

        /** @brief Answer one whole MQTT packet: CONNECT -> CONNACK (accepted),
         *  PINGREQ -> PINGRESP, everything else (PUBLISH, ...) accepted + dropped. */
        inline void netd_answer_mqtt(int fd, const uint8_t* pkt, size_t n) {
            netd_trace_mqtt(pkt, (ssize_t)n);
            if ((pkt[0] & 0xF0) == 0x10) {              // CONNECT
                uint8_t ack[4] = { 0x20, 0x02, 0x00, 0x00 };   // CONNACK, accepted
                ::send(fd, ack, sizeof(ack), MSG_NOSIGNAL);
            } else if ((pkt[0] & 0xF0) == 0xC0) {       // PINGREQ
                uint8_t pong[2] = { 0xD0, 0x00 };       // PINGRESP
                ::send(fd, pong, sizeof(pong), MSG_NOSIGNAL);
            }
        }

        /** @brief Trace + answer every whole MQTT packet in @p c's buffer,
         *  leaving a partial tail for the next call. False when the stream can
         *  no longer be followed and the connection has to go. */
        inline bool netd_parse_mqtt(MqttConn& c) {
            // MQTT fixed header: type byte + a 1..4 byte remaining-length
            // varint + that many bytes.
            size_t off = 0;
            while (c.len - off >= 2) {
                uint32_t rem = 0, mult = 1;
                size_t   p = off + 1;
                int      rl = 0;
                bool     complete = false;
                for (; rl < 4 && p < c.len; ++rl) {
                    uint8_t enc = c.buf[p++];
                    rem += (enc & 0x7F) * mult;
                    if (!(enc & 0x80)) { complete = true; ++rl; break; }
                    mult *= 128;
                }
                if (!complete) break;                   // varint not all here yet
                size_t total = 1 + rl + rem;
                // Longer than the buffer will ever hold: this stream cannot be
                // resynchronised, so end the connection and let the client come
                // back on a boundary both ends agree on. Skipping the bytes
                // instead leaves the two framing different packets for ever.
                if (total > sizeof(c.buf)) {
                    fprintf(stderr, "[mock-mqtt] %zu-byte packet over the %zu-byte "
                                    "buffer; dropping the connection\n",
                            total, sizeof(c.buf));
                    return false;
                }
                if (c.len - off < total) break;         // body not all here yet
                netd_answer_mqtt(c.fd, &c.buf[off], total);
                off += total;
            }
            if (off > 0) {                              // drop consumed packets
                memmove(c.buf, c.buf + off, c.len - off);
                c.len -= (uint16_t)off;
            }
            return true;
        }

        /** @brief Read and answer until the socket is empty; false once the peer
         *  closes, so the caller drops the conn.
         *
         *  Until EMPTY, not one read per call: a publisher filling the socket
         *  faster than a single read drains it backs the kernel buffer up until
         *  its own sends block, and under a fast clock that is every beat. */
        inline bool netd_serve_mqtt(MqttConn& c) {
            for (;;) {
                // A buffer with no room left holds at least one whole packet
                // (it is sized for the largest), so parsing always frees some.
                if (c.len == sizeof(c.buf) && !netd_parse_mqtt(c)) return false;
                ssize_t n = ::recv(c.fd, c.buf + c.len, sizeof(c.buf) - c.len, 0);
                if (n == 0) return false;               // peer closed
                if (n < 0)  break;                      // would-block: drained
                c.len += (uint16_t)n;
            }
            return netd_parse_mqtt(c);
        }
    }  // namespace detail

    /** @brief The listening sockets. [0] NTP/UDP, [1] MQTT/TCP. */
    inline int* _netd_fds() { static int fd[2] = { -1, -1 }; return fd; }

    enum { NETD_MAX_CLIENTS = 4 };

    /** @brief The accepted MQTT connections, held across steps. */
    inline detail::MqttConn* _netd_clients() {
        static detail::MqttConn client[NETD_MAX_CLIENTS];
        return client;
    }

    /** @brief Close the endpoints and any accepted connection (idempotent). */
    inline void stop_mock_netd() {
        if (!_netd_running()) return;
        _netd_running() = false;
        for (int i = 0; i < 2; ++i) {
            if (_netd_fds()[i] >= 0) { ::close(_netd_fds()[i]); _netd_fds()[i] = -1; }
        }
        detail::MqttConn* client = _netd_clients();
        for (int i = 0; i < NETD_MAX_CLIENTS; ++i) {
            if (client[i].fd >= 0) { ::close(client[i].fd); client[i].fd = -1; client[i].len = 0; }
        }
    }

    /** @brief Service the loopback endpoints once, from the main loop after each
     *  beat like the field bus. Every socket here is non-blocking, so one pass is
     *  all a background peer needs and the clients retry. */
    inline void netd_step() {
        if (!_netd_running()) return;
        const int udp = _netd_fds()[0];
        const int tcp = _netd_fds()[1];
        detail::MqttConn* client = _netd_clients();

        if (udp >= 0) detail::netd_serve_ntp(udp);
        if (tcp < 0) return;

        int c = ::accept(tcp, nullptr, nullptr);
        if (c >= 0) {
            int fl = fcntl(c, F_GETFL, 0);
            fcntl(c, F_SETFL, fl | O_NONBLOCK);
            fcntl(c, F_SETFD, FD_CLOEXEC);   // drop on a reset re-exec
            int slot = -1;
            for (int i = 0; i < NETD_MAX_CLIENTS; ++i) if (client[i].fd < 0) { slot = i; break; }
            if (slot >= 0) { client[slot].fd = c; client[slot].len = 0; }
            else ::close(c);
        }
        for (int i = 0; i < NETD_MAX_CLIENTS; ++i) {
            if (client[i].fd < 0) continue;
            if (!detail::netd_serve_mqtt(client[i])) {
                ::close(client[i].fd); client[i].fd = -1; client[i].len = 0;
            }
        }
    }

    /** @brief Bind the mock endpoints and register their teardown with
     *  HAL::System::reset(). The main loop drives them through netd_step().
     *  Binding here and not later: the firmware's first NTP/MQTT query goes out
     *  ~2 ms after we return, and losing that race costs a whole retry backoff
     *  (60 s of sim time) plus two bogus boot errors. */
    inline void start_mock_netd() {
        _netd_fds()[0] = detail::netd_bind(HAL::Network::NTP_MOCK_PORT,  /*dgram=*/true);
        _netd_fds()[1] = detail::netd_bind(HAL::Network::MQTT_MOCK_PORT, /*dgram=*/false);
        // Named per endpoint: half a netd is what leaves one client stuck
        // with nothing to explain it, so a single failed bind says which.
        // stderr, like the mock broker and the self-check: the harness failing
        // to start is a fact about this host, not the greenhouse, and must not
        // reach the LOGS page or MQTT.
        if (_netd_fds()[0] < 0) {
            fprintf(stderr, "[mock-netd] NTP failed to bind\n"); fflush(stderr);
        }
        if (_netd_fds()[1] < 0) {
            fprintf(stderr, "[mock-netd] MQTT failed to bind\n"); fflush(stderr);
        }
        if (_netd_fds()[0] < 0 && _netd_fds()[1] < 0) return;
        _netd_running() = true;
        HAL::System::on_reset(&stop_mock_netd);
    }

}}

#endif // TARGET_SIMULATOR

#endif // NETWORK_SIM_NETD_H
