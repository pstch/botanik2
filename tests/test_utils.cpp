/**
 * @file test_utils.cpp
 * @brief Utility layer: text formatting, uptime, buffered wire.serial, memory barriers.
 *
 * Each section below is the former test_<name>.cpp, kept in its own
 * namespace so their fixtures and helpers cannot collide.
 */

#include <cstdlib>                      // setenv / unsetenv
#include <cstring>                      // strcmp / strlen
#include <limits>                       // std::numeric_limits
#include "libbotanik/hal/platform.h"    // HAL::Env::u32 / u8
#include "libbotanik/utils/text.h"      // Utils::format_* / parse_uint / compare_str
#include "doctest.h"
#include <cstdint>                  // int64_t/uint64_t
#include "libbotanik/utils/time.h"  // Utils::CalendarTime + the uptime/wall conversions
#include <unistd.h>                     // pipe / read / write
#include "libbotanik_config.h"          // Config::SERIAL_RX_BUF / TX_BUF
#include "libbotanik/utils/serial.h"    // Utils::BufferedSerial

// --------------------------------------------------------------------------
// text
// --------------------------------------------------------------------------

namespace text {

using Botanik::Utils::format_float;
using Botanik::Utils::format_int;
using Botanik::Utils::format_age;
using Botanik::Utils::format_size;
// HAL::Env (moved out of Utils: reading the environment is a platform call)
static const auto env_u8  = Botanik::HAL::Env::u8;
static const auto env_u32 = Botanik::HAL::Env::u32;

TEST_CASE("format_int: unsigned magnitudes and a tight cap") {
    char b[24];
    CHECK(std::strcmp(format_int(b, sizeof(b), 0)          , "0")          == 0);
    CHECK(std::strcmp(format_int(b, sizeof(b), 7)          , "7")          == 0);
    CHECK(std::strcmp(format_int(b, sizeof(b), 4294967295LL), "4294967295") == 0);  // 2^32-1
    CHECK(std::strcmp(format_int(b, 4, 12345)              , "123")        == 0);   // truncated, not overrun
    CHECK(format_int(b, 0, 9) == b);                                                // cap 0 leaves buf untouched
}

TEST_CASE("format_int: signed decimal and LLONG_MIN") {
    char b[24];
    CHECK(std::strcmp(format_int(b, sizeof(b), 0)      , "0")     == 0);
    CHECK(std::strcmp(format_int(b, sizeof(b), 42)     , "42")    == 0);
    CHECK(std::strcmp(format_int(b, sizeof(b), -42)    , "-42")   == 0);
    // LLONG_MIN must negate via the unsigned magnitude, not overflow.
    CHECK(std::strcmp(format_int(b, sizeof(b), std::numeric_limits<long long>::min()),
                      "-9223372036854775808") == 0);
}

TEST_CASE("format_size: bytes below 32 KiB, KB at/above (rounded)") {
    char b[16];
    CHECK(std::strcmp(format_size(b, sizeof(b), 0)     , "0 B")     == 0);
    CHECK(std::strcmp(format_size(b, sizeof(b), 1024)  , "1024 B")  == 0);
    CHECK(std::strcmp(format_size(b, sizeof(b), 32767) , "32767 B") == 0);
    CHECK(std::strcmp(format_size(b, sizeof(b), 32768) , "32 KB")   == 0);  // threshold
    CHECK(std::strcmp(format_size(b, sizeof(b), 49152) , "48 KB")   == 0);
    CHECK(std::strcmp(format_size(b, sizeof(b), 65536) , "64 KB")   == 0);
}

// format_float
// ----------------------------------------------------------------------------

TEST_CASE("format_float: basic fixed-point") {
    char b[32];
    CHECK(std::strcmp(format_float(b, sizeof(b), 3.14159, 2), "3.14") == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b), 3.14159, 0), "3")    == 0);  // no point
    CHECK(std::strcmp(format_float(b, sizeof(b), 1.0,     1), "1.0")  == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b), 0.0,     1), "0.0")  == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b), 12.345,  3), "12.345") == 0);
}

