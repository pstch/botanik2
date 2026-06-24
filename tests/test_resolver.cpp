/**
 * @file test_resolver.cpp
 * @brief Tests for NameResolver. A dotted-decimal IPv4 literal resolves
 *        immediately (no DNS / table slot); a hostname goes through HAL::Net DNS
 *        (getaddrinfo on the simulator). Uses .invalid names so the suite is
 *        deterministic offline.
 */
#include <cstdint>

#include "doctest.h"

#include "libbotanik/network/resolver.h"
// HAL::Clock::sleep_ms / uptime_ms (poll the async DNS)
#include "libbotanik/hal/platform.h"

using namespace Botanik;
using namespace Botanik::Network;

namespace {
    // Drive resolve() to a terminal status. A literal / malformed-numeric name
    // settles on the first call; a real name goes through async DNS (getaddrinfo_a)
    // and returns PENDING until the lookup lands. Bounded so a hang fails the test,
    // not spins.
    ResolveStatus settle(NameResolver& r, const char* host, ResolvedAddr* a) {
        uint64_t start = HAL::Clock::uptime_ms();
        for (;;) {
            ResolveStatus s = r.resolve(host, a);
            if (s != RESOLVE_PENDING) return s;
            if (HAL::Clock::uptime_ms() - start > 15000) return s;
            HAL::Clock::sleep_ms(5);
        }
    }
}

TEST_CASE("NameResolver: a dotted-decimal IPv4 literal resolves to its octets") {
    NameResolver r;
    ResolvedAddr a;

    REQUIRE(r.resolve("192.168.1.10", &a) == RESOLVE_OK);
    CHECK(a.bytes[0] == 192);
    CHECK(a.bytes[1] == 168);
    CHECK(a.bytes[2] == 1);
    CHECK(a.bytes[3] == 10);

    REQUIRE(r.resolve("0.0.0.0", &a) == RESOLVE_OK);
    CHECK((a.bytes[0] | a.bytes[1] | a.bytes[2] | a.bytes[3]) == 0);

    REQUIRE(r.resolve("255.255.255.255", &a) == RESOLVE_OK);
    CHECK(a.bytes[0] == 255);
    CHECK(a.bytes[3] == 255);
}

TEST_CASE("NameResolver: malformed literals are not mis-parsed as addresses") {
    NameResolver r;
    ResolvedAddr a;
    // None are valid 4-octet dotted-decimal literals, so parse_ipv4 must reject
    // each (never a bogus RESOLVE_OK with the wrong octets). The all-numeric forms
    // are also rejected by the sim resolver (treated as a bad name), as is junk
    // with a character illegal in a hostname - so each comes back FAILED.
    CHECK(settle(r, "1.2.3", &a)     == RESOLVE_FAILED);   // too few octets
    CHECK(settle(r, "1.2.3.4.5", &a) == RESOLVE_FAILED);   // too many octets
    CHECK(settle(r, "256.1.1.1", &a) == RESOLVE_FAILED);   // octet out of range
    CHECK(settle(r, "1.2.3.#", &a)   == RESOLVE_FAILED);   // non-digit, illegal host char
    CHECK(settle(r, "1.2..4", &a)    == RESOLVE_FAILED);   // empty octet
    CHECK(settle(r, "10.0.0.1 ", &a) == RESOLVE_FAILED);   // trailing junk
}

TEST_CASE("NameResolver: a guaranteed-bad hostname resolves to FAILED") {
    NameResolver r;
    ResolvedAddr a;
    // .invalid (RFC 6761) never resolves, online or off - deterministic FAILED.
    CHECK(settle(r, "nonexistent.invalid", &a) == RESOLVE_FAILED);
}

TEST_CASE("NameResolver: a resolvable hostname resolves to the loopback peer") {
    NameResolver r;
    ResolvedAddr a;
    // The sim resolver is hermetic: any well-formed name maps to this instance's
    // per-process loopback (127/8, mock_loopback_ip()) where the in-process mock
    // responder listens, after an async PENDING phase that settle() polls through.
    REQUIRE(settle(r, "broker.local", &a) == RESOLVE_OK);
    CHECK(a.bytes[0] == 127);          // loopback (exact host part is PID-derived)
}

TEST_CASE("NameResolver: empty / null inputs fail cleanly") {
    NameResolver r;
    ResolvedAddr a;
    CHECK(r.resolve("", &a)             == RESOLVE_FAILED);
    CHECK(r.resolve(nullptr, &a)        == RESOLVE_FAILED);
    CHECK(r.resolve("1.2.3.4", nullptr) == RESOLVE_FAILED);
}
