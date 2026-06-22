#ifndef NETWORK_RESOLVER_H
#define NETWORK_RESOLVER_H

/**
 * @file resolver.h
 * @brief NameResolver - non-blocking hostname -> IPv4 resolution shared by the
 *        network-using components (NTP, MQTT, ...).
 *
 * Poll-style API via the HAL::Net DNS poll API; the simulator backend uses the host
 * resolver. A caller asks resolve() for a name every tick and gets back either a
 * resolved address now, "pending" (a lookup is in flight, ask again later), or
 * "failed" (back off and retry on a later tick). Nothing blocks the main loop.
 *
 * One fixed-size table backs both jobs (size Config::DNS_CACHE_SIZE): each row holds an
 * in-flight request's handle and the slot dns_poll writes its result into, and a
 * completed row stays as a cache entry that serves later resolve() calls without a
 * fresh lookup.
 */

#include <cstdint>

#include "libbotanik/hal/net.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik_config.h"

// Forward declaration kept for the transitional set_interface/interface shim.
// Removed with those methods once the network components no longer call them (Task 6).
class NetworkInterface;

namespace Botanik { namespace Network {

    /** @brief A resolved IPv4 address as four octets (a.b.c.d). */
    struct ResolvedAddr {
        uint8_t bytes[4];
    };

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
            HAL::Net::DnsHandle   handle;   ///< dns_begin handle while PENDING (0 = none)
            volatile uint8_t      state;    ///< SlotState
        };

        Entry             _table[Config::DNS_CACHE_SIZE];
        void*             _net;    ///< transitional holder; typed accessors below
        Logs::Logger*     _logger;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        NameResolver(Logs::Logger* logger = nullptr);
        // Transitional (removed in the NetworkHandler task once no consumer remains):
        // the resolver no longer uses the interface for DNS; HAL::Net owns the device.
        void               set_interface(NetworkInterface* net) { _net = (void*)net; }
        NetworkInterface*  interface() const { return (NetworkInterface*)_net; }
        /** @brief Drop every row (cache + in-flight), cancelling pending lookups. Call
         *  on a network reconfiguration so stale cached addresses don't linger. */
        void clear();

        /** @} */

        /** @name Resolution
         *  @{ */

        /** @brief Resolve @p host (a hostname or dotted-decimal IPv4 literal) to an
         *  IPv4 address without blocking. Returns RESOLVE_OK with @p out filled on a
         *  literal / cache hit / immediate stack hit; RESOLVE_PENDING if a lookup is
         *  (now or already) in flight; RESOLVE_FAILED on error or no free slot. The
         *  caller polls again on later ticks until it gets RESOLVE_OK. */
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
