/**
 * @file network.cpp
 * @brief NetworkHandler / TimeSyncHandler implementations.
 *
 * The Mbed device bring-up (EthernetInterface, non-blocking connect, status polling)
 * is guarded for the OPTA build; the simulator has no network device, so NetworkHandler
 * reports the link up immediately (downstream components can be exercised, and the
 * resolver still only handles literals on the sim).
 */

#include "libbotanik/network/network.h"

#include "libbotanik/hal/platform.h"   // HAL::wall_clock_set (NTP -> RTC)

#ifdef TARGET_OPTA
#include "netsocket/EthernetInterface.h"
#include "netsocket/SocketAddress.h"
#include "netsocket/NetworkInterface.h"
#endif

namespace Botanik { namespace Network {

    namespace {
#ifdef TARGET_OPTA
        // The single wired-Ethernet device, constructed lazily on first bring-up.
        NetworkInterface* default_eth() {
            static EthernetInterface eth;
            return &eth;
        }
#endif

        // Convert an SNTP reply's transmit timestamp (bytes 40..47, big-endian: 32-bit
        // seconds since 1900 + 32-bit fraction) to UNIX milliseconds. Returns 0 if the
        // timestamp is unset or pre-1970.
        uint64_t ntp_to_unix_ms(const uint8_t* pkt) {
            uint32_t secs = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) |
                            ((uint32_t)pkt[42] << 8)  |  (uint32_t)pkt[43];
            uint32_t frac = ((uint32_t)pkt[44] << 24) | ((uint32_t)pkt[45] << 16) |
                            ((uint32_t)pkt[46] << 8)  |  (uint32_t)pkt[47];
            const uint32_t NTP_UNIX_DELTA = 2208988800u;   // seconds between 1900 and 1970
            if (secs < NTP_UNIX_DELTA) return 0;           // 0 / pre-1970 -> invalid
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
          _state(NET_IDLE), _since_ms(0), _resolver(resolver), _net(nullptr) {}

    uint32_t NetworkHandler::config_serial() const {
        const PlcIO::PlcOutputConfig_t* c = config();
        return c ? c->net.serial : 0;
    }

    void NetworkHandler::_enter(State s, uint64_t now_ms) {
        _state = s;
        _since_ms = now_ms;
    }

    void NetworkHandler::_unwire_resolver() {
        if (_resolver != nullptr) {
            _resolver->set_interface(nullptr);
            _resolver->clear();
        }
    }

    void NetworkHandler::on_config_changed(uint64_t now_ms) {
        // The net config changed: drop the resolver's view of the (old) interface and
        // its cache, then re-bring-up from the new settings (or idle if there are none).
        _unwire_resolver();
        const PlcIO::PlcOutputConfig_t* c = config();
        if (c == nullptr || c->net.serial == 0) {
#ifdef TARGET_OPTA
            if (_net != nullptr) _net->disconnect();
#endif
            _enter(NET_IDLE, now_ms);
            return;
        }
        _begin_setup(now_ms);
    }

    void NetworkHandler::_begin_setup(uint64_t now_ms) {
        const PlcIO::PlcOutputConfig_t* c = config();
#ifdef TARGET_OPTA
        if (_net == nullptr) _net = default_eth();
        _net->disconnect();              // idempotent reset before (re)configuring
        _net->set_blocking(false);       // non-blocking: connect() returns, we poll status

        if (c->net.dhcp) {
            _net->set_dhcp(true);
        } else {
            SocketAddress ip, mask, gw;
            ip.set_ip_address(c->net.ip);
            mask.set_ip_address(c->net.netmask);
            gw.set_ip_address(c->net.gateway);
            _net->set_network(ip, mask, gw);   // implicitly disables DHCP
            SocketAddress dns;
            if (c->net.dns1[0] && dns.set_ip_address(c->net.dns1)) _net->add_dns_server(dns, nullptr);
            if (c->net.dns2[0] && dns.set_ip_address(c->net.dns2)) _net->add_dns_server(dns, nullptr);
        }
        _net->connect();
        if (_logger) _logger->info(PlcIO::NETWORK, c->net.dhcp ? "network bring-up (DHCP)"
                                                              : "network bring-up (static)");
        _enter(NET_SETUP, now_ms);
#else
        // Simulator: no Mbed device. Report the link up at once so downstream components
        // can run; _net stays null, so the resolver serves literals only.
        (void)c;
        _enter(NET_RUNNING, now_ms);
        if (_resolver != nullptr) _resolver->set_interface(_net);
        if (_logger) _logger->info(PlcIO::NETWORK, "network up (sim)");
#endif
    }

    void NetworkHandler::on_step(uint64_t now_ms) {
        switch (_state) {
            case NET_IDLE:
                break;   // wait for a config to arrive (handled in on_config_changed)

            case NET_SETUP:
#ifdef TARGET_OPTA
            {
                nsapi_connection_status_t st = _net->get_connection_status();
                if (st == NSAPI_STATUS_GLOBAL_UP || st == NSAPI_STATUS_LOCAL_UP) {
                    _enter(NET_RUNNING, now_ms);
                    if (_resolver != nullptr) _resolver->set_interface(_net);
                    if (_logger) {
                        SocketAddress a;
                        if (_net->get_ip_address(&a) == NSAPI_ERROR_OK)
                            _logger->notice(PlcIO::NETWORK, "network up: %s", a.get_ip_address());
                        else
                            _logger->notice(PlcIO::NETWORK, "network up");
                    }
                } else if (now_ms - _since_ms > Config::NET_SETUP_TIMEOUT_MS) {
                    if (_logger) _logger->warning(PlcIO::NETWORK, "network bring-up timed out");
                    _enter(NET_ERROR, now_ms);
                }
            }
#endif
                break;

            case NET_RUNNING:
#ifdef TARGET_OPTA
            {
                // Watch for a dropped link; on loss, unwire the resolver and re-setup.
                nsapi_connection_status_t st = _net->get_connection_status();
                if (st != NSAPI_STATUS_GLOBAL_UP && st != NSAPI_STATUS_LOCAL_UP) {
                    if (_logger) _logger->warning(PlcIO::NETWORK, "network link lost");
                    _unwire_resolver();
                    _enter(NET_ERROR, now_ms);
                }
            }
#endif
                break;

            case NET_ERROR:
                if (now_ms - _since_ms > Config::NET_RETRY_MS) _begin_setup(now_ms);
                break;
        }
    }

