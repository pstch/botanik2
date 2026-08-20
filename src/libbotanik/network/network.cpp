// REVIEW_35: TODO
/**
 * @file network.cpp
 * @brief NetworkHandler implementation. (The NTP client lives in ntp.cpp.)
 *
 * Bring-up goes through HAL::Net (Mbed vs. BSD handled by the backend). On the
 * sim link_status() returns LINK_UP immediately, so NET_SETUP -> NET_RUNNING
 * next step.
 */

#include "libbotanik/tasks/tasks.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_NETWORK
#include "libbotanik/network/network.h"

#include <cstring>

#include "libbotanik/hal/net.h"

namespace Botanik { namespace Network {

    // NetworkHandler

    NetworkHandler::NetworkHandler(const SetupContext& ctx, NameResolver* resolver)
        : Component(ctx, "NetworkHandler"),
          _state(NET_IDLE), _since_ms(0), _resolver(resolver) {}

    void NetworkHandler::set_config(const NetworkConfig& cfg) {
        _cfg = cfg;
        _pending = true;
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

    void NetworkHandler::_begin_setup(uint64_t now_ms) {
        HAL::Network::LinkConfig lc;
        memset(&lc, 0, sizeof(lc));
        lc.dhcp = _cfg.dhcp;
        if (!lc.dhcp) {
            strncpy(lc.ip,      _cfg.ip,   sizeof(lc.ip) - 1);
            strncpy(lc.netmask, _cfg.mask, sizeof(lc.netmask) - 1);
            strncpy(lc.gateway, _cfg.gw,   sizeof(lc.gateway) - 1);
            strncpy(lc.dns1,    _cfg.dns1, sizeof(lc.dns1) - 1);
            strncpy(lc.dns2,    _cfg.dns2, sizeof(lc.dns2) - 1);
        }
        HAL::Network::link_configure(lc);
        if (_logger) _logger->info(Logs::NETWORK, lc.dhcp ? "network bring-up (DHCP)"
                                                           : "network bring-up (static)");
        _enter(NET_SETUP, now_ms);
    }

    void NetworkHandler::on_service(uint64_t now_ms) {
        // A config landed at CONFIG, earlier in this same beat. Drop the
        // resolver cache with the link it was resolved over, and start again.
        if (_pending) {
            _pending = false;
            // The cache was resolved over the link that is about to go, either
            // way.
            _unwire_resolver();
            if (!_cfg.enabled) {
                HAL::Network::link_disconnect();
                _enter(NET_IDLE, now_ms);
            } else {
                _begin_setup(now_ms);
            }
        }
        switch (_state) {
            case NET_IDLE:
                // wait for a config to arrive (handled above, on _pending)
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

namespace Botanik { namespace Network {

    void NetworkHandler::_service_task(Component* c, const Tasks::Tick& t) {
        static_cast<NetworkHandler*>(c)->on_service(t.now_ms);
    }

    void NetworkHandler::register_tasks() {
        _tasks->add(*this, &NetworkHandler::_service_task, "sys_net", Tasks::GROUP_FAST,
              Tasks::PHASE_SERVICE);
    }

}}  // namespace Botanik::Network

#endif // BOTANIK_FEATURE_NETWORK
