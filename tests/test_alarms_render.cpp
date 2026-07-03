/**
 * @file test_alarms_render.cpp
 * @brief AlarmsPage paint diffing: bytes emitted on repaint. A pipe-backed
 *        AnsiTerminal lets us drain TX and assert exactly what a repaint wrote -
 *        the diff's contract is "emit only what changed", so the byte stream is
 *        the oracle. High baud disables the throttle so a paint finishes in a
 *        few ticks; we pump tick()+transmit() until the pipe goes quiet.
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
        // Pump ticks until a paint settles, capturing all bytes it wrote.
        std::string paint() {
            std::string out;
            for (int i = 0; i < 64; i++) { page.tick(true); out += capture(); }
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

// Acking an active alarm flips only its ON badge (active+unack -> active+ack):
// same alarm identity, so the repaint must rewrite just the ON cell - far less
// than a full row, and it must NOT re-emit the CODE/DESC text. A single active
// alarm is used so the ack does not reorder the list (acking one of several
// active alarms demotes it below the still-nagging ones, which would reshuffle
// every row's identity and force full redraws - that is the identity-change case
// below, not a same-row cell patch).
TEST_CASE("alarms render: same-alarm state change patches only the cell") {
    RenderFixture f;
    f.model.step(0x1, 1000);                     // only AAA active -> sole row, no peers
    f.page.focus();
    f.paint();                                   // initial full paint; discard

    CHECK(f.model.ack("AAA") == true);           // active+unack -> active+ack (stays put)
    f.page.handle_command(Command::ENTER, 1);    // repaint, shadow intact
    std::string out = f.paint();

    CHECK(out.size() > 0);                       // the ON cell was rewritten
    CHECK(out.find("alarm A") == std::string::npos);  // DESCRIPTION not re-emitted
    CHECK(out.find("AAA")     == std::string::npos);  // CODE not re-emitted
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

// A one-line scroll is incremental: the data band shifts via scroll_region (an
// IL/DL pair) and only the newly exposed row is drawn. The shadow shift in
// _drain_scroll mirrors that move, so the rows that merely slid are NOT redrawn.
// A short window (10 rows -> data window of 2) makes the 4-alarm set overflow so
// DOWN actually scrolls. With all four raised at once they keep table order
// (AAA, BBB, CCC, DDD); top=0 shows AAA,BBB and DOWN to top=1 exposes CCC.
TEST_CASE("alarms render: a one-line scroll redraws only the exposed row") {
    RenderFixture f(10);
    f.model.step(0xF, 1000);
    f.page.focus();
    std::string full = f.paint();
    REQUIRE(full.find("AAA") != std::string::npos);   // top of the window

    f.page.handle_command(Command::DOWN, 1);          // scroll one line
    std::string scrolled = f.paint();                 // drain the scroll
    CHECK(scrolled.size() < full.size());             // incremental, not a full repaint
    CHECK(scrolled.find("CCC") != std::string::npos); // newly exposed row drawn
    CHECK(scrolled.find("AAA") == std::string::npos); // scrolled off, not redrawn
    CHECK(scrolled.find("BBB") == std::string::npos); // slid by scroll_region, not redrawn
}