TEST_CASE("format_float: rounds half away from zero") {
    char b[32];
    CHECK(std::strcmp(format_float(b, sizeof(b),  2.5,  0),  "3") == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b), -2.5,  0), "-3") == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b),  1.25, 1), "1.3") == 0);  // tie rounds up
    CHECK(std::strcmp(format_float(b, sizeof(b),  9.99, 1), "10.0") == 0); // carry into int part
}

TEST_CASE("format_float: decimals capped at 9") {
    char b[32];
    // 12 requested decimals clamps to 9.
    const char* r = format_float(b, sizeof(b), 1.0, 12);
    CHECK(std::strlen(r) == std::strlen("1.000000000")); // "1." + 9 zeros
}

TEST_CASE("format_float: output is truncated, not overrun, when cap is tight") {
    char b[8];
    // "3.14" needs 5 bytes incl. NUL; cap 4 keeps 3 chars + NUL, dropping the
    // least-significant tail.
    CHECK(std::strcmp(format_float(b, 4, 3.14159, 2), "3.1") == 0);
    // cap 1 leaves room only for the terminator.
    CHECK(std::strcmp(format_float(b, 1, 3.14159, 2), "") == 0);
}

TEST_CASE("format_float: cap 0 leaves the buffer untouched") {
    char b[8] = "guard";
    CHECK(format_float(b, 0, 3.14, 2) == b); // returns buf
    CHECK(std::strcmp(b, "guard") == 0);     // nothing written
}

TEST_CASE("format_float: non-finite inputs render as tags") {
    char b[32];
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(std::strcmp(format_float(b, sizeof(b),  nan, 2),  "nan") == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b),  inf, 2),  "inf") == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b), -inf, 2), "-inf") == 0);
}

// format_age
// ----------------------------------------------------------------------------

TEST_CASE("format_age: picks the largest sensible unit") {
    char b[16];
    CHECK(std::strcmp(format_age(b, sizeof(b),     0), "  0 s ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b),    45), " 45 s ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b),    59), " 59 s ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b),    60), "  1 m ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b),  3599), " 59 m ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b),  3600), "  1 h ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b), 86399), " 23 h ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b), 86400), "  1 d ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b), 172800), "  2 d ago") == 0);
}

// env_u32 / env_u8
// ----------------------------------------------------------------------------

// Baud rates and the like don't fit a byte, so env_u32 is the general reader and
// env_u8 is the range-checked narrowing of it.
TEST_CASE("env_u32: parses the full 32-bit range, falls back on junk") {
    setenv("BOTANIK_TEST_U32", "4800", 1);
    CHECK(env_u32("BOTANIK_TEST_U32", 9) == 4800u);
    setenv("BOTANIK_TEST_U32", "115200", 1);
    CHECK(env_u32("BOTANIK_TEST_U32", 9) == 115200u);
    setenv("BOTANIK_TEST_U32", "0", 1);
    CHECK(env_u32("BOTANIK_TEST_U32", 9) == 0u);
    setenv("BOTANIK_TEST_U32", "4294967295", 1);
    CHECK(env_u32("BOTANIK_TEST_U32", 9) == 4294967295u);

    unsetenv("BOTANIK_TEST_U32");
    CHECK(env_u32("BOTANIK_TEST_U32", 9) == 9u);
    setenv("BOTANIK_TEST_U32", "", 1);
    CHECK(env_u32("BOTANIK_TEST_U32", 9) == 9u);
    setenv("BOTANIK_TEST_U32", "12x", 1);
    CHECK(env_u32("BOTANIK_TEST_U32", 9) == 9u);
    unsetenv("BOTANIK_TEST_U32");
}


