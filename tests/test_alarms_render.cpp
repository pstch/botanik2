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
#include "libbotanik/terminal/terminal.h"

using namespace Botanik;
using Command = Terminal::InputEvent::Command;

namespace {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    // Four always-recent conditions (recent_s < 0 never expires), logging/notify off.
    const Alarms::AlarmCondition TABLE[] = {
        { "AAA", "alarm A", 0, -1, -1, -1, -1, -1, 0 },
        { "BBB", "alarm B", 0, -1, -1, -1, -1, -1, 0 },
        { "CCC", "alarm C", 0, -1, -1, -1, -1, -1, 0 },
        { "DDD", "alarm D", 0, -1, -1, -1, -1, -1, 0 },
    };
#pragma GCC diagnostic pop

    // AnsiTerminal + AlarmModel over a pipe; drains TX into a captured string.
    struct RenderFixture {
        int                    fds[2];
        HAL::RawSerial         raw;
        Utils::BufferedSerial  serial;
        uint32_t               now = 0;
        Terminal::AnsiTerminal terminal;
        Alarms::AlarmModel     model;
        Pages::AlarmsPage      page;

        RenderFixture()
            : serial(&raw),
              // Very high baud so the cooperative TX floor never throttles a paint.
              terminal(serial, 80, 24, &now, 4000000u),
              model(TABLE, 4, nullptr),
              page(terminal, &model) {
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
