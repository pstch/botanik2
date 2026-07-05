// REVIEW_35: TODO
#ifndef NETWORK_NETWORK_H
#define NETWORK_NETWORK_H

/**
 * @file network.h
 * @brief NetworkHandler and NtpClient - PLC-config-driven state machines that
 *        bring up the link and run an NTP client.
 *
 * Both derive Component (config-serial machinery), so each restarts when its
 * config block's serial bumps. NetworkHandler owns the link (via HAL::Net) and
 * is_up() is authoritative; NtpClient resolves through the shared NameResolver
 * and gates on is_up().
 */

#include <cstdint>

#include "libbotanik/core/component.h"
#include "libbotanik/plcio/sync.h"      // PlcSyncContext + plc_config()
#include "libbotanik/hal/net.h"
#include "libbotanik/network/resolver.h"

namespace Botanik { namespace Network {

    /** @brief Convert an SNTP reply's transmit timestamp (bytes 40..47,
     *  big-endian: 32-bit seconds since 1900 + 32-bit fraction) to UNIX
     *  milliseconds. Returns 0 if the timestamp is unset or pre-1970. */
    inline uint64_t ntp_to_unix_ms(const uint8_t* pkt) {
        uint32_t secs = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) |
                        ((uint32_t)pkt[42] << 8)  |  (uint32_t)pkt[43];
        uint32_t frac = ((uint32_t)pkt[44] << 24) | ((uint32_t)pkt[45] << 16) |
                        ((uint32_t)pkt[46] << 8)  |  (uint32_t)pkt[47];
        // seconds between 1900 and 1970
        const uint32_t NTP_UNIX_DELTA = 2208988800u;
        if (secs < NTP_UNIX_DELTA) return 0;   // 0 / pre-1970 -> invalid
        uint64_t unix_s = (uint64_t)secs - NTP_UNIX_DELTA;
        uint32_t ms = (uint32_t)(((uint64_t)frac * 1000u) >> 32);
        return unix_s * 1000ull + ms;
    }

    /** @brief Owns the link (via HAL::Net bring-up); is_up() is authoritative.
     *  Drives the link state machine from the PLC `net` config block,
     *  restarting on a config change. Clears the NameResolver cache on
     *  reconfiguration / link loss. */
    class NetworkHandler : public Component {
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

        /** @brief PLC config source (may be null in tests). */
        PlcIO::PlcSyncContext* _ctx;
        State         _state;
        /// when this state was entered (for timeouts)
        uint64_t      _since_ms;
        /** @brief Cleared on reconfiguration / link loss (may be null). */
        NameResolver* _resolver;

        /** @} */

        /** @brief The generic config block (or null with no ctx). */
        const PlcIO::PlcOutputConfig_t* config() const { return PlcIO::plc_config(_ctx); }

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief @p resolver (optional) cache is cleared on reconfiguration or
         *  link loss. */
        NetworkHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr,
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
    class NtpClient : public Component {
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
        /** @brief PLC config source (may be null in tests). */
        PlcIO::PlcSyncContext* _ctx;
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
        /** @brief True while a valid sync is held. Set on a sync, kept across
         *  the routine re-poll, cleared on a failure (ERROR / disabled / reconfig)
         *  so the next sync reads as initial-or-recovery (the only ones logged /
         *  surfaced). */
        bool                  _sync_held;
        /** @} */

        /** @brief The generic config block (or null with no ctx). */
        const PlcIO::PlcOutputConfig_t* config() const { return PlcIO::plc_config(_ctx); }

    public:
        NtpClient(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr,
                  NameResolver* resolver = nullptr,
                  const NetworkHandler* net = nullptr);

        /** @brief True while a valid sync is held. Stays true through the routine
         *  re-poll (it is not "unsynced" mid-refresh); only an actual loss clears
         *  it. */
        bool synced() const { return _sync_held; }
        /** @brief Healthy while a valid time sync is held. */
        bool healthy() const override { return _sync_held; }
        /** @brief Sync phase as a short display string
         *  (off/resolving/querying/synced/error). */
        const char* status_str() const;
        /** @brief Last measured clock offset, ms (valid after a sync). */
        int32_t offset_ms() const { return _offset_ms; }
        /** @brief Resolved address of the server in use; meaningful once past
         *  the resolve step (all-zero until then). */
        ResolvedAddr server_addr() const { return _server_addr; }
        /** @brief NTP server UDP port (the well-known 123). */
        uint16_t server_port() const { return 123; }
        /** @brief Force an immediate resync: restart from the first server now
         *  (resolve -> query), regardless of the poll cadence. No-op effect
         *  next step if NTP is disabled/unconfigured. */
        void resync();

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