TEST_CASE("env_u8: parses valid values, clamps the rest to the fallback") {
    setenv("BOTANIK_TEST_U8", "3", 1);
    CHECK(env_u8("BOTANIK_TEST_U8", 9) == 3);
    setenv("BOTANIK_TEST_U8", "0", 1);
    CHECK(env_u8("BOTANIK_TEST_U8", 9) == 0);
    setenv("BOTANIK_TEST_U8", "255", 1);
    CHECK(env_u8("BOTANIK_TEST_U8", 9) == 255);

    // Unset, empty, out of range, and non-numeric all fall back.
    unsetenv("BOTANIK_TEST_U8");
    CHECK(env_u8("BOTANIK_TEST_U8", 9) == 9);
    setenv("BOTANIK_TEST_U8", "", 1);
    CHECK(env_u8("BOTANIK_TEST_U8", 9) == 9);
    setenv("BOTANIK_TEST_U8", "256", 1);
    CHECK(env_u8("BOTANIK_TEST_U8", 9) == 9);
    setenv("BOTANIK_TEST_U8", "-1", 1);
    CHECK(env_u8("BOTANIK_TEST_U8", 9) == 9);
    setenv("BOTANIK_TEST_U8", "4x", 1);
    CHECK(env_u8("BOTANIK_TEST_U8", 9) == 9);
    unsetenv("BOTANIK_TEST_U8");
}

}  // namespace text

// --------------------------------------------------------------------------
// time
// --------------------------------------------------------------------------

namespace uptime {

using namespace Botanik::Utils;

// Independent reference: days since 1970-01-01 for a civil (y, m, d) date.
// Howard Hinnant's algorithm - the inverse of the decomposition in time.cpp,
// so agreement cross-checks the implementation rather than mirroring it.
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t  era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

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

}  // namespace uptime

// --------------------------------------------------------------------------
// serial
// --------------------------------------------------------------------------

