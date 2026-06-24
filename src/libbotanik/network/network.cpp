/**
 * @file network.cpp
 * @brief NetworkHandler / NtpClient implementations.
 *
 * Bring-up goes through HAL::Net (Mbed vs. BSD handled by the backend). On the
 * sim link_status() returns LINK_UP immediately, so NET_SETUP -> NET_RUNNING
 * next step.
 */

#include <cstring>

#include "libbotanik/network/network.h"

#include "libbotanik/hal/platform.h"   // HAL::Clock::set_wall_ms (NTP -> RTC)

namespace Botanik { namespace Network {

    namespace {
        // Convert an SNTP reply's transmit timestamp (bytes 40..47, big-endian:
        // 32-bit seconds since 1900 + 32-bit fraction) to UNIX milliseconds.
        // Returns 0 if the timestamp is unset or pre-1970.
        uint64_t ntp_to_unix_ms(const uint8_t* pkt) {
            uint32_t secs = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) |
                            ((uint32_t)pkt[42] << 8)  |  (uint32_t)pkt[43];
            uint32_t frac = ((uint32_t)pkt[44] << 24) | ((uint32_t)pkt[45] << 16) |
                            ((uint32_t)pkt[46] << 8)  |  (uint32_t)pkt[47];
            // seconds between 1900 and 1970
            const uint32_t NTP_UNIX_DELTA = 2208988800u;
            if (secs < NTP_UNIX_DELTA) return 0;   // 0 / pre-1970 -> invalid
            uint64_t unix_s = (uint64_t)secs - NTP_UNIX_DELTA;
            uint32_t ms = (uint32_t)(((uint64_t)frac * 1000u) >> 32);
            return unix_s * 1000ull + ms;
        }
    }  // namespace

    // ------------------------------------------------------------------------
    // NetworkHandler
    // ------------------------------------------------------------------------

