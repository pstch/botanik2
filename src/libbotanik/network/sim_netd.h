#ifndef NETWORK_SIM_NETD_H
#define NETWORK_SIM_NETD_H

/**
 * @file sim_netd.h
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
 * above the HAL - the HAL transport stays protocol-agnostic. The thread lifecycle
 * mirrors PlcEmulator (plcio/emulator.h): a volatile run flag published with
 * HAL::dsb(), torn down through HAL::on_reset().
 */

#ifdef TARGET_SIMULATOR

#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "libbotanik/hal/net.h"        // NTP_MOCK_PORT / MQTT_MOCK_PORT
#include "libbotanik/hal/platform.h"   // HAL::sleep_ms / on_reset / dsb
#include "libbotanik/logs/logger.h"
#include "libbotanik_config.h"

namespace Botanik { namespace Network {

    /** @brief Run flag/handle for the single responder thread. Single-writer (lifecycle)
     *  / single-reader (thread loop): volatile + HAL::dsb() to publish, the lock-free
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
            sockaddr_in a;
            memset(&a, 0, sizeof(a));
            a.sin_family      = AF_INET;
            a.sin_port        = htons(port);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) < 0) { ::close(fd); return -1; }
            if (!dgram && ::listen(fd, 4) < 0) { ::close(fd); return -1; }
            int fl = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
            return fd;
        }

        /** @brief Answer one SNTP request on @p udp with a canned reply (fixed
         *  transmit timestamp); the value only has to be a plausible post-1970
         *  time. */
        inline void netd_serve_ntp(int udp) {
            uint8_t req[48];
            sockaddr_in src;
            socklen_t sl = sizeof(src);
            ssize_t n = ::recvfrom(udp, req, sizeof(req), 0, reinterpret_cast<sockaddr*>(&src), &sl);
            if (n < 48) return;
            uint8_t rep[48];
            memset(rep, 0, sizeof(rep));
            rep[0] = 0x24;                              // LI=0, VN=4, Mode=4 (server)
            const uint32_t secs = 1735689600u + 2208988800u;   // 2025-01-01 UTC, NTP epoch
            rep[40] = (uint8_t)(secs >> 24); rep[41] = (uint8_t)(secs >> 16);
            rep[42] = (uint8_t)(secs >> 8);  rep[43] = (uint8_t)secs;
            ::sendto(udp, rep, sizeof(rep), 0, reinterpret_cast<sockaddr*>(&src), sl);
        }

        /** @brief Service one MQTT client read: CONNECT -> CONNACK (accepted),
         *  PINGREQ -> PINGRESP, everything else discarded. Returns false once the
         *  peer closes (so the caller drops the fd). */
        inline bool netd_serve_mqtt(int fd) {
            uint8_t pkt[64];
            ssize_t n = ::recv(fd, pkt, sizeof(pkt), 0);
            if (n == 0) return false;                   // peer closed
            if (n < 0)  return true;                    // would-block: keep it
            if (pkt[0] == 0x10) {                       // CONNECT
                uint8_t ack[4] = { 0x20, 0x02, 0x00, 0x00 };   // CONNACK, accepted
                ::send(fd, ack, sizeof(ack), 0);
            } else if (pkt[0] == 0xC0) {                // PINGREQ
                uint8_t pong[2] = { 0xD0, 0x00 };       // PINGRESP
                ::send(fd, pong, sizeof(pong), 0);
            }
            return true;                                // PUBLISH etc.: accepted, discarded
        }
    }  // namespace detail

    /** @brief Stop the responder thread and join it (idempotent). */
    inline void stop_mock_netd() {
        if (!_netd_running()) return;
        _netd_running() = false;
        HAL::dsb();
        pthread_join(_netd_thread(), nullptr);
    }

    /** @brief Thread entry point: bind the loopback endpoints and service them
     *  until stopped. A background peer (not realtime); the clients retry, so it
     *  need not race the main loop. */
    inline void* _netd_main(void*) {
        const int MAX_CLIENTS = 4;
        int udp = detail::netd_bind(HAL::Net::NTP_MOCK_PORT,  /*dgram=*/true);
        int tcp = detail::netd_bind(HAL::Net::MQTT_MOCK_PORT, /*dgram=*/false);
        int client[MAX_CLIENTS];
        for (int i = 0; i < MAX_CLIENTS; ++i) client[i] = -1;

        while (_netd_running()) {
            if (udp >= 0) detail::netd_serve_ntp(udp);
            if (tcp >= 0) {
                int c = ::accept(tcp, nullptr, nullptr);
                if (c >= 0) {
                    int fl = fcntl(c, F_GETFL, 0);
                    fcntl(c, F_SETFL, fl | O_NONBLOCK);
                    int slot = -1;
                    for (int i = 0; i < MAX_CLIENTS; ++i) if (client[i] < 0) { slot = i; break; }
                    if (slot >= 0) client[slot] = c; else ::close(c);
                }
                for (int i = 0; i < MAX_CLIENTS; ++i) {
                    if (client[i] < 0) continue;
                    if (!detail::netd_serve_mqtt(client[i])) { ::close(client[i]); client[i] = -1; }
                }
            }
            HAL::sleep_ms(Config::TASK_PERIOD_MS);
        }

        if (udp >= 0) ::close(udp);
        if (tcp >= 0) ::close(tcp);
        for (int i = 0; i < MAX_CLIENTS; ++i) if (client[i] >= 0) ::close(client[i]);
        return nullptr;
    }

    /** @brief Spawn the mock responder thread and register its teardown with
     *  HAL::reset(). Start it before the process locks memory (mlockall
     *  MCL_FUTURE), like the emulator. */
    inline void start_mock_netd(Logs::Logger* logger = nullptr) {
        _netd_running() = true;
        HAL::dsb();
        if (pthread_create(&_netd_thread(), nullptr, &_netd_main, nullptr) != 0) {
            _netd_running() = false;
            if (logger != nullptr) logger->error(PlcIO::NETWORK, "mock netd failed to start");
            return;
        }
        HAL::on_reset(&stop_mock_netd);
    }

}}

#endif // TARGET_SIMULATOR

#endif // NETWORK_SIM_NETD_H
