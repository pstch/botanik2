/**
 * @file test_parser.cpp
 * @brief Tests for InputParser in terminal/parser.cpp — the byte-stream decode
 *        state machine. Drives the parser through its public parse() by feeding
 *        raw bytes into a real BufferedSerial backed by a pipe, then asserts the
 *        decoded InputEvent (printable batches, C0 controls, CSI/Alt/SEP
 *        sequences, chunkable coalescing, and the ESC timeout).
 */
#include <cstdint>
#include <cstring>
#include <unistd.h>

#include "doctest.h"

#include "libbotanik/terminal/parser.h"

using namespace Botanik;
using Botanik::Terminal::InputParser;
using Botanik::Terminal::InputEvent;
using Command = InputEvent::Command;
using EventType = InputEvent::EventType;

namespace {

    // A parser wired to a pipe: bytes written into feed() land in the RX ring and
    // are decoded by parse(). `now` is the parser's shared loop clock (ms).
    struct ParserFixture {
        int                       pipe_fds[2];
        Botanik::HAL::RawSerial   raw;
        Botanik::Utils::BufferedSerial serial;
        uint32_t                  now = 0;
        InputParser               parser;

        ParserFixture() : serial(&raw), parser(serial, &now) {
            REQUIRE(::pipe(pipe_fds) == 0);
            raw.set_fds(pipe_fds[0], -1);     // RX reads the pipe's read end
        }
        ~ParserFixture() { ::close(pipe_fds[0]); ::close(pipe_fds[1]); }

        // Push raw bytes through the wire into the RX ring.
        void feed(const char* data, size_t n) {
            REQUIRE(::write(pipe_fds[1], data, n) == (ssize_t)n);
            serial.receive();
        }
        void feed(const char* s) { feed(s, std::strlen(s)); }

        InputEvent& parse() { return parser.parse(); }
    };

}

// Printable text
// ----------------------------------------------------------------------------

TEST_CASE("parser: printable bytes accumulate into one CONTENT batch") {
    ParserFixture f;
    f.feed("hello");
    InputEvent& e = f.parse();
    CHECK(e.type == EventType::CONTENT);
    CHECK(e.batch_len == 5);
    CHECK(std::memcmp(e.batch_data, "hello", 5) == 0);
}

TEST_CASE("parser: a full batch flushes at ANSI_INPUT_BATCH_SIZE") {
    ParserFixture f;
    char buf[Config::ANSI_INPUT_BATCH_SIZE];
    std::memset(buf, 'a', sizeof(buf));
    f.feed(buf, sizeof(buf));
    InputEvent& e = f.parse();
    CHECK(e.type == EventType::CONTENT);
    CHECK(e.batch_len == Config::ANSI_INPUT_BATCH_SIZE);
}

// C0 control bytes
// ----------------------------------------------------------------------------

TEST_CASE("parser: control bytes decode to commands") {
    ParserFixture f;
    f.feed("\x0D");                         // CR
    CHECK(f.parse().command == Command::ENTER);
    f.feed("\x0A");                         // LF
    CHECK(f.parse().command == Command::ENTER);
    f.feed("\x09");                         // TAB
    CHECK(f.parse().command == Command::TAB);
    f.feed("\x03");                         // Ctrl+C
    CHECK(f.parse().command == Command::CTRL_C);
}

// CSI sequences
// ----------------------------------------------------------------------------

TEST_CASE("parser: CSI arrow keys") {
    ParserFixture f;
    f.feed("\x1B[A"); CHECK(f.parse().command == Command::UP);
    f.feed("\x1B[B"); CHECK(f.parse().command == Command::DOWN);
    f.feed("\x1B[C"); CHECK(f.parse().command == Command::RIGHT);
    f.feed("\x1B[D"); CHECK(f.parse().command == Command::LEFT);
    f.feed("\x1B[H"); CHECK(f.parse().command == Command::HOME);
    f.feed("\x1B[F"); CHECK(f.parse().command == Command::END);
}

TEST_CASE("parser: CSI tilde keys, with and without a modifier param") {
    ParserFixture f;
    f.feed("\x1B[3~");   CHECK(f.parse().command == Command::DELETE);
    f.feed("\x1B[5~");   CHECK(f.parse().command == Command::PAGE_UP);
    f.feed("\x1B[6~");   CHECK(f.parse().command == Command::PAGE_DOWN);
    // xterm modifier: Ctrl is bit 4 of (param2 - 1), so 5;5~ is Ctrl+PageUp.
    f.feed("\x1B[5;5~"); CHECK(f.parse().command == Command::CTRL_PAGE_UP);
    f.feed("\x1B[6;5~"); CHECK(f.parse().command == Command::CTRL_PAGE_DOWN);
}

TEST_CASE("parser: a long digit run saturates instead of wrapping") {
    ParserFixture f;
    // Param overflows uint16_t; it must clamp, not roll over into a small value
    // that spuriously matches a tilde key. 99999... is no valid key -> NONE.
    f.feed("\x1B[999999~");
    CHECK(f.parse().command == Command::NONE);
}

// Alt (ESC-prefixed) sequences
// ----------------------------------------------------------------------------

TEST_CASE("parser: Alt+letter and Alt+digit") {
    ParserFixture f;
    f.feed("\x1B" "b"); CHECK(f.parse().command == Command::ALT_B);
    f.feed("\x1B" "f"); CHECK(f.parse().command == Command::ALT_F);
    f.feed("\x1B" "3"); CHECK(f.parse().command == Command::ALT_3);
}

TEST_CASE("parser: a lone ESC resolves to ESC after the command timeout") {
    ParserFixture f;
    f.feed("\x1B");
    CHECK(f.parse().type == EventType::NONE);   // still waiting for the rest
    f.now += Config::ANSI_CMD_TIMEOUT_MS;       // nothing more arrives
    CHECK(f.parse().command == Command::ESC);
}

// Chunkable coalescing (held BACKSPACE / CTRL_D)
// ----------------------------------------------------------------------------

TEST_CASE("parser: a single backspace emits BACKSPACE with count 1") {
    ParserFixture f;
    f.feed("\x7F");
    InputEvent& e = f.parse();
    CHECK(e.command == Command::BACKSPACE);
    CHECK(e.count == 1);
}

TEST_CASE("parser: repeated backspaces coalesce into one count-N command") {
    ParserFixture f;
    f.feed("\x7F\x7F\x7F");
    InputEvent& e = f.parse();
    CHECK(e.command == Command::BACKSPACE);
    CHECK(e.count == 3);
}

TEST_CASE("parser: Ctrl+D is its own chunkable command") {
    ParserFixture f;
    f.feed("\x04\x04");
    InputEvent& e = f.parse();
    CHECK(e.command == Command::CTRL_D);
    CHECK(e.count == 2);
}
