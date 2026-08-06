// REVIEW_35: TODO
/**
 * @file ntp.cpp
 * @brief NtpClient implementation - non-blocking SNTP client.
 *
 * Resolves a server through the shared NameResolver, runs a non-blocking SNTP
 * exchange over HAL::Net UDP, and disciplines the RTC (HAL::Clock::set_wall_ms).
 * Gated on NetworkHandler::is_up().
 */

#include "libbotanik/network/ntp.h"   // NtpClient - what this file implements

#include "libbotanik/hal/platform.h"  // HAL::Clock::set_wall_ms - the reply lands on the RTC here

namespace Botanik { namespace Network {

    NtpClient::NtpClient(const Vars::Bank* cfg, Logs::Logger* logger,
                         NameResolver* resolver, const NetworkHandler* net)
        : Component(logger, "NtpClient"), _cfg(cfg),
          _state(TIME_IDLE), _since_ms(0), _resolver(resolver), _net(net), _server_idx(0),
          _udp(-1), _offset_ms(0), _sync_held(false) {
        _server_addr = HAL::Network::Ip4();   // 0.0.0.0 until resolved
    }

    uint32_t NtpClient::param_serial() const {
        return Vars::group_serial(_cfg, "ntp");
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

    void NtpClient::on_param_changed(uint64_t now_ms) {
        if (_udp >= 0) { HAL::Network::udp_close(_udp); _udp = -1; }
        // Restart the client with the new servers / poll interval, from the
        // first server. A reconfig is a fresh start: the next sync is "initial".
        _server_idx = 0;
        _sync_held = false;
        _state = Vars::get_int(_cfg, "ntp_enable") ? TIME_RESOLVE : TIME_IDLE;
        _since_ms = now_ms;
    }

    void NtpClient::resync() {
        // Drop any in-flight query and restart from the primary server now. The
        // next on_step() picks it up; if NTP is disabled it falls back to IDLE.
        if (_udp >= 0) { HAL::Network::udp_close(_udp); _udp = -1; }
        _server_idx = 0;
        _state      = TIME_RESOLVE;
        _since_ms   = HAL::Clock::uptime_ms();
    }

    void NtpClient::_next_server_or_error(uint64_t now_ms) {
        if (_server_idx == 0 && Vars::get_str(_cfg, "ntp_srv2")[0] != '\0') {
            _server_idx = 1;              // try the secondary server
            _state = TIME_RESOLVE;
            _since_ms = now_ms;
        } else {
            if (_logger) _logger->error(Logs::TIME, "NTP sync failed (no server answered)");
            _state = TIME_ERROR;
            _sync_held = false;       // sync lost: the next success is a recovery
            _since_ms = now_ms;
        }
    }

    void NtpClient::on_step(uint64_t now_ms) {
        switch (_state) {
            case TIME_IDLE:
                break;

            case TIME_RESOLVE: {
                if (!Vars::get_int(_cfg, "ntp_enable")) {
                    _state = TIME_IDLE; _sync_held = false; break;
                }
                // Needs a live link before sending; the NetworkHandler owns
                // that state.
                if (_net == nullptr || !_net->is_up()) break;

                const char* server = Vars::get_str(_cfg, _server_idx == 0 ? "ntp_srv1" : "ntp_srv2");
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
                    if (!_sync_held && _logger) {
                        char ab[16];
                        _logger->notice(Logs::TIME, "NTP synced (%s:%u)",
                                        HAL::Network::ip4_to_str(_server_addr, ab, sizeof(ab)),
                                        (unsigned)server_port());
                    }
                    _sync_held = true;
                    _state = TIME_SYNCED; _since_ms = now_ms;
                } else if (r < 0) {
                    if (_logger) _logger->warning(Logs::TIME, "NTP query failed");
                    _next_server_or_error(now_ms);
                }
                break;
            }

            case TIME_SYNCED: {
                uint32_t poll_s = (uint32_t)Vars::get_int(_cfg, "ntp_poll_sec");
                if (poll_s == 0) poll_s = Config::NTP_POLL_DEFAULT_S;
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
        uint8_t pkt[48] = {0};
        pkt[0] = 0x23;   // LI=0, VN=4, Mode=3 (client)
        if (!HAL::Network::udp_sendto(s, _server_addr, Config::NTP_PORT, pkt, sizeof(pkt))) {
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
