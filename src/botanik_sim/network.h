// REVIEW_35: TODO
#ifndef BOTANIK_SIM_NETWORK_H
#define BOTANIK_SIM_NETWORK_H

/**
 * @file network.h
 * @brief Simulator-only mock NTP/MQTT responder.
 *
 * Header-only and guarded by TARGET_SIMULATOR (a near-empty include on OPTA,
 * where the real network plays this role). It runs a tiny cooperative peer on
 * loopback so the firmware's real NtpClient/MqttClient state machines reach
 * SYNCED/READY in the sim without touching the public network: the HAL resolves
 * every name to 127.0.0.1 and routes loopback NTP to NTP_MOCK_PORT, and this
 * responder answers there and on MQTT_MOCK_PORT (the sim's broker port).
 *
 * The NTP/MQTT wire knowledge (the SNTP reply, the CONNACK/PINGRESP) lives here,
 * above the HAL - the HAL transport stays protocol-agnostic. The thread lifecycle is
 * a volatile run flag published with HAL::Memory::dsb(), torn down through
 * HAL::System::on_reset().
 */

#ifdef TARGET_SIMULATOR

#include <cstdint>                      // uint8_t/uint32_t/uint64_t
#include <cstdio>                       // fprintf(stderr)
#include <cstring>                      // memcpy / memset
#include <ctime>                        // time()

// POSIX - the mock responder is a real in-process socket peer, not a stub.
#include <fcntl.h>                      // fcntl / O_NONBLOCK
#include <netinet/in.h>                 // sockaddr_in / htons
#include <pthread.h>                    // pthread_t
#include <sys/socket.h>                 // socket / bind / recvfrom / sendto
#include <unistd.h>                     // close

#include "libbotanik_config.h"          // Config::NET_*

#include "libbotanik/hal/net.h"         // HAL::Network::NTP_MOCK_PORT / MQTT_MOCK_PORT
#include "libbotanik/hal/platform.h"    // HAL::Clock::sleep_ms / HAL::Memory::dsb
#include "libbotanik/logs/logger.h"     // Logs::Logger

namespace Botanik { namespace Network {

    /** @brief Run flag/handle for the single responder thread. Single-writer (lifecycle)
     *  / single-reader (thread loop): volatile + HAL::Memory::dsb() to publish, the lock-free
     *  pattern used throughout (no std::atomic). */
    inline volatile bool& _netd_running() { static volatile bool running = false; return running; }
    inline pthread_t&      _netd_thread()  { static pthread_t thread; return thread; }

