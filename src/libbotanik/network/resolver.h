#ifndef NETWORK_RESOLVER_H
#define NETWORK_RESOLVER_H

/**
 * @file resolver.h
 * @brief NameResolver - non-blocking hostname -> IPv4 resolution shared by the
 *        network-using components (NTP, MQTT, ...).
 *
 * Poll-style: resolve() returns an address now, PENDING (lookup in flight, ask again),
 * or FAILED (back off, retry later); nothing blocks the main loop. A fixed-size table
 * (Config::DNS_CACHE_SIZE) backs it: a row holds an in-flight handle + result slot, and
 * a completed row stays as a cache entry for later resolve() calls.
 */

#include <cstdint>

#include "libbotanik/hal/net.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik_config.h"

namespace Botanik { namespace Network {

    /** @brief A resolved IPv4 address. Resolution yields exactly the four
     *  octets the sockets consume, so it is the HAL's IPv4 type - one address
     *  type end to end (no copy between a resolver type and a socket type). */
    typedef HAL::Network::Ip4 ResolvedAddr;

    /** @brief Outcome of a resolve() call. PENDING and FAILED both mean "not now, try
     *  again on a later tick"; only OK carries an address. */
    enum ResolveStatus {
        RESOLVE_OK = 0,    ///< @p out holds a resolved address
        RESOLVE_PENDING,   ///< a lookup is in flight; call resolve() again later
        RESOLVE_FAILED,    ///< lookup failed or no free slot; back off and retry later
    };

    /** @brief Non-blocking hostname -> IPv4 resolver via HAL::Net DNS. */
    class NameResolver {
        /** @name Table
         *  @{ */

        /** @brief Per-row lifecycle. dns_poll advances PENDING -> READY or frees the row
         *  on failure; resolve() (main loop) owns every other transition. */
        enum SlotState : uint8_t {
            SLOT_FREE = 0,   ///< empty, reusable
            SLOT_PENDING,    ///< dns_begin in flight; dns_poll drives state
            SLOT_READY,      ///< resolved; addr valid (also a cache entry)
            SLOT_ERROR,      ///< lookup failed; transient (freed on the next resolve())
        };

        /** @brief One table row: an in-flight request and/or a cached result. */
        struct Entry {
            char                  host[Config::PLC_HOST_STR_LEN]; ///< name (cache key)
            ResolvedAddr          addr;     ///< result, valid when state == SLOT_READY
            HAL::Network::DnsHandle   handle;   ///< dns_begin handle while PENDING (0 = none)
            volatile uint8_t      state;    ///< SlotState
        };

        Entry             _table[Config::DNS_CACHE_SIZE];
        Logs::Logger*     _logger;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        NameResolver(Logs::Logger* logger = nullptr);
        /** @brief Drop every row (cache + in-flight), cancelling pending lookups. Call
         *  on a network reconfiguration so stale cached addresses don't linger. */
        void clear();

        /** @} */

        /** @name Resolution
         *  @{ */

        /** @brief Resolve @p host (hostname or IPv4 literal) without blocking. OK with
         *  @p out filled on a literal / cache / immediate stack hit; PENDING if a lookup
         *  is in flight; FAILED on error or no free slot. Poll again until OK. */
        ResolveStatus resolve(const char* host, ResolvedAddr* out);

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Find the row holding @p host (any non-free state), or null. */
        Entry* _find(const char* host);
        /** @brief Reserve a row for a new lookup: a FREE row, else an evictable
         *  READY/ERROR row, else null (every row is PENDING). Never evicts PENDING. */
        Entry* _alloc();

        /** @} */
    };

}}

#endif // NETWORK_RESOLVER_H
