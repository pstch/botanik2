#ifndef NETWORK_NETWORK_H
#define NETWORK_NETWORK_H

/**
 * @file network.h
 * @brief NetworkHandler and NtpClient - PLC-config-driven state machines that
 *        bring up the link and run an NTP client.
 *
 * Both derive ConfigHandler, so each restarts when its config block's serial bumps.
 * NetworkHandler owns the link (via HAL::Net) and is_up() is authoritative;
 * NtpClient resolves through the shared NameResolver and gates on is_up().
 */

#include <cstdint>

#include "libbotanik/config_handler.h"
#include "libbotanik/hal/net.h"
#include "libbotanik/network/resolver.h"

namespace Botanik { namespace Network {

    /** @brief Owns the link (via HAL::Net bring-up); is_up() is authoritative.
     *  Drives the link state machine from the PLC `net` config block, restarting on
     *  a config change. Clears the NameResolver cache on reconfiguration / link loss. */
    class NetworkHandler : public ConfigHandler {
        /** @name State
         *  @{ */

        /** @brief Bring-up state. on_config_changed() resets to SETUP (or IDLE if no
         *  usable config); on_step() drives SETUP -> RUNNING; ERROR backs off. */
        enum State : uint8_t {
            NET_IDLE = 0,   ///< no usable config (serial 0); nothing to bring up
            NET_SETUP,      ///< link_configure called; waiting for LINK_UP
            NET_RUNNING,    ///< link up with an address; resolver cache valid
            NET_ERROR,      ///< bring-up failed / link lost; waiting out the retry backoff
        };

        State             _state;
        uint64_t          _since_ms;   ///< when the current state was entered (for timeouts)
        NameResolver*     _resolver;   ///< cleared on reconfiguration / link loss (may be null)

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief @p resolver (optional) cache is cleared on reconfiguration / link loss. */
        NetworkHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr,
                       NameResolver* resolver = nullptr);

        /** @brief True once the link is up with an address. */
        bool is_up() const { return _state == NET_RUNNING; }

        /** @} */

    protected:
        /** @name ConfigHandler hooks
         *  @{ */
        uint32_t config_serial() const override;
        void on_config_changed(uint64_t now_ms) override;
        void on_step(uint64_t now_ms) override;
        /** @} */

    private:
        /** @name Internals
         *  @{ */
        /** @brief Enter a state and stamp the entry time. */
        void _enter(State s, uint64_t now_ms);
        /** @brief Configure the link from the PLC net block and move to NET_SETUP. */
        void _begin_setup(uint64_t now_ms);
        /** @brief Clear the resolver's cache on reconfiguration / link loss. */
        void _unwire_resolver();
        /** @} */
    };

    /** @brief NTP client driven by the PLC `ntp` config block: resolves a server via the
     *  NameResolver, runs a non-blocking SNTP exchange over HAL::Net UDP, and disciplines
     *  the RTC. Gated on NetworkHandler::is_up(). Restarts on a config change; falls back
     *  to the second server and backs off on failure. */
    class NtpClient : public ConfigHandler {
        /** @name State
         *  @{ */
        enum State : uint8_t {
            TIME_IDLE = 0,   ///< disabled / no config
            TIME_RESOLVE,    ///< resolving the current server's name through the resolver
            TIME_QUERY,      ///< SNTP request sent; polling for the reply
            TIME_SYNCED,     ///< RTC disciplined; idle until the next poll
            TIME_ERROR,      ///< failed; backing off before a retry
        };
        State                   _state;
        uint64_t                _since_ms;       ///< when the current state was entered (for timeouts)
        NameResolver*           _resolver;       ///< DNS resolver (set by NetworkHandler)
        const NetworkHandler*   _net;            ///< link gate (null = always blocked)
        ResolvedAddr            _server_addr;    ///< resolved address of the server being queried
        uint8_t                 _server_idx;     ///< which configured server we are on (0 = server1)
        HAL::Net::UdpSocket     _udp;            ///< open UDP socket handle (-1 = none)
        /** @} */

    public:
        NtpClient(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr,
                        NameResolver* resolver = nullptr, const NetworkHandler* net = nullptr);

    protected:
        uint32_t config_serial() const override;
        void on_config_changed(uint64_t now_ms) override;
        void on_step(uint64_t now_ms) override;

    private:
        /** @name Internals
         *  @{ */
        /** @brief Advance to the second server if there is one, else go to ERROR. */
        void _next_server_or_error(uint64_t now_ms);
        /** @brief Open the UDP socket and send an SNTP request to _server_addr.
         *  Returns false on any socket error. */
        bool _send_query();
        /** @brief Poll the UDP socket for the reply. Returns 1 = synced (RTC
         *  set), 0 = still waiting, -1 = failed/timed out. */
        int _poll_query(uint64_t now_ms);
        /** @} */
    };

}}

#endif // NETWORK_NETWORK_H
