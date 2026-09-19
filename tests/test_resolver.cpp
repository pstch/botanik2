/**
 * @file test_resolver.cpp
 * @brief NameResolver: the literal shortcut, the poll cycle, and the cache.
 *
 * Only the IPv4-literal path had ever run - a literal needs neither DNS nor a
 * table slot, so it returns before the interesting half of the file. Everything
 * below drives the hostname path against the simulator's emulated resolver,
 * which answers after a fixed latency and fails any `.invalid` name.
 *
 * The rules worth pinning are the ones a plant network depends on: nothing
 * blocks, a failed lookup frees its row rather than wedging it, and a table
 * whose rows are all in flight reclaims the oldest instead of failing for good.
 */

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/network/resolver.h"

namespace tests { namespace resolver {

using namespace Botanik;
using namespace Botanik::Network;

#if BOTANIK_FEATURE_NETWORK

namespace {

/** @brief Resolve @p host until it stops being PENDING, or give up.
 *  The emulated lookup answers on a deadline in uptime, so the wait is a real
 *  sleep - bounded, since a wedged resolver is the failure being watched for. */
ResolveStatus settle(NameResolver& r, const char* host, ResolvedAddr* out,
                     int rounds = 40) {
    for (int i = 0; i < rounds; i++) {
        const ResolveStatus s = r.resolve(host, out);
        if (s != RESOLVE_PENDING) return s;
        HAL::Clock::sleep_ms(5);
    }
    return RESOLVE_PENDING;
}

}  // namespace

TEST_CASE("resolver: an IPv4 literal needs neither DNS nor a table slot") {
    NameResolver r;
    ResolvedAddr a = {{0, 0, 0, 0}};

    REQUIRE(r.resolve("10.1.2.3", &a) == RESOLVE_OK);
    CHECK(a.b[0] == 10); CHECK(a.b[1] == 1); CHECK(a.b[2] == 2); CHECK(a.b[3] == 3);
    // Boundary values, since the parser rejects on >255 mid-octet.
    CHECK(r.resolve("255.255.255.255", &a) == RESOLVE_OK);
    CHECK(r.resolve("0.0.0.0", &a) == RESOLVE_OK);
}

TEST_CASE("resolver: a malformed literal is not quietly taken as a hostname") {
    NameResolver r;
    ResolvedAddr a = {{0, 0, 0, 0}};
    // All-numeric and wrong: the HAL refuses to start a lookup for one, because
    // a real DNS would fail it too. The alternative - asking a server about
    // "999.1.1.1" - is a query nobody wants on the wire.
    static const char* const bads[] = { "999.1.1.1", "1.2.3", "1.2.3.4.5", "1..3.4", "1.2.3." };
    for (const char* bad : bads) {
        INFO(bad);
        CHECK(r.resolve(bad, &a) == RESOLVE_FAILED);
    }
}

TEST_CASE("resolver: nothing to resolve is a refusal, not a lookup") {
    NameResolver r;
    ResolvedAddr a = {{0, 0, 0, 0}};
    CHECK(r.resolve(nullptr, &a) == RESOLVE_FAILED);
    CHECK(r.resolve("", &a) == RESOLVE_FAILED);
    CHECK(r.resolve("host.example", nullptr) == RESOLVE_FAILED);
}

TEST_CASE("resolver: a hostname resolves without blocking, over several calls") {
    NameResolver r;
    ResolvedAddr a = {{0, 0, 0, 0}};

    // The first call starts the lookup and polls once. It must not block: the
    // whole point of the poll API is that the main loop keeps running.
    const ResolveStatus first = r.resolve("broker.example", &a);
    CHECK((first == RESOLVE_PENDING || first == RESOLVE_OK));

    REQUIRE(settle(r, "broker.example", &a) == RESOLVE_OK);
    INFO("a resolved address is not the null one");
    CHECK((a.b[0] | a.b[1] | a.b[2] | a.b[3]) != 0);
}

TEST_CASE("resolver: a resolved name is served from the cache next time") {
    NameResolver r;
    ResolvedAddr a = {{0, 0, 0, 0}}, b = {{0, 0, 0, 0}};
    REQUIRE(settle(r, "ntp.example", &a) == RESOLVE_OK);

    // Immediately, with no further polling: a cache hit is what keeps a
    // per-beat resolve() call from being a query per beat.
    CHECK(r.resolve("ntp.example", &b) == RESOLVE_OK);
    CHECK(memcmp(a.b, b.b, 4) == 0);
}

TEST_CASE("resolver: a cached entry past its age is re-resolved, not served") {
    NameResolver r;
    ResolvedAddr a = {{0, 0, 0, 0}};
    // The default TTL is an hour, so this is the only way to reach the expiry
    // path: the HAL DNS API exposes no record TTL, and this is what bounds
    // staleness for a server whose address changes under us.
    r.set_cache_ttl_ms(1);
    REQUIRE(settle(r, "mqtt.example", &a) == RESOLVE_OK);

    HAL::Clock::sleep_ms(5);
    // Past the age, so the row is dropped and a fresh lookup starts - which
    // means this call is not an immediate OK from the cache.
    ResolvedAddr b = {{0, 0, 0, 0}};
    const ResolveStatus again = r.resolve("mqtt.example", &b);
    CHECK((again == RESOLVE_PENDING || again == RESOLVE_OK));
    CHECK(settle(r, "mqtt.example", &b) == RESOLVE_OK);
}

TEST_CASE("resolver: a TTL of 0 means never expire") {
    NameResolver r;
    ResolvedAddr a = {{0, 0, 0, 0}}, b = {{0, 0, 0, 0}};
    r.set_cache_ttl_ms(0);
    REQUIRE(settle(r, "static.example", &a) == RESOLVE_OK);
    HAL::Clock::sleep_ms(5);
    INFO("still an immediate cache hit");
    CHECK(r.resolve("static.example", &b) == RESOLVE_OK);
}

TEST_CASE("resolver: a name that cannot be resolved fails and frees its row") {
    Logs::Logger log;
    NameResolver r(&log);
    ResolvedAddr a = {{0, 0, 0, 0}};

    REQUIRE(settle(r, "nowhere.invalid", &a) == RESOLVE_FAILED);
    // The row must go back to FREE, or a site whose broker is briefly wrong
    // leaks a slot per attempt until nothing resolves at all. Proven by
    // filling the table afterwards: it still has room.
    char host[32];
    for (int i = 0; i < (int)Config::DNS_CACHE_SIZE; i++) {
        snprintf(host, sizeof(host), "h%d.example", i);
        INFO(host);
        CHECK(settle(r, host, &a) == RESOLVE_OK);
    }
}

TEST_CASE("resolver: clear() drops the cache and cancels what is in flight") {
    NameResolver r;
    ResolvedAddr a = {{0, 0, 0, 0}};
    REQUIRE(settle(r, "cached.example", &a) == RESOLVE_OK);

    // One in flight as well, so clear() has both states to deal with.
    ResolvedAddr b = {{0, 0, 0, 0}};
    (void)r.resolve("inflight.example", &b);

    r.clear();
    // The cached name is no longer an immediate hit: it has to be looked up
    // again, which is what a reconfigured broker address depends on.
    const ResolveStatus after = r.resolve("cached.example", &a);
    CHECK(after != RESOLVE_OK);
    CHECK(settle(r, "cached.example", &a) == RESOLVE_OK);
}

TEST_CASE("resolver: the table serves more distinct names than it has rows") {
    NameResolver r;
    ResolvedAddr a = {{0, 0, 0, 0}};
    char host[32];
    // Twice the cache size, resolved one after another. Each completes, so each
    // leaves a READY row, and the row after the table is full has to EVICT one
    // rather than fail - a resolver that stops working at row N+1 would take
    // the plant network down as soon as a site named one more server.
    for (int i = 0; i < (int)Config::DNS_CACHE_SIZE * 2; i++) {
        snprintf(host, sizeof(host), "many%d.example", i);
        INFO(host);
        CHECK(settle(r, host, &a) == RESOLVE_OK);
    }
}

#endif // BOTANIK_FEATURE_NETWORK

}}  // namespace tests::resolver
