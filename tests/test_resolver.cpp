/**
 * @file test_resolver.cpp
 * @brief Tests for NameResolver's host-side behaviour. A dotted-decimal IPv4 literal
 *        resolves immediately (and uses no DNS / table slot); anything else needs the
 *        Mbed async-DNS path, which is absent on the host build, so it reports FAILED.
 *        The async resolution + cache/table logic is OPTA-only and compile-checked,
 *        not exercised here.
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
    // None are valid literals; with no interface set, the host has no DNS to fall back
    // on, so each must come back FAILED (never a bogus RESOLVE_OK).
    CHECK(r.resolve("1.2.3", &a)     == RESOLVE_FAILED);   // too few octets
    CHECK(r.resolve("1.2.3.4.5", &a) == RESOLVE_FAILED);   // too many octets
    CHECK(r.resolve("256.1.1.1", &a) == RESOLVE_FAILED);   // octet out of range
    CHECK(r.resolve("1.2.3.x", &a)   == RESOLVE_FAILED);   // non-digit
    CHECK(r.resolve("1.2..4", &a)    == RESOLVE_FAILED);   // empty octet
    CHECK(r.resolve("10.0.0.1 ", &a) == RESOLVE_FAILED);   // trailing junk
}

TEST_CASE("NameResolver: a hostname needs DNS, absent on the host -> FAILED") {
    NameResolver r;
    ResolvedAddr a;
    CHECK(r.resolve("pool.ntp.org", &a) == RESOLVE_FAILED);
}

TEST_CASE("NameResolver: empty / null inputs fail cleanly") {
    NameResolver r;
    ResolvedAddr a;
    CHECK(r.resolve("", &a)             == RESOLVE_FAILED);
    CHECK(r.resolve(nullptr, &a)        == RESOLVE_FAILED);
    CHECK(r.resolve("1.2.3.4", nullptr) == RESOLVE_FAILED);
}
