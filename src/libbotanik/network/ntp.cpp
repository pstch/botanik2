/**
 * @file ntp.cpp
 * @brief NtpClient implementation - non-blocking SNTP client.
 *
 * Resolves a server through the shared NameResolver, runs a non-blocking SNTP
 * exchange over HAL::Net UDP, and disciplines the RTC (HAL::Clock::set_wall_ms).
 * Gated on NetworkHandler::is_up().
 */

#include "libbotanik/tasks/tasks.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_NTP
#include "libbotanik/network/ntp.h"

#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Network {

    NtpClient::NtpClient(const SetupContext& ctx,
                         NameResolver* resolver, const NetworkHandler* net)
        : Component(ctx, "NtpClient"),
          _state(TIME_IDLE), _since_ms(0), _resolver(resolver), _net(net), _server_idx(0),
          _udp(-1), _offset_ms(0), _sync_held(false) {
        _server_addr = HAL::Network::Ip4();   // 0.0.0.0 until resolved
    }

    void NtpClient::set_config(const NtpConfig& cfg) {
        _cfg = cfg;
        _pending = true;
    }

    uint16_t NtpClient::server_port() const { return _cfg.port; }

    const char* NtpClient::status_str() const {
        switch (_state) {
            case TIME_RESOLVE: return "resolving";
            case TIME_QUERY:   return "querying";
            case TIME_SYNCED:  return "synced";
            case TIME_ERROR:   return "error";
            default:           return "off";
        }
    }

    void NtpClient::resync() {
        // Drop any in-flight query and restart from the primary server now. The
        // next on_service() picks it up; if NTP is disabled it falls back to IDLE.
        if (_udp >= 0) { HAL::Network::udp_close(_udp); _udp = -1; }
        _server_idx = 0;
        _state      = TIME_RESOLVE;
        _since_ms   = HAL::Clock::uptime_ms();
    }

    void NtpClient::_next_server_or_error(uint64_t now_ms) {
        if (_server_idx == 0 && _cfg.srv2[0] != '\0') {
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

    void NtpClient::on_service(uint64_t now_ms) {
        // A config landed at CONFIG, earlier in this same beat. New servers or
        // cadence: drop any in-flight query and start over from the primary.
        // A reconfig is a fresh start, so the next sync is "initial" again.
        if (_pending) {
            _pending = false;
            if (_udp >= 0) { HAL::Network::udp_close(_udp); _udp = -1; }
            _server_idx = 0;
            _sync_held  = false;
            _state      = _cfg.enabled ? TIME_RESOLVE : TIME_IDLE;
            _since_ms   = now_ms;
        }

        switch (_state) {
            case TIME_IDLE:
                break;

            case TIME_RESOLVE: {
                if (!_cfg.enabled) {
                    _state = TIME_IDLE; _sync_held = false; break;
                }
                // Needs a live link before sending; the NetworkHandler owns
                // that state.
                if (_net == nullptr || !_net->is_up()) break;

                const char* server = (_server_idx == 0) ? _cfg.srv1 : _cfg.srv2;
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
                // 0 is "stop polling", and it is reachable: the band starts at
                // 10, but an override is deliberately unbanded so a maintenance
                // session can take the client off a server. One sync then, and
                // no more - not a query every beat, which is what substituting
                // a default here would have to pretend was asked for.
                const uint32_t poll_s = _cfg.poll_sec;
                if (poll_s == 0) break;
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
        if (!HAL::Network::udp_sendto(s, _server_addr, server_port(), pkt, sizeof(pkt))) {
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
        // Short: not an SNTP reply, so not ours - the same answer as a wrong
        // source below, and for the same reason. Failing the query here sent
        // the client to its secondary and then into a 60 s backoff on someone
        // else's stray datagram, which is exactly what the comment three lines
        // down says must not happen. A genuine socket error is r < 0, which
        // udp_recvfrom() reports separately and which still fails.
        if (r < (int)sizeof(pkt)) return 0;

        // Only the server we asked, and only an answer shaped like one. The
        // socket is an unbound ephemeral UDP port, so without this ANY host that
        // lands a 48-byte datagram inside the query window sets the RTC below,
        // moving every log timestamp on the unit.
        //
        // Not a spoofing defence (unauthenticated NTP cannot be one): it is the
        // difference between "a peer we addressed" and "anything at all".
        const bool from_server =
            (from.b[0] == _server_addr.b[0] && from.b[1] == _server_addr.b[1] &&
             from.b[2] == _server_addr.b[2] && from.b[3] == _server_addr.b[3] &&
             port == server_port());
        const bool is_reply = ((pkt[0] & 0x07u) == 4u);   // mode 4: server
        if (!from_server || !is_reply) {
            // Not ours: drop the datagram and keep waiting out the window,
            // rather than failing the query on someone else's traffic.
            return 0;
        }

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

namespace Botanik { namespace Network {


    void NtpClient::register_tasks() {
        _tasks->add(*this, "sys_ntp", Tasks::EVERY_BEAT,
              Tasks::PHASE_SERVICE);
    }

}}  // namespace Botanik::Network

#endif // BOTANIK_FEATURE_NTP
