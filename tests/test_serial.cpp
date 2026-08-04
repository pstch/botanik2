/**
 * @file test_serial.cpp
 * @brief Tests for the RX/TX ring buffers in utils/serial.h (BufferedSerial).
 *        RX is fed through a real pipe + receive(); TX is exercised via write()
 *        / transmit() / tx_discard(). Covers ordering, peek, wrap-around past
 *        the ring boundary, the full-ring capacity cap, and TX drop counting.
 */
#include <cstdint>
#include <cstring>
#include <unistd.h>

#include "doctest.h"

#include "libbotanik_config.h"
#include "libbotanik/utils/serial.h"

using Botanik::Utils::BufferedSerial;

namespace {
    struct Pipe {
        int fds[2];
        Pipe()  { REQUIRE(::pipe(fds) == 0); }
        ~Pipe() { ::close(fds[0]); ::close(fds[1]); }
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

TEST_CASE("serial TX: tx_discard() empties the ring") {
    BufferedSerial bs(nullptr);
    bs.write("payload", 7);
    CHECK_FALSE(bs.tx_empty());
    bs.tx_discard();
    CHECK(bs.tx_empty());
    CHECK(bs.availableForWrite() == BufferedSerial::tx_capacity() - 1);
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
