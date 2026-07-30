/**
 * @file test_time.cpp
 * @brief Tests for utils/time.cpp — 32-bit rollover projection and the
 *        calendar/UNIX conversion + timestamp formatting.
 *
 * @c cal_time_ms() folds in a boot epoch sampled from the wall clock, so its
 * absolute output is not fixed across runs. Rather than pin a date, these tests
 * assert the conversion's internal consistency: the civil fields it emits must
 * decode back to the same @c unixSeconds it reports, cross-checked against an
 * independent implementation of Howard Hinnant's date algorithm.
 */
#include <cstddef>
#include <cstdint>

#include "doctest.h"

#include "libbotanik/utils/time.h"

using namespace Botanik::Utils;

// Independent reference: days since 1970-01-01 for a civil (y, m, d) date.
// Howard Hinnant's algorithm — the inverse of the decomposition in time.cpp,
// so agreement cross-checks the implementation rather than mirroring it.
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t  era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

// _project_rollover
// ----------------------------------------------------------------------------





// absolute_ms / absolute_us
// ----------------------------------------------------------------------------


// cal_time_ms
// ----------------------------------------------------------------------------

TEST_CASE("cal_time_ms: civil fields decode the reported unixSeconds") {
    // Sweep a wide span of uptimes (offset from whatever boot epoch this run
    // sampled). Each decomposition must be self-consistent.
    const uint64_t uptimes[] = {0ULL, 1ULL, 999ULL, 1000ULL, 86400000ULL,
                                1000000000ULL, 9999999999ULL};
    for (uint64_t t : uptimes) {
        CalendarTime ct = cal_time_ms(t);

        CHECK(ct.month  >= 1);  CHECK(ct.month  <= 12);
        CHECK(ct.day    >= 1);  CHECK(ct.day    <= 31);
        CHECK(ct.hour   <  24);
        CHECK(ct.minute <  60);
        CHECK(ct.second <  60);
        CHECK(ct.millisecond < 1000);

        // unixDays is the day index of unixSeconds...
        CHECK(ct.unixDays == (uint32_t)(ct.unixSeconds / 86400));
        // ...and the civil date independently re-derives that same day index.
        CHECK(days_from_civil(ct.year, ct.month, ct.day) == (int64_t)ct.unixDays);
        // ...and the clock fields account for the seconds within the day.
        CHECK((uint64_t)ct.hour * 3600 + (uint64_t)ct.minute * 60 + ct.second
              == ct.unixSeconds % 86400);
    }
}

TEST_CASE("cal_time_ms: advancing the timestamp advances unixSeconds in lockstep") {
    // The boot epoch is fixed for the process, so differences are exact.
    CalendarTime base = cal_time_ms(0);
    CHECK(cal_time_ms(5000).unixSeconds  == base.unixSeconds + 5);
    CHECK(cal_time_ms(60000).unixSeconds == base.unixSeconds + 60);
    // Sub-second carry: +1500 ms adds one second plus 500 ms of fraction.
    CalendarTime plus = cal_time_ms(1500);
    CHECK(plus.unixSeconds == base.unixSeconds + 1 + (base.millisecond + 500) / 1000);
}
