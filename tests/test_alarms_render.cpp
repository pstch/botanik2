/**
 * @file test_alarms_render.cpp
 * @brief AlarmsPage paint diffing: bytes emitted on repaint. A pipe-backed
 *        AnsiTerminal lets us drain TX and assert exactly what a repaint wrote -
 *        the diff's contract is "emit only what changed", so the byte stream is
 *        the oracle. High baud disables the throttle so a paint finishes in a
 *        few ticks; each pump submits (tick), drains the transaction queue, and
 *        transmits until the pipe goes quiet.
 */
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "doctest.h"

#include "libbotanik/alarms/alarms.h"
#include "libbotanik/pages/alarms.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/console/console.h"

using namespace Botanik;
using Command = Terminal::InputEvent::Command;

namespace {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    // Four always-recent conditions (recent_s < 0 never expires), logging/notify off.
    const Alarms::AlarmCondition TABLE[] = {
        { "AAA", "alarm A", 0, -1, -1, -1, 0 },
        { "BBB", "alarm B", 0, -1, -1, -1, 0 },
        { "CCC", "alarm C", 0, -1, -1, -1, 0 },
        { "DDD", "alarm D", 0, -1, -1, -1, 0 },
    };
#pragma GCC diagnostic pop

    // AnsiTerminal + AlarmModel over a pipe; drains TX into a captured string.
    struct RenderFixture {
        int                    fds[2];
        HAL::Serial::Raw         raw;
        Utils::BufferedSerial  serial;
        uint32_t               now = 0;
        Terminal::Console      console;
        Alarms::AlarmModel     model;
        Pages::AlarmsPage      page;

        // @p rows lets a test pick a short screen so the 4-alarm set overflows the
        // data window and UP/DOWN actually scroll (the default 24 rows holds them all).
        explicit RenderFixture(uint16_t rows = 24)
            : serial(&raw),
              // Very high baud so the cooperative TX floor never throttles a paint.
              console(serial, 80, rows, &now, 4000000u),
              model(TABLE, 4, nullptr),
              page(console, &model) {
            REQUIRE(::pipe(fds) == 0);
            ::fcntl(fds[0], F_SETFL, O_NONBLOCK);   // read end never blocks
            raw.set_fds(fds[0], fds[1]);
        }
        ~RenderFixture() { ::close(fds[0]); ::close(fds[1]); }

        // Drain TX to the pipe and return whatever has accumulated since last call.
        std::string capture() {
            std::string out;
            char buf[8192];
            for (int i = 0; i < 64; i++) serial.transmit();
            ssize_t n;
            while ((n = ::read(fds[0], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
            return out;
        }
        // Pump ticks until a paint settles, capturing all bytes it wrote. Each
        // tick submits transactions; tx_drain() feeds the ring, capture() the pipe.
        std::string paint() {
            std::string out;
            for (int i = 0; i < 64; i++) {
                page.tick(true);
                console.terminal().tx_drain();
                out += capture();
            }
            return out;
        }
    };
}

// With no visible alarms (RECENT mode, nothing fired), a focus paints the
// structural rows; an immediate repaint with nothing changed must skip them all.
// Only the always-redrawn footer (one line) is re-emitted, so the repaint is a
// fraction of the first paint and carries no header or rule bytes.
TEST_CASE("alarms render: unchanged repaint skips the structural rows") {
    RenderFixture f;
    f.page.focus();
    std::string first = f.paint();
    CHECK(first.size() > 0);                  // structural rows drawn once
    CHECK(first.find("CODE") != std::string::npos);   // header was in the first paint

    f.page.handle_command(Command::ENTER, 1); // request repaint, shadow intact
    std::string second = f.paint();
    CHECK(second.size() < first.size() / 2);  // only the footer, not the table
    CHECK(second.find("CODE") == std::string::npos);  // header skipped
    CHECK(second.find("----") == std::string::npos);  // rules skipped
}

// Raise all four alarms so the table has data rows, then drive changes.
static void raise_all(RenderFixture& f) {
    f.model.step(0xF, 1000);     // bits 0..3 active
    f.page.focus();
    f.paint();                   // initial full paint; discard
}

// Acking an active alarm flips its ON badge (active+unack -> active+ack): a
// same-identity change, so only its mutable cells (ON badge + LAST EVENT) repaint
// on the value lane. The CODE cell (and the static DESCRIPTION) stay untouched, so
// "BBB" does NOT reach the wire again - a whole-row redraw would be wasted bytes.
// The still-unacked AAA row above it is diff-skipped, as are the header and rules.
// A single reorder-free ack is used (BBB stays below AAA) so one row's shadow moves.
TEST_CASE("alarms render: a same-alarm state change patches its cells, skips the rest") {
    RenderFixture f;
    f.model.step(0x3, 1000);                     // AAA + BBB active; row0=AAA (sel), row1=BBB
    f.page.focus();
    f.paint();                                   // initial full paint; discard

    CHECK(f.model.ack("BBB") == true);           // BBB (row1) active+unack -> active+ack
    f.page.handle_command(Command::ENTER, 1);    // repaint, shadow intact, selection at row0
    std::string out = f.paint();

    CHECK(out.size()          > 0);                   // the ON/EVENT cell patch was emitted
    CHECK(out.find("BBB")     == std::string::npos);  // CODE cell untouched (cells-only patch)
    CHECK(out.find("AAA")     == std::string::npos);  // the unchanged AAA row is skipped
    CHECK(out.find("CODE")    == std::string::npos);  // header/rules not re-emitted
    CHECK(out.find("----")    == std::string::npos);
}

// A repaint with nothing changed emits nothing (all data rows skipped too).
TEST_CASE("alarms render: unchanged data repaint emits nothing") {
    RenderFixture f;
    raise_all(f);
    f.page.handle_command(Command::ENTER, 1);
    std::string out = f.paint();
    CHECK(out.find("CCC") == std::string::npos);   // no data row re-emitted
    CHECK(out.find("----") == std::string::npos);  // no rule re-emitted
}

// Raising a new alarm reorders the list (new row identity), so at least one row
// is redrawn in full - its CODE/DESC reach the wire.
TEST_CASE("alarms render: identity change redraws the row in full") {
    RenderFixture f;
    f.model.step(0x1, 1000);             // only AAA
    f.page.focus();
    f.paint();

    f.model.step(0x3, 2000);             // BBB now active too -> sorts in, reorders
    f.page.handle_command(Command::ENTER, 1);
    std::string out = f.paint();
    CHECK(out.find("BBB") != std::string::npos);   // a full row carried the new CODE
}

// DOWN moves the selection cursor, not the window. A short window (10 rows ->
// data window of 2) overflows the 4-alarm set; with all four raised at once they
// keep table order (AAA, BBB, CCC, DDD). top=0 shows AAA,BBB; moving the cursor
// down past the window bottom auto-scrolls so CCC comes into view.
TEST_CASE("alarms render: DOWN moves the selection and scrolls it into view") {
    RenderFixture f(10);
    f.model.step(0xF, 1000);
    f.page.focus();
    std::string full = f.paint();
    REQUIRE(full.find("AAA") != std::string::npos);   // window top
    REQUIRE(full.find("CCC") == std::string::npos);   // below the window

    f.page.handle_command(Command::DOWN, 1);          // cursor -> row 1 (BBB, still shown)
    f.paint();
    f.page.handle_command(Command::DOWN, 1);          // cursor -> row 2 (CCC): scroll to reveal
    std::string out = f.paint();
    CHECK(out.find("CCC") != std::string::npos);      // the cursor followed down into view
}
