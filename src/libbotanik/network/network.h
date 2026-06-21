#ifndef NETWORK_NETWORK_H
#define NETWORK_NETWORK_H

/**
 * @file network.h
 * @brief NetworkHandler and TimeSyncHandler - PLC-config-driven state machines that
 *        bring up the network device and run an NTP client.
 *
 * Both derive ConfigHandler (config_handler.h), so each restarts its state machine
 * whenever the operator changes its config block (the PLC bumps that block's serial).
 * NetworkHandler owns the network interface and hands it to the shared NameResolver
 * (resolver.h) once the link is up; TimeSyncHandler resolves its NTP servers through
 * that same resolver.
 */

#include <cstdint>

#include "libbotanik/config_handler.h"
#include "libbotanik/network/resolver.h"

// Mbed network device (global namespace); used only in network.cpp, forward-declared
// so this header stays clear of the Mbed headers.
class NetworkInterface;

namespace Botanik { namespace Network {

    /** @brief Brings the network device up from the PLC `net` config block, as a state
     *  machine that restarts on a config change. Owns the interface and hands it to the
     *  NameResolver while the link is up. */
    class NetworkHandler : public ConfigHandler {
        /** @name State
         *  @{ */

        /** @brief Bring-up state. on_config_changed() resets to SETUP (or IDLE if there
         *  is no usable config); on_step() drives SETUP -> RUNNING, and ERROR backs off
         *  before retrying. */
        enum State : uint8_t {
            NET_IDLE = 0,   ///< no usable config (serial 0); nothing to bring up
            NET_SETUP,      ///< device configured; waiting for the link + an address
            NET_RUNNING,    ///< link up with an address; interface handed to the resolver
            NET_ERROR,      ///< bring-up failed / link lost; waiting out the retry backoff
        };

        State             _state;
        uint64_t          _since_ms;   ///< when the current state was entered (for timeouts)
        NameResolver*     _resolver;   ///< wired with the live interface while up (may be null)
        NetworkInterface* _net;        ///< the network device (null until first bring-up)

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief @p resolver (optional) is handed the live interface once the link is
         *  up, and cleared on a reconfiguration / link loss. */
        NetworkHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr,
                       NameResolver* resolver = nullptr);

        /** @brief True once the link is up with an address. */
        bool is_up() const { return _state == NET_RUNNING; }
        /** @brief The live network interface, or null until the link is up. */
        NetworkInterface* interface() const { return _state == NET_RUNNING ? _net : nullptr; }

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
        /** @brief Configure the device from the `net` block and start a non-blocking
         *  connect; moves to SETUP (on the sim, straight to RUNNING). */
        void _begin_setup(uint64_t now_ms);
        /** @brief Unwire the resolver (clear its interface + cache). */
        void _unwire_resolver();
        /** @} */
    };

    /** @brief NTP client driven by the PLC `ntp` config block: resolves a server via the
     *  NameResolver, runs a non-blocking SNTP exchange over UDP on the live interface
     *  (reached through the resolver), and disciplines the RTC. Restarts on a config
     *  change; falls back to the second server and backs off on failure. */
    class TimeSyncHandler : public ConfigHandler {
        /** @name State
         *  @{ */
        enum State : uint8_t {
            TIME_IDLE = 0,   ///< disabled / no config
            TIME_RESOLVE,    ///< resolving the current server's name through the resolver
            TIME_QUERY,      ///< SNTP request sent; polling for the reply
            TIME_SYNCED,     ///< RTC disciplined; idle until the next poll
            TIME_ERROR,      ///< failed; backing off before a retry
        };
        State         _state;
        uint64_t      _since_ms;       ///< when the current state was entered (for timeouts)
        NameResolver* _resolver;       ///< DNS + the live interface (set by NetworkHandler)
        ResolvedAddr  _server_addr;    ///< resolved address of the server being queried
        uint8_t       _server_idx;     ///< which configured server we are on (0 = server1)
        /** @} */

    public:
        TimeSyncHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr,
                        NameResolver* resolver = nullptr);

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
         *  OPTA only; returns false on any socket error. */
        bool _send_query();
        /** @brief Poll the UDP socket for the reply. OPTA only; returns 1 = synced (RTC
         *  set), 0 = still waiting, -1 = failed/timed out. */
        int _poll_query(uint64_t now_ms);
        /** @} */
    };

}}

#endif // NETWORK_NETWORK_H
