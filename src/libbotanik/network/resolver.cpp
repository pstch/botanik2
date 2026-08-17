// REVIEW_35: TODO
/**
 * @file resolver.cpp
 * @brief NameResolver - non-blocking hostname -> IPv4 via HAL::Net DNS poll API.
 *
 * IPv4 literals are parsed locally (no DNS, no table slot); hostnames go through
 * the non-blocking dns_begin/dns_poll poll API (async on both backends:
 * gethostbyname_async on OPTA, an in-process emulated resolver on the sim).
 */

#include "libbotanik/network/resolver.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_NETWORK

#include <cstring>

#include "libbotanik/hal/net.h"
#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Network {

    NameResolver::NameResolver(Logs::Logger* logger)
        : _logger(logger) {
        for (size_t i = 0; i < Config::DNS_CACHE_SIZE; ++i) {
            _table[i].state    = SLOT_FREE;
            _table[i].handle   = 0;
            _table[i].host[0]  = '\0';
            _table[i].stamp_at = 0;
        }
    }

    void NameResolver::clear() {
        for (size_t i = 0; i < Config::DNS_CACHE_SIZE; ++i) {
            if (_table[i].state == SLOT_PENDING && _table[i].handle != 0)
                HAL::Network::dns_cancel(_table[i].handle);
            _table[i].state    = SLOT_FREE;
            _table[i].handle   = 0;
            _table[i].host[0]  = '\0';
            _table[i].stamp_at = 0;
        }
    }

    bool NameResolver::_expired(const Entry& e) const {
        if (_cache_ttl_ms == 0) return false;   // 0 = never expire
        // uptime is monotonic (64-bit, no wrap), so a plain difference is safe.
        return (HAL::Clock::uptime_ms() - e.stamp_at) > (uint64_t)_cache_ttl_ms;
    }

    NameResolver::Entry* NameResolver::_find(const char* host) {
        for (size_t i = 0; i < Config::DNS_CACHE_SIZE; ++i) {
            Entry& e = _table[i];
            if (e.state != SLOT_FREE &&
                strncmp(e.host, host, Config::HOST_STR_LEN) == 0)
                return &e;
        }
        return nullptr;
    }

    NameResolver::Entry* NameResolver::_alloc() {
        const uint64_t now = HAL::Clock::uptime_ms();
        Entry* victim = nullptr;
        Entry* stale  = nullptr;                       // oldest reclaimable in-flight row
        for (size_t i = 0; i < Config::DNS_CACHE_SIZE; ++i) {
            Entry& e = _table[i];
            if (e.state == SLOT_FREE) return &e;
            if (e.state != SLOT_PENDING) {
                // First non-pending row is an evictable cache/error entry.
                if (victim == nullptr) victim = &e;
            } else if ((now - e.stamp_at) > (uint64_t)Config::DNS_PENDING_MAX_MS &&
                       (stale == nullptr || e.stamp_at < stale->stamp_at)) {
                stale = &e;
            }
        }
        if (victim != nullptr) return victim;
        // Every row is in flight: reclaim the oldest one past the age bound, so
        // callers that abandoned a lookup cannot wedge the table for good.
        if (stale != nullptr) {
            if (stale->handle != 0) HAL::Network::dns_cancel(stale->handle);
            stale->handle = 0;
            stale->state  = SLOT_FREE;
        }
        return stale;
    }

    ResolveStatus NameResolver::_apply_poll(Entry& e, const char* host,
                                            ResolvedAddr* out) {
        HAL::Network::Ip4 ip;
        switch (HAL::Network::dns_poll(e.handle, &ip)) {
            case HAL::Network::DNS_OK:
                e.addr     = ip;
                e.stamp_at = HAL::Clock::uptime_ms();   // TTL base
                e.state    = SLOT_READY;
                *out = e.addr;
                return RESOLVE_OK;
            case HAL::Network::DNS_FAILED:
                if (_logger != nullptr)
                    _logger->error(Logs::NETWORK, "DNS resolve failed\n%s", host);
                e.state = SLOT_FREE;
                return RESOLVE_FAILED;
            default:
                return RESOLVE_PENDING;
        }
    }

    ResolveStatus NameResolver::resolve(const char* host, ResolvedAddr* out) {
        if (host == nullptr || host[0] == '\0' || out == nullptr) return RESOLVE_FAILED;

        // An IPv4 literal needs neither DNS nor a table slot.
        if (parse_ipv4(host, out)) return RESOLVE_OK;

        // Known row? (cached result, lookup in flight, or a stale failure)
        Entry* e = _find(host);
        if (e != nullptr) {
            if (e->state == SLOT_READY) {
                // Serve the cached address unless it has aged past the TTL; an
                // expired entry is dropped and re-resolved via the miss path below.
                if (!_expired(*e)) { *out = e->addr; return RESOLVE_OK; }
                e->state = SLOT_FREE;
            } else if (e->state == SLOT_PENDING) {
                return _apply_poll(*e, host, out);
            }
        }

        // Start (or poll) a HAL DNS lookup. A row stores its DnsHandle while
        // pending.
        e = _alloc();
        // null only while every row is a lookup too young to reclaim.
        if (e == nullptr) return RESOLVE_FAILED;

        HAL::Network::DnsHandle h = HAL::Network::dns_begin(host);
        if (h == 0) {                                  // could not start
            if (_logger != nullptr) _logger->warning(Logs::NETWORK, "DNS lookup failed to start");
            return RESOLVE_FAILED;
        }
        strncpy(e->host, host, Config::HOST_STR_LEN - 1);
        e->host[Config::HOST_STR_LEN - 1] = '\0';
        e->handle   = h;
        e->state    = SLOT_PENDING;
        e->stamp_at = HAL::Clock::uptime_ms();   // in-flight age, for _alloc()'s reclaim

        // Poll once now: usually PENDING (later resolve() calls drive it), but an
        // OPTA stack-cache hit can land a result immediately.
        return _apply_poll(*e, host, out);
    }

}}

#endif // BOTANIK_FEATURE_NETWORK
