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
#include "libbotanik/hal/platform.h"   // HAL::ordered_read

namespace Botanik { namespace Network {

    namespace {
        // Parse a dotted-decimal IPv4 literal ("a.b.c.d", each octet 0..255) into
        // four octets. Returns false on anything else (a hostname, partial, or
        // out-of-range), so a name falls through to DNS. Strict: rejects trailing
        // junk and empty octets.
        bool parse_ipv4(const char* s, ResolvedAddr* out) {
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
                    tmp.bytes[parts++] = (uint8_t)octet;
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
    }  // namespace

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
                HAL::Net::dns_cancel(_table[i].handle);
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
            uint8_t st = _table[i].state;
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
            uint8_t st = HAL::ordered_read(&e->state);
            if (st == SLOT_READY)  { *out = e->addr; return RESOLVE_OK; }
            if (st == SLOT_PENDING) {
                HAL::Net::Ip4 ip;
                HAL::Net::DnsResult r = HAL::Net::dns_poll(e->handle, &ip);
                if (r == HAL::Net::DNS_OK) {
                    e->addr.bytes[0]=ip.b[0]; e->addr.bytes[1]=ip.b[1];
                    e->addr.bytes[2]=ip.b[2]; e->addr.bytes[3]=ip.b[3];
                    e->state = SLOT_READY; *out = e->addr; return RESOLVE_OK;
                }
                if (r == HAL::Net::DNS_FAILED) { e->state = SLOT_FREE; return RESOLVE_FAILED; }
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

        HAL::Net::DnsHandle h = HAL::Net::dns_begin(host);
        if (h == 0) {                                  // could not start
            if (_logger != nullptr) _logger->warning(PlcIO::NETWORK, "DNS lookup failed to start");
            return RESOLVE_FAILED;
        }
        strncpy(e->host, host, Config::PLC_HOST_STR_LEN - 1);
        e->host[Config::PLC_HOST_STR_LEN - 1] = '\0';
        e->handle = h;
        e->state  = SLOT_PENDING;

        // Poll once now: usually PENDING (later resolve() calls drive it), but an
        // OPTA stack-cache hit can land a result immediately.
        HAL::Net::Ip4 ip;
        HAL::Net::DnsResult r = HAL::Net::dns_poll(h, &ip);
        if (r == HAL::Net::DNS_OK) {
            e->addr.bytes[0]=ip.b[0]; e->addr.bytes[1]=ip.b[1];
            e->addr.bytes[2]=ip.b[2]; e->addr.bytes[3]=ip.b[3];
            e->state = SLOT_READY;
            *out = e->addr;
            return RESOLVE_OK;
        }
        if (r == HAL::Net::DNS_FAILED) { e->state = SLOT_FREE; return RESOLVE_FAILED; }
        return RESOLVE_PENDING;
    }

}}