    namespace detail {
        /** @brief A non-blocking loopback socket bound to @p port (UDP if @p
         *  dgram, else a listening TCP socket). -1 on failure. */
        inline int netd_bind(uint16_t port, bool dgram) {
            int fd = ::socket(AF_INET, dgram ? SOCK_DGRAM : SOCK_STREAM, 0);
            if (fd < 0) return -1;
            int one = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            // Bind this instance's per-process loopback address (matches what
            // the HAL resolver hands the firmware) so concurrent sims don't
            // share the port on 127.0.0.1.
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

        /** @brief One MQTT client: its fd plus a reassembly buffer. TCP is a
         *  byte stream, so a single read can carry several small packets coalesced
         *  or split a large one; buffering lets us trace + answer each whole
         *  packet (the publisher's fields would otherwise show up fragmented). */
        struct MqttConn {
            int      fd  = -1;
            uint8_t  buf[256];
            uint16_t len = 0;
        };

        /** @brief Answer one whole MQTT packet: CONNECT -> CONNACK (accepted),
         *  PINGREQ -> PINGRESP, everything else (PUBLISH, ...) accepted + dropped. */
        inline void netd_answer_mqtt(int fd, const uint8_t* pkt, size_t n) {
            netd_trace_mqtt(pkt, (ssize_t)n);
            if ((pkt[0] & 0xF0) == 0x10) {              // CONNECT
                uint8_t ack[4] = { 0x20, 0x02, 0x00, 0x00 };   // CONNACK, accepted
                ::send(fd, ack, sizeof(ack), 0);
            } else if ((pkt[0] & 0xF0) == 0xC0) {       // PINGREQ
                uint8_t pong[2] = { 0xD0, 0x00 };       // PINGRESP
                ::send(fd, pong, sizeof(pong), 0);
            }
        }

        /** @brief Read what's available, then trace + answer every complete MQTT
         *  packet now buffered, leaving any partial tail for the next call.
         *  Returns false once the peer closes (so the caller drops the conn). */
        inline bool netd_serve_mqtt(MqttConn& c) {
            // Append into the reassembly buffer (drop a stuck full buffer rather
            // than read 0 bytes and misread it as a close).
            if (c.len >= sizeof(c.buf)) c.len = 0;
            ssize_t n = ::recv(c.fd, c.buf + c.len, sizeof(c.buf) - c.len, 0);
            if (n == 0) return false;                   // peer closed
            if (n > 0)  c.len += (uint16_t)n;           // n < 0: would-block, parse what we have

            // Parse as many whole packets as the buffer holds. MQTT fixed header:
            // type byte + a 1..4 byte remaining-length varint + that many bytes.
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
    }  // namespace detail

    /** @brief Stop the responder thread and join it (idempotent). */
    inline void stop_mock_netd() {
        if (!_netd_running()) return;
        _netd_running() = false;
        HAL::Memory::dsb();
        pthread_join(_netd_thread(), nullptr);
    }

    /** @brief The listening sockets, bound by start_mock_netd() before the
     *  thread exists (see there). [0] NTP/UDP, [1] MQTT/TCP. */
    inline int* _netd_fds() { static int fd[2] = { -1, -1 }; return fd; }

    /** @brief Thread entry point: service the loopback endpoints until stopped.
     *  A background peer (not realtime); the clients retry, so it need not race
     *  the main loop. */
    inline void* _netd_main(void*) {
        const int MAX_CLIENTS = 4;
        int udp = _netd_fds()[0];
        int tcp = _netd_fds()[1];
        detail::MqttConn client[MAX_CLIENTS];

        while (_netd_running()) {
            if (udp >= 0) detail::netd_serve_ntp(udp);
            if (tcp >= 0) {
                int c = ::accept(tcp, nullptr, nullptr);
                if (c >= 0) {
                    int fl = fcntl(c, F_GETFL, 0);
                    fcntl(c, F_SETFL, fl | O_NONBLOCK);
                    fcntl(c, F_SETFD, FD_CLOEXEC);   // drop on a reset re-exec
                    int slot = -1;
                    for (int i = 0; i < MAX_CLIENTS; ++i) if (client[i].fd < 0) { slot = i; break; }
                    if (slot >= 0) { client[slot].fd = c; client[slot].len = 0; }
                    else ::close(c);
                }
                for (int i = 0; i < MAX_CLIENTS; ++i) {
                    if (client[i].fd < 0) continue;
                    if (!detail::netd_serve_mqtt(client[i])) {
                        ::close(client[i].fd); client[i].fd = -1; client[i].len = 0;
                    }
                }
            }
            HAL::Clock::sleep_ms(Config::TASK_PERIOD_MS);
        }

        if (udp >= 0) ::close(udp);
        if (tcp >= 0) ::close(tcp);
        for (int i = 0; i < MAX_CLIENTS; ++i) if (client[i].fd >= 0) ::close(client[i].fd);
        return nullptr;
    }

    /** @brief Spawn the mock responder thread and register its teardown with
     *  HAL::System::reset(). Start it before the process locks memory (mlockall
     *  MCL_FUTURE), like the emulator. */
    inline void start_mock_netd(Logs::Logger* logger = nullptr) {
        // Bind here rather than in the thread: the firmware's first NTP/MQTT
        // query goes out ~2 ms after we return, and losing that race cost a
        // whole retry backoff (60 s of sim time) plus two bogus boot errors.
        _netd_fds()[0] = detail::netd_bind(HAL::Network::NTP_MOCK_PORT,  /*dgram=*/true);
        _netd_fds()[1] = detail::netd_bind(HAL::Network::MQTT_MOCK_PORT, /*dgram=*/false);
        _netd_running() = true;
        HAL::Memory::dsb();
        if (pthread_create(&_netd_thread(), nullptr, &_netd_main, nullptr) != 0) {
            _netd_running() = false;
            if (logger != nullptr) logger->error(Logs::NETWORK, "mock netd failed to start");
            return;
        }
        HAL::System::on_reset(&stop_mock_netd);
    }

}}

#endif // TARGET_SIMULATOR

#endif // NETWORK_SIM_NETD_H
