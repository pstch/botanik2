// REVIEW_35: TODO
/**
 * @file network.cpp
 * @brief NetworkHandler implementation. (The NTP client lives in ntp.cpp.)
 *
 * Bring-up goes through HAL::Net (Mbed vs. BSD handled by the backend). On the
 * sim link_status() returns LINK_UP immediately, so NET_SETUP -> NET_RUNNING
 * next step.
 */

#include <cstring>

#include "libbotanik/network/network.h"

#include "libbotanik/hal/platform.h"   // HAL::Clock::set_wall_ms (NTP -> RTC)

namespace Botanik { namespace Network {

    // ------------------------------------------------------------------------
    // NetworkHandler
    // ------------------------------------------------------------------------

    NetworkHandler::NetworkHandler(const Params::ParamHandler* cfg, Logs::Logger* logger,
                                   NameResolver* resolver)
        : Component(logger, "NetworkHandler"), _cfg(cfg),
          _state(NET_IDLE), _since_ms(0), _resolver(resolver) {}

    uint32_t NetworkHandler::param_serial() const {
        return Params::group_serial(_cfg, "net");
    }

    const char* NetworkHandler::status_str() const {
        switch (_state) {
            case NET_SETUP:   return "setup";
            case NET_RUNNING: return "online";
            case NET_ERROR:   return "error";
            default:          return "down";
        }
    }

    void NetworkHandler::_enter(State s, uint64_t now_ms) { _state = s; _since_ms = now_ms; }

    void NetworkHandler::_unwire_resolver() {
        if (_resolver != nullptr) _resolver->clear();
    }

    void NetworkHandler::on_param_changed(uint64_t now_ms) {
        // Clear the resolver cache, then re-bring-up (or idle).
        _unwire_resolver();
        if (Params::group_serial(_cfg, "net") == 0) {
            HAL::Network::link_disconnect();
            _enter(NET_IDLE, now_ms);
            return;
        }
        _begin_setup(now_ms);
    }

    void NetworkHandler::_begin_setup(uint64_t now_ms) {
        HAL::Network::LinkConfig lc;
        memset(&lc, 0, sizeof(lc));
        lc.dhcp = Params::get_int(_cfg, "net_dhcp") != 0;
        if (!lc.dhcp) {
            strncpy(lc.ip,      Params::get_str(_cfg, "net_ip"),      sizeof(lc.ip) - 1);
            strncpy(lc.netmask, Params::get_str(_cfg, "net_netmask"), sizeof(lc.netmask) - 1);
            strncpy(lc.gateway, Params::get_str(_cfg, "net_gateway"), sizeof(lc.gateway) - 1);
            strncpy(lc.dns1,    Params::get_str(_cfg, "net_dns1"),    sizeof(lc.dns1) - 1);
            strncpy(lc.dns2,    Params::get_str(_cfg, "net_dns2"),    sizeof(lc.dns2) - 1);
        }
        HAL::Network::link_configure(lc);
        if (_logger) _logger->info(Logs::NETWORK, lc.dhcp ? "network bring-up (DHCP)"
                                                           : "network bring-up (static)");
        _enter(NET_SETUP, now_ms);
    }

    void NetworkHandler::on_step(uint64_t now_ms) {
        switch (_state) {
            case NET_IDLE:
                // wait for a config to arrive (handled in on_param_changed)
                break;

            case NET_SETUP:
                if (HAL::Network::link_status() == HAL::Network::LINK_UP) {
                    _enter(NET_RUNNING, now_ms);
                    HAL::Network::Ip4 a;
                    if (_logger) {
                        if (HAL::Network::link_ip(&a))
                            _logger->notice(Logs::NETWORK, "network up: %u.%u.%u.%u",
                                            a.b[0], a.b[1], a.b[2], a.b[3]);
                        else
                            _logger->notice(Logs::NETWORK, "network up");
                    }
                } else if (now_ms - _since_ms > Config::NET_SETUP_TIMEOUT_MS) {
                    if (_logger) _logger->warning(Logs::NETWORK, "network bring-up timed out");
                    _enter(NET_ERROR, now_ms);
                }
                break;

            case NET_RUNNING:
                if (HAL::Network::link_status() != HAL::Network::LINK_UP) {
                    if (_logger) _logger->warning(Logs::NETWORK, "network link lost");
                    _unwire_resolver();
                    _enter(NET_ERROR, now_ms);
                }
                break;

            case NET_ERROR:
                if (now_ms - _since_ms > Config::NET_RETRY_MS) _begin_setup(now_ms);
                break;
        }
    }

}}