namespace serial {

using Botanik::Utils::BufferedSerial;

namespace {
    struct Pipe {
        int fds[2];
        Pipe()  { REQUIRE(::pipe(fds) == 0); }
        int r() const { return fds[0]; }
        int w() const { return fds[1]; }
    };
}

// RX ring
// ----------------------------------------------------------------------------

TEST_CASE("serial RX: receive() fills the ring, read() drains it in order") {
    Pipe in;
    Botanik::HAL::Serial::Raw raw;
    raw.set_fds(in.r(), -1);
    BufferedSerial bs(&raw);

    CHECK(bs.rx_empty());
    REQUIRE(::write(in.w(), "hello", 5) == 5);
    CHECK(bs.receive() == 5);
    CHECK(bs.available() == 5);
    CHECK_FALSE(bs.rx_empty());

    uint8_t b = 0;
    CHECK(bs.peek(&b));                 // peek does not consume
    CHECK(b == 'h');
    CHECK(bs.available() == 5);

    char out[8] = {0};
    CHECK(bs.read(out, 5) == 5);
    CHECK(std::memcmp(out, "hello", 5) == 0);
    CHECK(bs.available() == 0);
    CHECK(bs.rx_empty());
    CHECK_FALSE(bs.peek(&b));           // empty -> false
}

TEST_CASE("serial RX: data survives wrap-around past the ring boundary") {
    Pipe in;
    Botanik::HAL::Serial::Raw raw;
    raw.set_fds(in.r(), -1);
    BufferedSerial bs(&raw);

    // The RX ring is 256 bytes; cycling 100-byte chunks several times forces the
    // head/tail indices to wrap. Each byte carries a running counter so a torn or
    // misordered wrap shows up as a content mismatch.
    uint8_t next = 0;
    for (int round = 0; round < 6; ++round) {
        char chunk[100];
        for (int i = 0; i < 100; ++i) chunk[i] = (char)(uint8_t)(next + i);
        REQUIRE(::write(in.w(), chunk, sizeof(chunk)) == (ssize_t)sizeof(chunk));
        CHECK(bs.receive() == 100);

        char out[100] = {0};
        CHECK(bs.read(out, 100) == 100);
        CHECK(std::memcmp(out, chunk, 100) == 0);
        next = (uint8_t)(next + 100);
    }
    CHECK(bs.rx_empty());
}

TEST_CASE("serial RX: receive() stops at capacity, leaving the rest on the wire") {
    Pipe in;
    Botanik::HAL::Serial::Raw raw;
    raw.set_fds(in.r(), -1);
    BufferedSerial bs(&raw);

    // One slot is reserved to distinguish full from empty, so usable RX capacity
    // is rx_capacity() - 1.
    const uint16_t cap = BufferedSerial::rx_capacity() - 1;
    char big[BufferedSerial::rx_capacity() + 50];
    std::memset(big, 'z', sizeof(big));
    REQUIRE(::write(in.w(), big, sizeof(big)) == (ssize_t)sizeof(big));

    CHECK(bs.receive() == cap);         // fills to capacity, no further
    CHECK(bs.available() == cap);

    char drain[BufferedSerial::rx_capacity()];
    CHECK(bs.read(drain, cap) == cap);  // empty the ring
    CHECK(bs.receive() > 0);            // the held-back bytes now come through
}

// TX ring
// ----------------------------------------------------------------------------

TEST_CASE("serial TX: write() fills the ring and tracks free space") {
    BufferedSerial bs(nullptr);         // write() touches only the ring, not the device
    CHECK(bs.tx_empty());
    CHECK(bs.availableForWrite() == BufferedSerial::tx_capacity() - 1);

    CHECK(bs.write("abc", 3) == 3);
    CHECK_FALSE(bs.tx_empty());
    CHECK(bs.availableForWrite() == BufferedSerial::tx_capacity() - 1 - 3);
}

TEST_CASE("serial TX: a write past capacity is truncated and counted as dropped") {
    BufferedSerial bs(nullptr);
    const uint16_t cap = BufferedSerial::tx_capacity() - 1;   // usable bytes

    char big[BufferedSerial::tx_capacity() + 100];
    std::memset(big, 'x', sizeof(big));
    uint16_t over = (uint16_t)(sizeof(big) - cap);

    CHECK(bs.write(big, sizeof(big)) == cap);   // only the usable bytes are accepted
    CHECK(bs.availableForWrite() == 0);
    CHECK(bs.tx_dropped() == over);             // the overflow is counted
}

TEST_CASE("serial TX: transmit() drains the ring to the device") {
    Pipe out;
    Botanik::HAL::Serial::Raw raw;
    raw.set_fds(-1, out.w());           // a pipe sink (non-tty: no baud throttle)
    BufferedSerial bs(&raw);

    CHECK(bs.write("hello", 5) == 5);
    CHECK(bs.transmit() == 5);
    CHECK(bs.tx_empty());

    char got[8] = {0};
    CHECK(::read(out.r(), got, 5) == 5);
    CHECK(std::memcmp(got, "hello", 5) == 0);
}

TEST_CASE("serial TX: transmit() is bounded per activation, and resumes") {
    // The drain used to run until the ring emptied or the device refused. A
    // UART bounds itself through availableForWrite(); USB CDC with no host
    // answers 1 forever, so one activation pushed the whole 4095-byte ring.
    Pipe out;
    Botanik::HAL::Serial::Raw raw;
    raw.set_fds(-1, out.w());
    BufferedSerial bs(&raw);

    const uint16_t cap = Botanik::Config::SERIAL_TX_PER_STEP;
    REQUIRE(cap > 0);                       // 0 would mean the cap is disabled

    // Queue half a beat's worth more than the cap allows.
    const uint16_t n = (uint16_t)(cap + 64);
    char blob[BufferedSerial::tx_capacity()];
    std::memset(blob, 'x', sizeof(blob));
    REQUIRE(bs.write(blob, n) == n);

    CHECK(bs.transmit() == cap);            // capped, not drained
    CHECK_FALSE(bs.tx_empty());             // the rest is still owed

    CHECK(bs.transmit() == 64);             // and goes out next activation
    CHECK(bs.tx_empty());                   // nothing lost
}

}  // namespace serial

// --------------------------------------------------------------------------
// hal_memory
// --------------------------------------------------------------------------

namespace hal_memory {

using namespace Botanik;

namespace {

    // Word-multiple, as both backends' static_assert demands.
    struct Block {
        uint32_t seq;
        uint32_t a;
        uint32_t b;
        uint32_t c;
    };

    struct Empty {};

}  // namespace

}  // namespace hal_memory