    // ------------------------------------------------------------------------
    // TimeSyncHandler - non-blocking SNTP client
    // ------------------------------------------------------------------------

    TimeSyncHandler::TimeSyncHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger,
                                     NameResolver* resolver, const NetworkHandler* net)
        : ConfigHandler(ctx, logger, "TimeSyncHandler"),
          _state(TIME_IDLE), _since_ms(0), _resolver(resolver), _net(net), _server_idx(0),
          _udp(-1) {
        _server_addr.bytes[0] = _server_addr.bytes[1] = 0;
        _server_addr.bytes[2] = _server_addr.bytes[3] = 0;
    }

    uint32_t TimeSyncHandler::config_serial() const {
        const PlcIO::PlcOutputConfig_t* c = config();
        return c ? c->ntp.serial : 0;
    }

    void TimeSyncHandler::on_config_changed(uint64_t now_ms) {
        const PlcIO::PlcOutputConfig_t* c = config();
        // Restart the client with the new servers / poll interval, from the first server.
        _server_idx = 0;
        _state = (c != nullptr && c->ntp.enabled) ? TIME_RESOLVE : TIME_IDLE;
        _since_ms = now_ms;
    }

    void TimeSyncHandler::_next_server_or_error(uint64_t now_ms) {
        const PlcIO::PlcOutputConfig_t* c = config();
        if (_server_idx == 0 && c != nullptr && c->ntp.server2[0] != '\0') {
            _server_idx = 1;              // try the secondary server
            _state = TIME_RESOLVE;
            _since_ms = now_ms;
        } else {
            _state = TIME_ERROR;
            _since_ms = now_ms;
        }
    }

    void TimeSyncHandler::on_step(uint64_t now_ms) {
        const PlcIO::PlcOutputConfig_t* c = config();
        switch (_state) {
            case TIME_IDLE:
                break;

            case TIME_RESOLVE: {
                if (c == nullptr || !c->ntp.enabled) { _state = TIME_IDLE; break; }
                // Needs a live link before sending; the NetworkHandler owns that state.
                if (_net == nullptr || !_net->is_up()) break;

                const char* server = (_server_idx == 0) ? c->ntp.server1 : c->ntp.server2;
                if (server[0] == '\0') { _next_server_or_error(now_ms); break; }

                if (_resolver == nullptr) { _next_server_or_error(now_ms); break; }
                ResolveStatus rs = _resolver->resolve(server, &_server_addr);
                if (rs == RESOLVE_PENDING) break;                       // DNS in flight
                if (rs == RESOLVE_FAILED)  { _next_server_or_error(now_ms); break; }

                if (_send_query()) { _state = TIME_QUERY; _since_ms = now_ms; }
                else               { _next_server_or_error(now_ms); }
                break;
            }

            case TIME_QUERY: {
                int r = _poll_query(now_ms);
                if (r > 0) {
                    if (_logger) _logger->notice(PlcIO::TIME, "NTP synced");
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

    bool TimeSyncHandler::_send_query() {
        HAL::Net::UdpSocket s = HAL::Net::udp_open();
        if (s < 0) return false;
        _udp = s;                                  // store the handle for the poll/close
        HAL::Net::Ip4 srv;
        srv.b[0]=_server_addr.bytes[0]; srv.b[1]=_server_addr.bytes[1];
        srv.b[2]=_server_addr.bytes[2]; srv.b[3]=_server_addr.bytes[3];
        uint8_t pkt[48] = {0};
        pkt[0] = 0x23;                             // LI=0, VN=4, Mode=3 (client)
        if (!HAL::Net::udp_sendto(s, srv, 123, pkt, sizeof(pkt))) { HAL::Net::udp_close(s); _udp = -1; return false; }
        return true;
    }

    int TimeSyncHandler::_poll_query(uint64_t now_ms) {
        uint8_t pkt[48];
        HAL::Net::Ip4 from; uint16_t port = 0;
        int r = HAL::Net::udp_recvfrom(_udp, pkt, sizeof(pkt), &from, &port);
        if (r == 0) {
            if (now_ms - _since_ms > Config::NTP_QUERY_TIMEOUT_MS) { HAL::Net::udp_close(_udp); _udp = -1; return -1; }
            return 0;
        }
        if (r < (int)sizeof(pkt)) { HAL::Net::udp_close(_udp); _udp = -1; return -1; }
        uint64_t unix_ms = ntp_to_unix_ms(pkt);
        HAL::Net::udp_close(_udp); _udp = -1;
        if (unix_ms == 0) return -1;
        HAL::wall_clock_set(unix_ms);
        return 1;
    }

}}
