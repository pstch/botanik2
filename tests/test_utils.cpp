/**
 * @file test_utils.cpp
 * @brief Utility layer: text formatting, uptime, buffered serial, memory barriers.
 *
 * Sections, each in its own namespace to avoid fixture collisions.
 */

#include <cstdlib>
#include <cstring>
#include <limits>
#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/text.h"
#include "doctest.h"
#include <cstdint>
#include "libbotanik/utils/time.h"
#include <unistd.h>
#include "libbotanik_config.h"
#include "libbotanik/utils/serial.h"

// text

namespace text {

using Botanik::Utils::format_float;
using Botanik::Utils::format_int;
using Botanik::Utils::format_age;
using Botanik::Utils::format_size;
using Botanik::Utils::contains_ci;
static const auto env_u8  = Botanik::HAL::Env::u8;   // HAL::Env: reading env is a platform call
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
    const char* r = format_float(b, sizeof(b), 1.0, 12);   // 12 requested clamps to 9
    CHECK(std::strlen(r) == std::strlen("1.000000000")); // "1." + 9 zeros
}

TEST_CASE("format_float: output is truncated, not overrun, when cap is tight") {
    char b[8];
    // "3.14" needs 5 bytes incl. NUL; cap 4 keeps 3 chars + NUL, dropping the tail.
    CHECK(std::strcmp(format_float(b, 4, 3.14159, 2), "3.1") == 0);
    CHECK(std::strcmp(format_float(b, 1, 3.14159, 2), "") == 0);   // cap 1: room only for NUL
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

TEST_CASE("contains_ci: case-folded substring, with the empty and null edges") {
    CHECK(contains_ci("cli_temp_setpoint", "temp"));
    CHECK(contains_ci("cli_temp_setpoint", "TEMP"));
    CHECK(contains_ci("CLI_TEMP_SETPOINT", "temp"));
    CHECK(contains_ci("cli_temp_setpoint", "cli_temp_setpoint"));
    CHECK(contains_ci("cli_temp_setpoint", "t"));
    CHECK_FALSE(contains_ci("cli_temp_setpoint", "tempo"));
    CHECK_FALSE(contains_ci("cli_temp", "cli_temp_setpoint"));   // needle outruns the tail
    CHECK_FALSE(contains_ci("", "a"));
    // An empty needle is the unfiltered table: it keeps every row.
    CHECK(contains_ci("anything", ""));
    CHECK(contains_ci("", ""));
    CHECK(contains_ci(nullptr, ""));
    CHECK_FALSE(contains_ci(nullptr, "a"));
}

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

// env_u32 / env_u8: baud rates don't fit a byte, so env_u8 is env_u32's range-checked narrowing.
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

    unsetenv("BOTANIK_TEST_U8");   // unset, empty, out of range, non-numeric all fall back
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

// time

namespace uptime {

using namespace Botanik::Utils;

// Independent reference (Howard Hinnant's algorithm): cross-checks time.cpp's decomposition instead of mirroring it.
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t  era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

TEST_CASE("cal_time_ms: civil fields decode the reported unixSeconds") {
    // Sweep a wide span of uptimes; each decomposition must be self-consistent.
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

        CHECK(ct.unixDays == (uint32_t)(ct.unixSeconds / 86400));   // day index of unixSeconds
        CHECK(days_from_civil(ct.year, ct.month, ct.day) == (int64_t)ct.unixDays);  // re-derives it independently
        CHECK((uint64_t)ct.hour * 3600 + (uint64_t)ct.minute * 60 + ct.second
              == ct.unixSeconds % 86400);   // clock fields account for the day's seconds
    }
}

}  // namespace uptime

// serial

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

    // 256-byte ring; cycling 100-byte chunks wraps head/tail. Each byte is a running counter, so a torn wrap shows as a mismatch.
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

    const uint16_t cap = BufferedSerial::rx_capacity() - 1;   // one slot reserved: full vs. empty
    char big[BufferedSerial::rx_capacity() + 50];
    std::memset(big, 'z', sizeof(big));
    REQUIRE(::write(in.w(), big, sizeof(big)) == (ssize_t)sizeof(big));

    CHECK(bs.receive() == cap);         // fills to capacity, no further
    CHECK(bs.available() == cap);

    char drain[BufferedSerial::rx_capacity()];
    CHECK(bs.read(drain, cap) == cap);  // empty the ring
    CHECK(bs.receive() > 0);            // the held-back bytes now come through
}

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

// USB CDC with no host answers availableForWrite() as 1 forever, so the drain must be bounded
// per activation or it pushes the whole 4095-byte ring at once.
TEST_CASE("serial TX: transmit() is bounded per activation, and resumes") {
    Pipe out;
    Botanik::HAL::Serial::Raw raw;
    raw.set_fds(-1, out.w());
    BufferedSerial bs(&raw);

    const uint16_t cap = Botanik::Config::SERIAL_TX_PER_STEP;
    REQUIRE(cap > 0);                       // 0 would mean the cap is disabled

    const uint16_t n = (uint16_t)(cap + 64);   // queue half a beat's worth past the cap
    char blob[BufferedSerial::tx_capacity()];
    std::memset(blob, 'x', sizeof(blob));
    REQUIRE(bs.write(blob, n) == n);

    CHECK(bs.transmit() == cap);            // capped, not drained
    CHECK_FALSE(bs.tx_empty());             // the rest is still owed

    CHECK(bs.transmit() == 64);             // and goes out next activation
    CHECK(bs.tx_empty());                   // nothing lost
}

}  // namespace serial

// hal_memory

namespace hal_memory {

using namespace Botanik;

namespace {

    struct Block {   // word-multiple, as both backends' static_assert demands
        uint32_t seq;
        uint32_t a;
        uint32_t b;
        uint32_t c;
    };

    struct Empty {};

}  // namespace

}  // namespace hal_memory

namespace pulse {

using namespace Botanik;

TEST_CASE("pulse: a channel counts what it is given, with the instant of the read") {
    REQUIRE(HAL::Pulse::attach(5, HAL::Pulse::EDGE_RISING));
    const HAL::Pulse::Sample a = HAL::Pulse::sample(5);
    HAL::Pulse::inject(5, 450);
    const HAL::Pulse::Sample b = HAL::Pulse::sample(5);

    CHECK(b.count - a.count == 450);
    CHECK(b.at_ms >= a.at_ms);
    INFO("an unattached channel reads zero rather than another channel's count");
    CHECK(HAL::Pulse::sample(6).count == 0);
}

TEST_CASE("pulse: I1 and I4 cannot both count") {
    // They share an interrupt line on the OPTA's part. Enforced on the host too,
    // or a site that only ever runs in the simulator meets it on the bench.
    REQUIRE(HAL::Pulse::attach(0, HAL::Pulse::EDGE_RISING));
    CHECK_FALSE(HAL::Pulse::attach(3, HAL::Pulse::EDGE_RISING));
    INFO("and any other channel is still free");
    CHECK(HAL::Pulse::attach(4, HAL::Pulse::EDGE_RISING));
}

TEST_CASE("pulse: attaching twice is refused rather than silently re-armed") {
    REQUIRE(HAL::Pulse::attach(7, HAL::Pulse::EDGE_FALLING));
    CHECK_FALSE(HAL::Pulse::attach(7, HAL::Pulse::EDGE_RISING));
    CHECK_FALSE(HAL::Pulse::attach(8, HAL::Pulse::EDGE_RISING));   // no such input
}

}  // namespace pulse
