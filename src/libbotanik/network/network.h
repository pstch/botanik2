#ifndef NETWORK_NETWORK_H
#define NETWORK_NETWORK_H

/**
 * @file network.h
 * @brief NetworkHandler - parameter-driven state machine that brings up the
 *        link.
 *
 * Derives Component (config-serial machinery), so it restarts when its config
 * block's serial bumps. NetworkHandler owns the link (via HAL::Net) and is_up()
 * is authoritative. (The NTP client lives in network/ntp.h.)
 */

#include <cstdint>

#include "libbotanik/core/component.h"
#include "libbotanik/hal/net.h"
#include "libbotanik/network/resolver.h"

namespace Botanik { namespace Tasks { struct Tick; class Registry; } }

namespace Botanik { namespace Network {

    /** @brief The link a NetworkHandler brings up.
     *
     *  @note These two deliberately disagree with the `net_` rows, which pair
     *  dhcp=0 with a real static address. A build with NO configuration source
     *  must not invent one, so the addresses stay empty and DHCP is the only
     *  thing left that can work. Every other Config struct in the library
     *  matches its rows exactly. */
    struct NetworkConfig {
        bool enabled  = true;
        bool dhcp     = true;
        char ip[16]   = {0};
        char mask[16] = {0};
        char gw[16]   = {0};
        char dns1[16] = {0};
        char dns2[16] = {0};
    };

    /** @brief Owns the link (via HAL::Net bring-up); is_up() is authoritative.
     *  Drives the link state machine from the `net` parameter group,
     *  restarting on a config change. Clears the NameResolver cache on
     *  reconfiguration / link loss. */
    class NetworkHandler : public Component {
    public:
        void register_tasks() override;
    private:

        /** @name State
         *  @{ */

        /** @brief Bring-up state. set_config() resets to SETUP (or IDLE when
         *  disabled); on_service() drives SETUP -> RUNNING; ERROR backs off. */
        enum State : uint8_t {
            /// no config yet, or net_enable off; nothing to bring up
            NET_IDLE = 0,
            NET_SETUP,      ///< link_configure called; waiting for LINK_UP
            NET_RUNNING,    ///< link up with an address; resolver cache valid
            /// bring-up failed / link lost; waiting out backoff
            NET_ERROR,
        };

        /** @brief The link in force, and whether a fresh one is waiting for the
         *  next SERVICE beat to be brought up (set_config() has no clock). */
        NetworkConfig _cfg;
        bool          _pending = false;
        State         _state;
        /// when this state was entered (for timeouts)
        uint64_t      _since_ms;
        /** @brief Cleared on reconfiguration / link loss (may be null). */
        NameResolver* _resolver;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief @p resolver (optional) cache is cleared on reconfiguration or
         *  link loss. Idle until set_config(). */
        NetworkHandler(const SetupContext& ctx, NameResolver* resolver = nullptr);

        /** @brief Adopt @p cfg and re-bring-up. The link goes down and back
         *  from the next SERVICE beat, which is where the clock is. */
        void set_config(const NetworkConfig& cfg);

        /** @brief True once the link is up with an address. */
        bool is_up() const { return _state == NET_RUNNING; }

        /** @brief Healthy while the link is up. */
        bool healthy() const override { return is_up(); }

        /** @brief The TASKS verdict: up is HEALTHY, a failed bring-up is
         *  FAULTED, and coming up is neither. */
        Plc::Health verdict() const override {
            if (is_up())   return Plc::Health::HEALTHY;
            if (failed())  return Plc::Health::FAULTED;
            return Plc::Health::DEGRADED;
        }

        /** @brief Bring-up FAILED, or the link was lost: the alarm condition,
         *  which is not the same as "not up". Coming up is not a fault and
         *  neither is being switched off, and SETUP times out into this. */
        bool failed() const { return _state == NET_ERROR; }

        /** @brief Bring-up phase as a short display string
         *  (down/setup/online/error). */
        const char* status_str() const;

        /** @brief Current IPv4 into @p out; false unless the link is up. */
        bool ip(HAL::Network::Ip4* out) const {
            return is_up() && HAL::Network::link_ip(out);
        }

        /** @} */

    protected:
        /** @name Component hooks
         *  @{ */
        void on_service(uint64_t now_ms) override;
        /** @} */

    private:
        /** @name Internals
         *  @{ */
        /** @brief Enter a state and stamp the entry time. */
        void _enter(State s, uint64_t now_ms);
        /** @brief Configure the link from the `net` parameters and move to
         *  NET_SETUP. */
        void _begin_setup(uint64_t now_ms);
        /** @brief Clear the resolver's cache on reconfiguration / link loss. */
        void _unwire_resolver();
        /** @} */
    };

}}

#endif // NETWORK_NETWORK_H
