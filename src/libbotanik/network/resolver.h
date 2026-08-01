// REVIEW_35: TODO
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

#include "../hal/net.h"
#include "../logs/logger.h"
#include "../../libbotanik_config.h"

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

    /** @brief Parse a dotted-decimal IPv4 literal ("a.b.c.d", each octet 0..255)
     *  into four octets. Returns false on anything else (a hostname, partial, or
     *  out-of-range), so a name falls through to DNS. Strict: rejects trailing
     *  junk and empty octets. */
    inline bool parse_ipv4(const char* s, ResolvedAddr* out) {
        ResolvedAddr tmp = {{0, 0, 0, 0}};
        uint32_t octet = 0;
        int digits = 0, parts = 0;
        for (const char* p = s; ; ++p) {
            char c = *p;
            if (c >= '0' && c <= '9') {
                octet = octet * 10u + (uint32_t)(c - '0');
                if (octet > 255u) return false;
                ++digits;
            } else if (c == '.' || c == '\0') {
                if (digits == 0 || parts >= 4) return false;
                tmp.b[parts++] = (uint8_t)octet;
                octet = 0; digits = 0;
                if (c == '\0') break;
            } else {
                return false;   // non-digit / non-dot: treat as a hostname
            }
        }
        if (parts != 4) return false;
        *out = tmp;
        return true;
    }

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
        };

        /** @brief One table row: an in-flight request and/or a cached result. */
        struct Entry {
            char                        host[Config::HOST_STR_LEN]; ///< name (cache key)
            ResolvedAddr                addr;     ///< result           (when SLOT_READY)
            HAL::Network::DnsHandle     handle;   ///< async DNS handle (when SLOT_PENDING)
            uint64_t                    stamp_at; ///< uptime ms the row entered its state
                                                  ///< (TTL base when READY, in-flight age when PENDING)
            SlotState                   state;    ///< current state of the slot
        };

        Entry             _table[Config::DNS_CACHE_SIZE];
        Logs::Logger*     _logger;
        /** @brief Max age a READY entry is served before re-resolving (0 = never
         *  expire). Defaults from Config::DNS_CACHE_TTL_MS. */
        uint32_t          _cache_ttl_ms = Config::DNS_CACHE_TTL_MS;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Construct a new resolver, bound to the given logger (or none). */
        NameResolver(Logs::Logger* logger = nullptr);

        /** @brief Drop the whole cache, and cancel pending lookups. */
        void clear();

        /** @brief Override the cache max-age (ms) for this resolver; 0 disables
         *  expiry. Defaults from Config::DNS_CACHE_TTL_MS. */
        void set_cache_ttl_ms(uint32_t ms) { _cache_ttl_ms = ms; }

        /** @} */

        /** @name Resolution
         *  @{ */

        /** @brief Resolve @p host (hostname or IPv4 literal) without blocking, and store
         *         the result in @p out when successfully resolved.
         *  @returns The status of the requested resolution. */
        ResolveStatus resolve(const char* host, ResolvedAddr* out);

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief True if a READY row has aged past Config::DNS_CACHE_TTL_MS (0
         *  = never expire), so it must be re-resolved rather than served. */
        bool _expired(const Entry& e) const;
        /** @brief Find the row holding @p host (any non-free state), or null. */
        Entry* _find(const char* host);
        /** @brief Reserve a row for a new lookup: a FREE row, else an evictable
         *  READY/ERROR row, else null (every row is PENDING). Never evicts PENDING. */
        Entry* _alloc();

        /** @brief Fold one dns_poll() result into row @p e and turn it into a
         *  ResolveStatus: OK caches the address into @p out, FAILED logs and
         *  frees the row, PENDING leaves it in flight. Shared by the two places
         *  that poll (an already-pending row, and the first poll right after
         *  dns_begin) so their handling cannot drift apart. */
        ResolveStatus _apply_poll(Entry& e, const char* host, ResolvedAddr* out);

        /** @} */
    };

}}

#endif // NETWORK_RESOLVER_H
