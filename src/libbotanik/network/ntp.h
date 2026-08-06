// REVIEW_35: TODO
#ifndef NETWORK_NTP_H
#define NETWORK_NTP_H

/**
 * @file ntp.h
 * @brief NtpClient - parameter-driven SNTP client that disciplines the RTC.
 *
 * Derives Component (config-serial machinery), so it restarts when the `ntp`
 * config block's serial bumps. Resolves a server through the shared
 * NameResolver, runs a non-blocking SNTP exchange over HAL::Net UDP, and gates
 * on NetworkHandler::is_up().
 */

#include <cstdint>                          // uint8_t/int32_t/uint64_t

#include "libbotanik/core/component.h"      // Component
#include "libbotanik/hal/net.h"             // HAL::Network::UdpSocket
#include "libbotanik/network/network.h"     // Network::NetworkHandler
#include "libbotanik/network/resolver.h"    // Network::NameResolver
#include "libbotanik/vars/params.h"         // Vars::Bank / get_str

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

    /** @brief NTP client driven by the `ntp` parameter group: resolves a
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
        /** @brief Config source (may be null in tests). */
        const Vars::Bank* _cfg;
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

    public:
        NtpClient(const Vars::Bank* cfg, Logs::Logger* logger = nullptr,
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
        /** @brief NTP server UDP port, from the ntp_port parameter (123 unless
         *  the installation reaches its time source through a proxy). A
         *  parameter like the rest of the time config, so it is one commit of
         *  the ntp_ group rather than a rebuild. */
        uint16_t server_port() const;
        /** @brief Force an immediate resync: restart from the first server now
         *  (resolve -> query), regardless of the poll cadence. No-op effect
         *  next step if NTP is disabled/unconfigured. */
        void resync();

    protected:
        uint32_t param_serial() const override;
        void on_param_changed(uint64_t now_ms) override;
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

#endif // NETWORK_NTP_H
