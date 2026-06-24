#ifndef NETWORK_NETWORK_H
#define NETWORK_NETWORK_H

/**
 * @file network.h
 * @brief NetworkHandler and NtpClient - PLC-config-driven state machines that
 *        bring up the link and run an NTP client.
 *
 * Both derive ConfigHandler, so each restarts when its config block's serial
 * bumps. NetworkHandler owns the link (via HAL::Net) and is_up() is
 * authoritative; NtpClient resolves through the shared NameResolver and gates
 * on is_up().
 */

#include <cstdint>

#include "libbotanik/config_handler.h"
#include "libbotanik/hal/net.h"
#include "libbotanik/network/resolver.h"

namespace Botanik { namespace Network {

    /** @brief Owns the link (via HAL::Net bring-up); is_up() is authoritative.
     *  Drives the link state machine from the PLC `net` config block,
     *  restarting on a config change. Clears the NameResolver cache on
     *  reconfiguration / link loss. */
    class NetworkHandler : public ConfigHandler {
        /** @name State
         *  @{ */

        /** @brief Bring-up state. on_config_changed() resets to SETUP (or IDLE
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
        NetworkHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr,
                       NameResolver* resolver = nullptr);

        /** @brief True once the link is up with an address. */
        bool is_up() const { return _state == NET_RUNNING; }

        /** @brief Bring-up phase as a short display string
         *  (down/setup/online/error). */
        const char* status_str() const;

        /** @brief Current IPv4 into @p out; false unless the link is up. */
        bool ip(HAL::Network::Ip4* out) const {
            return is_up() && HAL::Network::link_ip(out);
        }

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
        /** @brief Configure the link from the PLC net block and move to
         *  NET_SETUP. */
        void _begin_setup(uint64_t now_ms);
        /** @brief Clear the resolver's cache on reconfiguration / link loss. */
        void _unwire_resolver();
        /** @} */
    };

    /** @brief NTP client driven by the PLC `ntp` config block: resolves a
     *  server via the NameResolver, runs a non-blocking SNTP exchange over
     *  HAL::Net UDP, and disciplines the RTC. Gated on NetworkHandler::is_up().
     *  Restarts on a config change; falls back to the second server and backs
     *  off on failure. */
    class NtpClient : public ConfigHandler {
        /** @name State
         *  @{ */
        enum State : uint8_t {
            TIME_IDLE = 0,   ///< disabled / no config
            /// resolving the current server's name through the resolver
            TIME_RESOLVE,
            TIME_QUERY,      ///< SNTP request sent; polling for the reply
            TIME_SYNCED,     ///< RTC disciplined; idle until the next poll
            TIME_ERROR,      ///< failed; backing off before a retry
        };
        State                 _state;
        /// when this state was entered (timeouts)
        uint64_t              _since_ms;
        /// DNS resolver (set by NetworkHandler)
        NameResolver*         _resolver;
        /// link gate (null = always blocked)
        const NetworkHandler* _net;
        /** @brief Resolved address of the server being queried. */
        ResolvedAddr          _server_addr;
        /** @brief Which configured server we are on (0 = server1). */
        uint8_t               _server_idx;
        /// open UDP socket handle (-1 = none)
        HAL::Network::UdpSocket   _udp;
        /** @brief Last measured clock offset, ms (correction applied at sync). */
        int32_t               _offset_ms;
        /** @} */

    public:
        NtpClient(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr,
                  NameResolver* resolver = nullptr,
                  const NetworkHandler* net = nullptr);

        /** @brief True once the RTC has been disciplined. */
        bool synced() const { return _state == TIME_SYNCED; }
        /** @brief Sync phase as a short display string
         *  (off/resolving/querying/synced/error). */
        const char* status_str() const;
        /** @brief Last measured clock offset, ms (valid after a sync). */
        int32_t offset_ms() const { return _offset_ms; }

    protected:
        uint32_t config_serial() const override;
        void on_config_changed(uint64_t now_ms) override;
        void on_step(uint64_t now_ms) override;

    private:
        /** @name Internals
         *  @{ */
        /** @brief Advance to the second server if there is one, else go to
         *  ERROR. */
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
