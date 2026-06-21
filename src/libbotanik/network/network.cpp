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

#ifdef TARGET_OPTA
#include "netsocket/EthernetInterface.h"
#include "netsocket/SocketAddress.h"
#include "netsocket/NetworkInterface.h"
#endif

namespace Botanik { namespace Network {

#ifdef TARGET_OPTA
    namespace {
        // The single wired-Ethernet device, constructed lazily on first bring-up (so it
        // is not built during static init, before the Mbed stack is ready).
        NetworkInterface* default_eth() {
            static EthernetInterface eth;
            return &eth;
        }
    }  // namespace
#endif

    // ------------------------------------------------------------------------
    // NetworkHandler
    // ------------------------------------------------------------------------

    NetworkHandler::NetworkHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger,
                                   NameResolver* resolver)
        : ConfigHandler(ctx, logger),
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
    // TimeSyncHandler (WIP - config-watch + structure; NTP exchange still to write)
    // ------------------------------------------------------------------------

    TimeSyncHandler::TimeSyncHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger,
                                     NameResolver* resolver)
        : ConfigHandler(ctx, logger),
          _state(TIME_IDLE), _since_ms(0), _resolver(resolver) {}

    uint32_t TimeSyncHandler::config_serial() const {
        const PlcIO::PlcOutputConfig_t* c = config();
        return c ? c->ntp.serial : 0;
    }

    void TimeSyncHandler::on_config_changed(uint64_t now_ms) {
        const PlcIO::PlcOutputConfig_t* c = config();
        // Restart the NTP client with the new servers / poll interval.
        _state = (c != nullptr && c->ntp.enabled) ? TIME_RESOLVE : TIME_IDLE;
        _since_ms = now_ms;
    }

    void TimeSyncHandler::on_step(uint64_t now_ms) {
        (void)now_ms;
        // TODO: drive the NTP state machine -
        //   TIME_RESOLVE: _resolver->resolve(ntp.server1/2) -> on OK, TIME_QUERY
        //   TIME_QUERY:   NTP exchange over a UDP socket on the live interface
        //   on success:   discipline the RTC (HAL::wall_clock_set), TIME_SYNCED
        //   TIME_ERROR:   back off (ntp.poll_s / a retry) and retry
    }

}}