    NetworkHandler::NetworkHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger,
                                   NameResolver* resolver)
        : ConfigHandler(ctx, logger, "NetworkHandler"),
          _state(NET_IDLE), _since_ms(0), _resolver(resolver) {}

    uint32_t NetworkHandler::config_serial() const {
        const PlcIO::PlcOutputConfig_t* c = config();
        return c ? c->net.serial : 0;
    }

    const char* NetworkHandler::status_str() const {
        switch (_state) {
            case NET_SETUP:   return "setup";
            case NET_RUNNING: return "online";
            case NET_ERROR:   return "error";
            default:          return "down";
        }
    }

    void NetworkHandler::_enter(State s, uint64_t now_ms) {
        _state = s;
        _since_ms = now_ms;
    }

    void NetworkHandler::_unwire_resolver() {
        if (_resolver != nullptr) _resolver->clear();
    }

    void NetworkHandler::on_config_changed(uint64_t now_ms) {
        // Clear the resolver cache, then re-bring-up (or idle).
        _unwire_resolver();
        const PlcIO::PlcOutputConfig_t* c = config();
        if (c == nullptr || c->net.serial == 0) {
            HAL::Network::link_disconnect();
            _enter(NET_IDLE, now_ms);
            return;
        }
        _begin_setup(now_ms);
    }

    void NetworkHandler::_begin_setup(uint64_t now_ms) {
        const PlcIO::PlcOutputConfig_t* c = config();
        HAL::Network::LinkConfig lc;
        memset(&lc, 0, sizeof(lc));
        lc.dhcp = c->net.dhcp != 0;
        if (!lc.dhcp) {
            strncpy(lc.ip,      c->net.ip,      sizeof(lc.ip) - 1);
            strncpy(lc.netmask, c->net.netmask, sizeof(lc.netmask) - 1);
            strncpy(lc.gateway, c->net.gateway, sizeof(lc.gateway) - 1);
            strncpy(lc.dns1,    c->net.dns1,    sizeof(lc.dns1) - 1);
            strncpy(lc.dns2,    c->net.dns2,    sizeof(lc.dns2) - 1);
        }
        HAL::Network::link_configure(lc);
        if (_logger) _logger->info(PlcIO::NETWORK, lc.dhcp ? "network bring-up (DHCP)"
                                                           : "network bring-up (static)");
        _enter(NET_SETUP, now_ms);
    }

    void NetworkHandler::on_step(uint64_t now_ms) {
        switch (_state) {
            case NET_IDLE:
                // wait for a config to arrive (handled in on_config_changed)
                break;

            case NET_SETUP:
                if (HAL::Network::link_status() == HAL::Network::LINK_UP) {
                    _enter(NET_RUNNING, now_ms);
                    HAL::Network::Ip4 a;
                    if (_logger) {
                        if (HAL::Network::link_ip(&a))
                            _logger->notice(PlcIO::NETWORK, "network up: %u.%u.%u.%u",
                                            a.b[0], a.b[1], a.b[2], a.b[3]);
                        else
                            _logger->notice(PlcIO::NETWORK, "network up");
                    }
                } else if (now_ms - _since_ms > Config::NET_SETUP_TIMEOUT_MS) {
                    if (_logger) _logger->warning(PlcIO::NETWORK, "network bring-up timed out");
                    _enter(NET_ERROR, now_ms);
                }
                break;

            case NET_RUNNING:
                if (HAL::Network::link_status() != HAL::Network::LINK_UP) {
                    if (_logger) _logger->warning(PlcIO::NETWORK, "network link lost");
                    _unwire_resolver();
                    _enter(NET_ERROR, now_ms);
                }
                break;

            case NET_ERROR:
                if (now_ms - _since_ms > Config::NET_RETRY_MS) _begin_setup(now_ms);
                break;
        }
    }

    // ------------------------------------------------------------------------
    // NtpClient - non-blocking SNTP client
    // ------------------------------------------------------------------------

    NtpClient::NtpClient(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger,
                         NameResolver* resolver, const NetworkHandler* net)
        : ConfigHandler(ctx, logger, "NtpClient"),
          _state(TIME_IDLE), _since_ms(0), _resolver(resolver), _net(net), _server_idx(0),
          _udp(-1), _offset_ms(0), _sync_held(false) {
        _server_addr.bytes[0] = _server_addr.bytes[1] = 0;
        _server_addr.bytes[2] = _server_addr.bytes[3] = 0;
    }

    uint32_t NtpClient::config_serial() const {
        const PlcIO::PlcOutputConfig_t* c = config();
        return c ? c->ntp.serial : 0;
    }

    const char* NtpClient::status_str() const {
        switch (_state) {
            case TIME_RESOLVE: return "resolving";
            case TIME_QUERY:   return "querying";
            case TIME_SYNCED:  return "synced";
            case TIME_ERROR:   return "error";
            default:           return "off";
        }
    }

    void NtpClient::on_config_changed(uint64_t now_ms) {
        if (_udp >= 0) { HAL::Network::udp_close(_udp); _udp = -1; }
        const PlcIO::PlcOutputConfig_t* c = config();
        // Restart the client with the new servers / poll interval, from the
        // first server. A reconfig is a fresh start: the next sync is "initial".
        _server_idx = 0;
        _sync_held = false;
        _state = (c != nullptr && c->ntp.enabled) ? TIME_RESOLVE : TIME_IDLE;
        _since_ms = now_ms;
    }

    void NtpClient::_next_server_or_error(uint64_t now_ms) {
        const PlcIO::PlcOutputConfig_t* c = config();
        if (_server_idx == 0 && c != nullptr && c->ntp.server2[0] != '\0') {
            _server_idx = 1;              // try the secondary server
            _state = TIME_RESOLVE;
            _since_ms = now_ms;
        } else {
            _state = TIME_ERROR;
            _sync_held = false;       // sync lost: the next success is a recovery
            _since_ms = now_ms;
        }
    }

    void NtpClient::on_step(uint64_t now_ms) {
        const PlcIO::PlcOutputConfig_t* c = config();
        switch (_state) {
            case TIME_IDLE:
                break;

            case TIME_RESOLVE: {
                if (c == nullptr || !c->ntp.enabled) {
                    _state = TIME_IDLE; _sync_held = false; break;
                }
                // Needs a live link before sending; the NetworkHandler owns
                // that state.
                if (_net == nullptr || !_net->is_up()) break;

                const char* server = (_server_idx == 0) ? c->ntp.server1 : c->ntp.server2;
                if (server[0] == '\0') { _next_server_or_error(now_ms); break; }

                if (_resolver == nullptr) { _next_server_or_error(now_ms); break; }
                ResolveStatus rs = _resolver->resolve(server, &_server_addr);
                if (rs == RESOLVE_PENDING) break;   // DNS in flight
                if (rs == RESOLVE_FAILED)  { _next_server_or_error(now_ms); break; }

                if (_send_query()) { _state = TIME_QUERY; _since_ms = now_ms; }
                else               { _next_server_or_error(now_ms); }
                break;
            }

            case TIME_QUERY: {
                int r = _poll_query(now_ms);
                if (r > 0) {
                    // Announce only the initial / recovery sync, not the routine
                    // re-poll (it fires every poll_s and would spam the log).
                    if (!_sync_held && _logger) _logger->notice(PlcIO::TIME, "NTP synced");
                    _sync_held = true;
                    _state = TIME_SYNCED; _since_ms = now_ms;
                } else if (r < 0) {
                    if (_logger) _logger->warning(PlcIO::TIME, "NTP query failed");
                    _next_server_or_error(now_ms);
                }
                break;
            }

            case TIME_SYNCED: {
                uint32_t poll_s = (c != nullptr && c->ntp.poll_s) ? c->ntp.poll_s
                                                                  : Config::NTP_POLL_DEFAULT_S;
                if (now_ms - _since_ms > (uint64_t)poll_s * 1000ull) {
                    _server_idx = 0;
                    _state = TIME_RESOLVE; _since_ms = now_ms;
                }
                break;
            }

            case TIME_ERROR:
                if (now_ms - _since_ms > Config::NTP_RETRY_MS) {
                    _server_idx = 0;
                    _state = TIME_RESOLVE; _since_ms = now_ms;
                }
                break;
        }
    }

    bool NtpClient::_send_query() {
        HAL::Network::UdpSocket s = HAL::Network::udp_open();
        if (s < 0) return false;
        _udp = s;   // store the handle for the poll/close
        HAL::Network::Ip4 srv;
        srv.b[0]=_server_addr.bytes[0]; srv.b[1]=_server_addr.bytes[1];
        srv.b[2]=_server_addr.bytes[2]; srv.b[3]=_server_addr.bytes[3];
        uint8_t pkt[48] = {0};
        pkt[0] = 0x23;   // LI=0, VN=4, Mode=3 (client)
        if (!HAL::Network::udp_sendto(s, srv, 123, pkt, sizeof(pkt))) {
            HAL::Network::udp_close(s); _udp = -1; return false;
        }
        return true;
    }

    int NtpClient::_poll_query(uint64_t now_ms) {
        uint8_t pkt[48];
        HAL::Network::Ip4 from; uint16_t port = 0;
        int r = HAL::Network::udp_recvfrom(_udp, pkt, sizeof(pkt), &from, &port);
        if (r == 0) {
            if (now_ms - _since_ms > Config::NTP_QUERY_TIMEOUT_MS) {
                HAL::Network::udp_close(_udp); _udp = -1; return -1;
            }
            return 0;
        }
        if (r < (int)sizeof(pkt)) { HAL::Network::udp_close(_udp); _udp = -1; return -1; }
        uint64_t unix_ms = ntp_to_unix_ms(pkt);
        HAL::Network::udp_close(_udp); _udp = -1;
        if (unix_ms == 0) return -1;
        // Offset = correction about to be applied (pre-sync clock vs. server).
        // A cold-start clock can be years off, so saturate to int32 rather than
        // wrap (after the first sync it is small).
        int64_t off = (int64_t)unix_ms - (int64_t)HAL::Clock::wall_ms();
        if (off >  2147483647LL) off =  2147483647LL;
        if (off < -2147483647LL) off = -2147483647LL;
        _offset_ms = (int32_t)off;
        HAL::Clock::set_wall_ms(unix_ms);
        return 1;
    }

}}
