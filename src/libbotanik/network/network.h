// REVIEW_35: TODO
#ifndef NETWORK_NETWORK_H
#define NETWORK_NETWORK_H

/**
 * @file network.h
 * @brief NetworkHandler - PLC-config-driven state machine that brings up the
 *        link.
 *
 * Derives Component (config-serial machinery), so it restarts when its config
 * block's serial bumps. NetworkHandler owns the link (via HAL::Net) and is_up()
 * is authoritative. (The NTP client lives in network/ntp.h.)
 */

#include <cstdint>

#include "libbotanik/core/component.h"
#include "libbotanik/core/params.h"     // Params::ParamHandler + cfg_* name getters
#include "libbotanik/hal/net.h"
#include "libbotanik/network/resolver.h"

namespace Botanik { namespace Network {

    /** @brief Owns the link (via HAL::Net bring-up); is_up() is authoritative.
     *  Drives the link state machine from the PLC `net` config block,
     *  restarting on a config change. Clears the NameResolver cache on
     *  reconfiguration / link loss. */
    class NetworkHandler : public Component {
        /** @name State
         *  @{ */

        /** @brief Bring-up state. on_param_changed() resets to SETUP (or IDLE
         *  if no usable config); on_step() drives SETUP -> RUNNING; ERROR backs
         *  off. */
        enum State : uint8_t {
            /// no usable config (serial 0); nothing to bring up
            NET_IDLE = 0,
            NET_SETUP,      ///< link_configure called; waiting for LINK_UP
            NET_RUNNING,    ///< link up with an address; resolver cache valid
            /// bring-up failed / link lost; waiting out backoff
            NET_ERROR,
        };

        /** @brief Config source (may be null in tests). */
        const Params::ParamHandler* _cfg;
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
         *  link loss. */
        NetworkHandler(const Params::ParamHandler* cfg, Logs::Logger* logger = nullptr,
                       NameResolver* resolver = nullptr);

        /** @brief True once the link is up with an address. */
        bool is_up() const { return _state == NET_RUNNING; }

        /** @brief Healthy while the link is up. */
        bool healthy() const override { return is_up(); }

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
        uint32_t param_serial() const override;
        void on_param_changed(uint64_t now_ms) override;
        void on_step(uint64_t now_ms) override;
        /** @} */

    private:
        /** @name Internals
         *  @{ */
        /** @brief Enter a state and stamp the entry time. */
        void _enter(State s, uint64_t now_ms);
        /** @brief Configure the link from the PLC net block and move to
         *  NET_SETUP. */
        void _begin_setup(uint64_t now_ms);
        /** @brief Clear the resolver's cache on reconfiguration / link loss. */
        void _unwire_resolver();
        /** @} */
    };

}}

#endif // NETWORK_NETWORK_H
