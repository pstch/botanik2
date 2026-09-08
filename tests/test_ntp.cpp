/**
 * @file test_ntp.cpp
 * @brief The SNTP timestamp conversion, across the 2036 wrap.
 *
 * The seconds field is 32 bits counted from 1900 and runs out on 2036-02-07.
 * A unit that reads it as one era stops disciplining its clock that morning,
 * silently: every reply looks pre-1970 and is discarded, the last good time
 * keeps ticking on the RTC, and nothing says why the drift is growing.
 */

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/network/ntp.h"

namespace tests { namespace ntp_time {

using namespace Botanik;
using namespace Botanik::Network;

#if BOTANIK_FEATURE_NTP

/** @brief A 48-byte SNTP reply carrying @p secs / @p frac as its transmit
 *  timestamp, which is the only field the conversion reads. */
struct Packet {
    uint8_t b[48];
    Packet(uint32_t secs, uint32_t frac = 0) {
        memset(b, 0, sizeof(b));
        b[40] = (uint8_t)(secs >> 24); b[41] = (uint8_t)(secs >> 16);
        b[42] = (uint8_t)(secs >> 8);  b[43] = (uint8_t)secs;
        b[44] = (uint8_t)(frac >> 24); b[45] = (uint8_t)(frac >> 16);
        b[46] = (uint8_t)(frac >> 8);  b[47] = (uint8_t)frac;
    }
};

TEST_CASE("ntp: era 0 is seconds since 1900") {
    // 2208988800 is the epoch itself: 1970-01-01T00:00:00Z.
    CHECK(ntp_to_unix_ms(Packet(NTP_UNIX_DELTA).b) == 0ull);
    CHECK(ntp_to_unix_ms(Packet(NTP_UNIX_DELTA + 1).b) == 1000ull);

    // A time in living memory: 2026-09-03T00:00:00Z is 1772496000 UNIX.
    CHECK(ntp_to_unix_ms(Packet(NTP_UNIX_DELTA + 1772496000u).b)
          == 1772496000000ull);
}

TEST_CASE("ntp: the fraction becomes milliseconds") {
    // The fraction is a binary fraction of a second: half of 2^32 is 500 ms.
    CHECK(ntp_to_unix_ms(Packet(NTP_UNIX_DELTA, 0x80000000u).b) == 500ull);
    CHECK(ntp_to_unix_ms(Packet(NTP_UNIX_DELTA, 0x40000000u).b) == 250ull);
    CHECK(ntp_to_unix_ms(Packet(NTP_UNIX_DELTA, 0xFFFFFFFFu).b) == 999ull);
}

TEST_CASE("ntp: an all-zero timestamp is unsynchronised, and only that") {
    // RFC 5905's "unknown or unsynchronised". It stays invalid because a server
    // that has no time sends zeros - but only the all-zero case, since in era 1
    // second zero is a real instant.
    CHECK(ntp_to_unix_ms(Packet(0, 0).b) == 0ull);
    CHECK(ntp_to_unix_ms(Packet(0, 1).b) != 0ull);
}

TEST_CASE("ntp: the 2036 wrap keeps time moving forward") {
    // The last second of era 0 and the first of era 1 are one second apart,
    // even though the field goes from 0xFFFFFFFF to 0.
    const uint64_t last_era0 = ntp_to_unix_ms(Packet(0xFFFFFFFFu).b);
    const uint64_t first_era1 = ntp_to_unix_ms(Packet(0u, 1u).b);
    INFO("era 0 ends at " << last_era0 << " ms, era 1 starts at " << first_era1);
    CHECK(first_era1 > last_era0);
    CHECK(first_era1 - last_era0 == 1000ull);

    // 2036-02-07T06:28:16Z, the wrap instant itself.
    CHECK(ntp_to_unix_ms(Packet(0u, 1u).b) / 1000ull == (uint64_t)NTP_ERA1_UNIX);
}

TEST_CASE("ntp: a reply from after 2036 is a time, not a rejection") {
    // secs with the top bit clear is era 1, not pre-1970. Read as pre-1970 it
    // is discarded, and from 2036-02-07 onward every reply goes with it.
    const uint32_t a_year = 31556952u;
    for (uint32_t years = 1; years <= 60; years++) {
        const uint32_t secs = years * a_year;         // top bit clear: era 1
        INFO(years << " years past the wrap, secs field " << secs);
        REQUIRE((secs & 0x80000000u) == 0u);
        const uint64_t ms = ntp_to_unix_ms(Packet(secs).b);
        CHECK(ms != 0ull);
        CHECK(ms / 1000ull == (uint64_t)NTP_ERA1_UNIX + secs);
    }
}

TEST_CASE("ntp: the era boundary is the top bit, not a date comparison") {
    // 0x80000000 is 1968-01-20, the first second of era 0's representable half.
    CHECK(ntp_to_unix_ms(Packet(0x80000000u).b) != 0ull);
    // One below it is era 1: 2104-ish, the far end.
    const uint64_t far = ntp_to_unix_ms(Packet(0x7FFFFFFFu).b);
    CHECK(far / 1000ull == (uint64_t)NTP_ERA1_UNIX + 0x7FFFFFFFull);
}

#endif

}}  // namespace tests::ntp_time
