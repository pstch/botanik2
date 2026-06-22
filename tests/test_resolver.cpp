/**
 * @file test_resolver.cpp
 * @brief Tests for NameResolver. A dotted-decimal IPv4 literal resolves immediately
 *        (no DNS / table slot); a hostname goes through HAL::Net DNS (getaddrinfo on
 *        the simulator). Uses .invalid names so the suite is deterministic offline.
 */
#include <cstdint>

#include "doctest.h"

#include "libbotanik/network/resolver.h"

using namespace Botanik;
using namespace Botanik::Network;

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
    // None are valid 4-octet dotted-decimal literals, and DNS rejects them as names,
    // so each must come back FAILED (never a bogus RESOLVE_OK).
    // Note: "1.2.3" is intentionally omitted: Linux getaddrinfo treats 3-part notation
    // as a valid address (1.2.0.3), so it resolves rather than failing.
    CHECK(r.resolve("1.2.3.4.5", &a) == RESOLVE_FAILED);   // too many octets
    CHECK(r.resolve("256.1.1.1", &a) == RESOLVE_FAILED);   // octet out of range
    CHECK(r.resolve("1.2.3.x", &a)   == RESOLVE_FAILED);   // non-digit
    CHECK(r.resolve("1.2..4", &a)    == RESOLVE_FAILED);   // empty octet
    CHECK(r.resolve("10.0.0.1 ", &a) == RESOLVE_FAILED);   // trailing junk
}

TEST_CASE("NameResolver: a guaranteed-bad hostname resolves to FAILED") {
    NameResolver r;
    ResolvedAddr a;
    // .invalid (RFC 6761) never resolves, online or off - deterministic FAILED.
    CHECK(r.resolve("nonexistent.invalid", &a) == RESOLVE_FAILED);
}

TEST_CASE("NameResolver: empty / null inputs fail cleanly") {
    NameResolver r;
    ResolvedAddr a;
    CHECK(r.resolve("", &a)             == RESOLVE_FAILED);
    CHECK(r.resolve(nullptr, &a)        == RESOLVE_FAILED);
    CHECK(r.resolve("1.2.3.4", nullptr) == RESOLVE_FAILED);
}
