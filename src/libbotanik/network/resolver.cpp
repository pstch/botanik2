/**
 * @file resolver.cpp
 * @brief NameResolver - non-blocking hostname -> IPv4 via HAL::Net DNS poll API.
 *
 * IPv4 literals are parsed locally (no DNS, no table slot); hostnames go through
 * the non-blocking dns_begin/dns_poll poll API (async on both backends:
 * gethostbyname_async on OPTA, an in-process emulated resolver on the sim).
 */

#include "libbotanik/network/resolver.h"

#include <cstring>   // strncmp / strncpy

#include "libbotanik/hal/net.h"

namespace Botanik { namespace Network {

    NameResolver::NameResolver(Logs::Logger* logger)
        : _logger(logger) {
        for (size_t i = 0; i < Config::DNS_CACHE_SIZE; ++i) {
            _table[i].state   = SLOT_FREE;
            _table[i].handle  = 0;
            _table[i].host[0] = '\0';
        }
    }

    void NameResolver::clear() {
        for (size_t i = 0; i < Config::DNS_CACHE_SIZE; ++i) {
            if (_table[i].state == SLOT_PENDING && _table[i].handle != 0)
                HAL::Network::dns_cancel(_table[i].handle);
            _table[i].state   = SLOT_FREE;
            _table[i].handle  = 0;
            _table[i].host[0] = '\0';
        }
    }

    NameResolver::Entry* NameResolver::_find(const char* host) {
        for (size_t i = 0; i < Config::DNS_CACHE_SIZE; ++i) {
            Entry& e = _table[i];
            if (e.state != SLOT_FREE &&
                strncmp(e.host, host, Config::PLC_HOST_STR_LEN) == 0)
                return &e;
        }
        return nullptr;
    }

    NameResolver::Entry* NameResolver::_alloc() {
        Entry* victim = nullptr;
        for (size_t i = 0; i < Config::DNS_CACHE_SIZE; ++i) {
            SlotState st = _table[i].state;
            if (st == SLOT_FREE) return &_table[i];
            // First non-pending row is an evictable cache/error entry
            // (fallback).
            if (st != SLOT_PENDING && victim == nullptr) victim = &_table[i];
        }
        return victim;   // null only if every row is PENDING
    }

    ResolveStatus NameResolver::resolve(const char* host, ResolvedAddr* out) {
        if (host == nullptr || host[0] == '\0' || out == nullptr) return RESOLVE_FAILED;

        // An IPv4 literal needs neither DNS nor a table slot.
        if (parse_ipv4(host, out)) return RESOLVE_OK;

        // Known row? (cached result, lookup in flight, or a stale failure)
        Entry* e = _find(host);
        if (e != nullptr) {
            SlotState st = e->state;
            if (st == SLOT_READY)  { *out = e->addr; return RESOLVE_OK; }
            if (st == SLOT_PENDING) {
                HAL::Network::Ip4 ip;
                HAL::Network::DnsResult r = HAL::Network::dns_poll(e->handle, &ip);
                if (r == HAL::Network::DNS_OK) {
                    e->addr = ip;
                    e->state = SLOT_READY; *out = e->addr; return RESOLVE_OK;
                }
                if (r == HAL::Network::DNS_FAILED) {
                    if (_logger != nullptr)
                        _logger->error(Logs::NETWORK, "DNS resolve failed: %s", host);
                    e->state = SLOT_FREE; return RESOLVE_FAILED;
                }
                return RESOLVE_PENDING;
            }
            e->state = SLOT_FREE;   // SLOT_ERROR: free so the next poll retries
            return RESOLVE_FAILED;
        }

        // Start (or poll) a HAL DNS lookup. A row stores its DnsHandle while
        // pending.
        e = _alloc();
        // null only if every row is an in-flight lookup.
        if (e == nullptr) return RESOLVE_FAILED;

        HAL::Network::DnsHandle h = HAL::Network::dns_begin(host);
        if (h == 0) {                                  // could not start
            if (_logger != nullptr) _logger->warning(Logs::NETWORK, "DNS lookup failed to start");
            return RESOLVE_FAILED;
        }
        strncpy(e->host, host, Config::PLC_HOST_STR_LEN - 1);
        e->host[Config::PLC_HOST_STR_LEN - 1] = '\0';
        e->handle = h;
        e->state  = SLOT_PENDING;

        // Poll once now: usually PENDING (later resolve() calls drive it), but an
        // OPTA stack-cache hit can land a result immediately.
        HAL::Network::Ip4 ip;
        HAL::Network::DnsResult r = HAL::Network::dns_poll(h, &ip);
        if (r == HAL::Network::DNS_OK) {
            e->addr = ip;
            e->state = SLOT_READY;
            *out = e->addr;
            return RESOLVE_OK;
        }
        if (r == HAL::Network::DNS_FAILED) {
            if (_logger != nullptr)
                _logger->error(Logs::NETWORK, "DNS resolve failed: %s", host);
            e->state = SLOT_FREE; return RESOLVE_FAILED;
        }
        return RESOLVE_PENDING;
    }

}}
