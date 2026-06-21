/**
 * @file resolver.cpp
 * @brief NameResolver - non-blocking hostname -> IPv4 over Mbed async DNS.
 *
 * The Mbed-specific bits (gethostbyname_async, the SocketAddress result, cancel) are
 * guarded for the OPTA build; the simulator has no Mbed stack, so it resolves only
 * dotted-decimal IPv4 literals and reports hostnames as failed.
 */

#include "libbotanik/network/resolver.h"

#include <cstring>   // strncmp / strncpy

#include "libbotanik/hal/platform.h"   // HAL::dsb / HAL::ordered_read

#ifdef TARGET_OPTA
#include "netsocket/NetworkInterface.h"   // gethostbyname_async, hostbyname_cb_t
#include "netsocket/SocketAddress.h"      // SocketAddress, nsapi_addr_t
#endif

namespace Botanik { namespace Network {

    namespace {
        // Parse a dotted-decimal IPv4 literal ("a.b.c.d", each octet 0..255) into four
        // octets. Returns false on anything else (a hostname, partial, or out-of-range),
        // so a name falls through to DNS. Strict: rejects trailing junk and empty octets.
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
        : _net(nullptr), _logger(logger) {
        for (size_t i = 0; i < Config::DNS_CACHE_SIZE; ++i) {
            _table[i].state   = SLOT_FREE;
            _table[i].handle  = 0;
            _table[i].host[0] = '\0';
        }
    }

    void NameResolver::clear() {
        for (size_t i = 0; i < Config::DNS_CACHE_SIZE; ++i) {
#ifdef TARGET_OPTA
            // Cancel an in-flight lookup so its callback can't write a recycled row.
            if (_net != nullptr && _table[i].state == SLOT_PENDING && _table[i].handle != 0)
                _net->gethostbyname_async_cancel(_table[i].handle);
#endif
            _table[i].state   = SLOT_FREE;
            _table[i].handle  = 0;
            _table[i].host[0] = '\0';
        }
    }

    NameResolver::Entry* NameResolver::_find(const char* host) {
        for (size_t i = 0; i < Config::DNS_CACHE_SIZE; ++i) {
            Entry& e = _table[i];
            if (e.state != SLOT_FREE && strncmp(e.host, host, Config::PLC_HOST_STR_LEN) == 0)
                return &e;
        }
        return nullptr;
    }

    NameResolver::Entry* NameResolver::_alloc() {
        Entry* victim = nullptr;
        for (size_t i = 0; i < Config::DNS_CACHE_SIZE; ++i) {
            uint8_t st = _table[i].state;
            if (st == SLOT_FREE) return &_table[i];
            // First non-pending row is an evictable cache/error entry (fallback).
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
            if (st == SLOT_READY)   { *out = e->addr; return RESOLVE_OK; }
            if (st == SLOT_PENDING) return RESOLVE_PENDING;
            // SLOT_ERROR: a past failure. Free it so the caller's next poll starts a
            // fresh lookup rather than caching the failure forever.
            e->state = SLOT_FREE;
            return RESOLVE_FAILED;
        }

#ifdef TARGET_OPTA
        if (_net == nullptr) return RESOLVE_FAILED;   // network device not up yet

        e = _alloc();
        if (e == nullptr) return RESOLVE_FAILED;       // every row is an in-flight lookup

        // Claim the row PENDING before the call: gethostbyname_async may invoke the
        // callback synchronously (a stack DNS-cache hit) and it must find a live row.
        strncpy(e->host, host, Config::PLC_HOST_STR_LEN - 1);
        e->host[Config::PLC_HOST_STR_LEN - 1] = '\0';
        e->handle = 0;
        e->state  = SLOT_PENDING;

        nsapi_value_or_error_t r = _net->gethostbyname_async(
            host, mbed::Callback<void(int, SocketAddress*)>(&NameResolver::_on_dns, e),
            NSAPI_IPv4);

        if (r == NSAPI_ERROR_OK) {
            // Resolved synchronously: the callback already published the result.
            uint8_t st = HAL::ordered_read(&e->state);
            if (st == SLOT_READY) { *out = e->addr; return RESOLVE_OK; }
            e->state = SLOT_FREE;
            return RESOLVE_FAILED;
        }
        if (r < 0) {                       // immediate failure; the callback won't fire
            e->state = SLOT_FREE;
            if (_logger != nullptr) _logger->warning(PlcIO::NETWORK, "DNS lookup failed to start");
            return RESOLVE_FAILED;
        }
        e->handle = (int)r;                // positive unique id; the callback fires later
        return RESOLVE_PENDING;
#else
        // Simulator: no Mbed stack. Literals were handled above; a real hostname can't
        // be resolved here (the sim's network components are mocked), so report failure.
        return RESOLVE_FAILED;
#endif
    }

#ifdef TARGET_OPTA
    void NameResolver::_on_dns(Entry* e, int result, SocketAddress* address) {
        // Runs in the network-stack context. Keep it short (Mbed asks for < 10ms and no
        // socket/blocking ops). Write addr first, then publish state with a release
        // barrier so the main loop never reads addr ahead of a READY state.
        if (result > 0 && address != nullptr) {
            nsapi_addr_t a = address->get_addr();
            e->addr.bytes[0] = a.bytes[0];
            e->addr.bytes[1] = a.bytes[1];
            e->addr.bytes[2] = a.bytes[2];
            e->addr.bytes[3] = a.bytes[3];
            HAL::dsb();
            e->state = SLOT_READY;
        } else {
            HAL::dsb();
            e->state = SLOT_ERROR;
        }
    }
#endif

}}
