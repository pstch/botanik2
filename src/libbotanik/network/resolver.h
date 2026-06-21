#ifndef NETWORK_RESOLVER_H
#define NETWORK_RESOLVER_H

/**
 * @file resolver.h
 * @brief NameResolver - non-blocking hostname -> IPv4 resolution shared by the
 *        network-using components (NTP, MQTT, ...).
 *
 * Wraps Mbed's asynchronous DNS (NetworkInterface::gethostbyname_async) behind a
 * poll-style API: a caller asks resolve() for a name every tick and gets back either
 * a resolved address now, "pending" (a lookup is in flight, ask again later), or
 * "failed" (back off and retry on a later tick). Nothing blocks the main loop.
 *
 * One fixed-size table backs both jobs (size Config::DNS_CACHE_SIZE): each row holds an
 * in-flight request's handle and the slot the DNS callback writes its result into, and
 * a completed row stays as a cache entry that serves later resolve() calls without a
 * fresh lookup.
 *
 * Target-neutral header: the Mbed types stay in resolver.cpp (only forward-declared
 * here). On the simulator there is no Mbed stack, so resolve() handles dotted-decimal
 * IPv4 literals directly and reports hostnames as failed.
 */

#include <cstdint>

#include "libbotanik/logs/logger.h"
#include "libbotanik_config.h"

// Mbed types (global namespace), used only by the OPTA path in resolver.cpp; forward-
// declared so this header stays clear of the Mbed headers.
class NetworkInterface;
class SocketAddress;

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

    /** @brief Non-blocking hostname -> IPv4 resolver over Mbed async DNS. */
    class NameResolver {
        /** @name Table
         *  @{ */

        /** @brief Per-row lifecycle. The DNS callback advances PENDING -> READY/ERROR
         *  (it runs in the network-stack context); resolve() (main loop) owns every
         *  other transition, and never reuses a PENDING row. */
        enum SlotState : uint8_t {
            SLOT_FREE = 0,   ///< empty, reusable
            SLOT_PENDING,    ///< gethostbyname_async in flight; the callback owns addr/state
            SLOT_READY,      ///< resolved; addr valid (also a cache entry)
            SLOT_ERROR,      ///< lookup failed; transient (freed on the next resolve())
        };

        /** @brief One table row: an in-flight request and/or a cached result. */
        struct Entry {
            char             host[Config::PLC_HOST_STR_LEN]; ///< requested name (cache key)
            ResolvedAddr     addr;     ///< result, valid when state == SLOT_READY
            int              handle;   ///< gethostbyname_async id while PENDING (0 = none)
            volatile uint8_t state;    ///< SlotState; the DNS callback publishes it (other thread)
        };

        Entry             _table[Config::DNS_CACHE_SIZE];
        NetworkInterface* _net;        ///< network device, set once it is up (null = no resolver)
        Logs::Logger*     _logger;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        NameResolver(Logs::Logger* logger = nullptr);
        /** @brief Wire the network device once it is up (before any resolve()). */
        void set_interface(NetworkInterface* net) { _net = net; }
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
        /** @brief DNS completion callback (Mbed async), bound per row via the entry
         *  pointer. Runs in the network-stack context, so it only writes addr then
         *  publishes state with a barrier. Defined in the OPTA path only. */
        static void _on_dns(Entry* e, int result, SocketAddress* address);

        /** @} */
    };

}}

#endif // NETWORK_RESOLVER_H
