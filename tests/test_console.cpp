/**
 * @file test_console.cpp
 * @brief The input parser, fed the way a serial line actually feeds it.
 */

#include <unistd.h>

#include "doctest.h"

#include "libbotanik.h"
#include "term_fixture.h"

namespace console_input {

using namespace Botanik;
using Terminal::InputParser;
using Terminal::InputEvent;
using Command = InputEvent::Command;

namespace {

/** @brief A parser over a loopback pipe, so a case can hand it bytes in
 *  whatever chunks it likes - which is the whole point: parse() returns as
 *  soon as the ring empties, and a key that spans two of those returns is the
 *  case this file exists for. */
struct Feed {
    botanik_test::PipeSerial pipe;
    uint64_t                 now = 0;
    InputParser              parser{pipe.serial, &now};

    /** @brief Put @p bytes on the wire and run ONE parse, as a beat does. */
    Command step(const char* bytes, size_t n) {
        if (n > 0) REQUIRE(::write(pipe.fds[1], bytes, n) == (ssize_t)n);
        pipe.serial.receive();
        now += 10;                       // one beat of the console task
        const InputEvent& e = parser.parse();
        return (e.type == InputEvent::EventType::COMMAND) ? e.command
                                                          : Command::NONE;
    }
    Command step(const char* s) { return step(s, __builtin_strlen(s)); }
};

}  // namespace

TEST_CASE("input: a CSI sequence arriving whole is decoded") {
    Feed f;
    CHECK(f.step("\x1b[3~") == Command::DELETE);
}

TEST_CASE("input: a CSI sequence split across polls is still decoded") {
    // The bug this file was written for. parse() returns the moment the ring
    // empties, so a key whose bytes straddle a poll boundary is delivered in
    // pieces - about one press in ten at 38400 baud, and most of them at 4800.
    // The parameter has to survive the gap, which it did not while it lived on
    // the event parse() clears at the top of every call.
    Feed f;
    CHECK(f.step("\x1b[3") == Command::NONE);   // nothing to publish yet
    CHECK(f.step("~")      == Command::DELETE);
}

TEST_CASE("input: a split PARAMETERISED sequence keeps both parameters") {
    // ESC[1;2A is SHIFT_UP. Losing the parameters degrades it to a bare UP -
    // history recall where cursor motion was meant, which is worse than the
    // key being dropped.
    Feed f;
    CHECK(f.step("\x1b[1;") == Command::NONE);
    CHECK(f.step("2A")      == Command::SHIFT_UP);
}

TEST_CASE("input: one byte per poll is still one key") {
    // The worst case the wire can produce, and what a slow line does routinely.
    Feed f;
    CHECK(f.step("\x1b") == Command::NONE);
    CHECK(f.step("[")    == Command::NONE);
    CHECK(f.step("1")    == Command::NONE);
    CHECK(f.step(";")    == Command::NONE);
    CHECK(f.step("2")    == Command::NONE);
    CHECK(f.step("A")    == Command::SHIFT_UP);
}

TEST_CASE("input: a new sequence does not inherit the last one's parameters") {
    // The accumulator is cleared on entering CSI, not on leaving it: a bare
    // ESC[A after ESC[3~ must be UP and not something the 3 selected.
    Feed f;
    CHECK(f.step("\x1b[3~") == Command::DELETE);
    CHECK(f.step("\x1b[A")  == Command::UP);
    // ...and the same across a split.
    CHECK(f.step("\x1b[5") == Command::NONE);
    CHECK(f.step("~")      == Command::PAGE_UP);
    CHECK(f.step("\x1b[")  == Command::NONE);
    CHECK(f.step("B")      == Command::DOWN);
}

}  // namespace console_input

