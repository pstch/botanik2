/**
 * @file test_hal_net.cpp
 * @brief The simulator's network HAL, where a sentinel and a free marker share
 *        a value.
 *
 * These are sim-backend cases on purpose. The OPTA half is not compiled here, so
 * what they guard is the SIMULATOR agreeing with the contract in net.h - a sim
 * that answers where the hardware refuses hides the caller bug until it is on a
 * greenhouse.
 */

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/hal/net.h"

namespace hal_net {

using namespace Botanik;
using namespace Botanik::HAL::Network;

#if BOTANIK_FEATURE_NETWORK

/** @brief Poll @p h to a terminal result. The emulated resolver has a real
 *  25 ms latency, so this waits on the clock rather than spinning a fixed
 *  number of times. */
static DnsResult resolve(DnsHandle h, Ip4* out) {
    const uint64_t give_up = HAL::Clock::uptime_ms() + 2000;
    for (;;) {
        const DnsResult r = dns_poll(h, out);
        if (r != DNS_PENDING) return r;
        if (HAL::Clock::uptime_ms() > give_up) return DNS_PENDING;
    }
}

TEST_CASE("hal net: the invalid handle resolves nothing") {
    // net.h: "0 == invalid / could-not-start". The sim's free slots are marked
    // by id 0 as well, so a poll of the sentinel matched the first free one -
    // whose deadline is 0, already passed - and answered DNS_OK with the
    // loopback address. A caller that ignored a 0 from dns_begin() got a
    // successful lookup on the sim and a failure on the OPTA.
    Ip4 addr;
    memset(&addr, 0xAA, sizeof(addr));
    CHECK(dns_poll(0, &addr) == DNS_FAILED);
    dns_cancel(0);                        // idempotent, and must not free a slot

    // ...and the sentinel really is what a refused start hands back.
    CHECK(dns_begin(nullptr) == 0);
    CHECK(dns_begin("") == 0);
    CHECK(dns_begin("10.0.0.1") == 0);    // a numeric literal is not a lookup
}

TEST_CASE("hal net: a real lookup still resolves after a poll of the sentinel") {
    // The guard rejects the sentinel rather than consuming a slot: polling 0
    // must not free the first one on its way out.
    for (int i = 0; i < 8; i++) CHECK(dns_poll(0, nullptr) == DNS_FAILED);

    const DnsHandle h = dns_begin("broker.local");
    REQUIRE(h != 0);
    Ip4 addr;
    const DnsResult r = resolve(h, &addr);
    CHECK(r == DNS_OK);
}

TEST_CASE("hal net: an unresolvable name fails rather than resolving to loopback") {
    const DnsHandle h = dns_begin("nothing.invalid");
    REQUIRE(h != 0);
    Ip4 addr;
    const DnsResult r = resolve(h, &addr);
    CHECK(r == DNS_FAILED);
}

TEST_CASE("hal net: a terminal result consumes the handle") {
    const DnsHandle h = dns_begin("broker.local");
    REQUIRE(h != 0);
    Ip4 addr;
    const DnsResult r = resolve(h, &addr);
    REQUIRE(r == DNS_OK);
    // Polling a consumed handle must not answer OK a second time, or a caller
    // holding a stale one reads a resolution it never asked for.
    CHECK(dns_poll(h, &addr) == DNS_FAILED);
}

#endif

}  // namespace hal_net
