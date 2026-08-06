/**
 * @file test_alarms_render.cpp
 * @brief AlarmsPage paint diffing: bytes emitted on repaint. The page draws
 *        immediate-mode into the terminal's FRONT grid and present() emits the
 *        minimal diff, so "emit only what changed" is the contract under test. A
 *        pipe-backed AnsiTerminal lets us drain TX and assert exactly what a
 *        repaint wrote. High baud disables the throttle so a paint finishes in a
 *        few ticks; each pump ticks the page, presents, and transmits until the
 *        pipe goes quiet.
 */
#include <cstdint>                          // uint8_t/uint16_t/uint64_t
#include <fcntl.h>                          // fcntl / O_NONBLOCK
#include <string>                           // std::string
#include <unistd.h>                         // pipe / read / close

#include "libbotanik/alarms/alarms.h"       // Alarms::AlarmController
#include "libbotanik/console/console.h"     // Console
#include "libbotanik/console/terminal.h"    // AnsiTerminal::present
#include "libbotanik/pages/alarms.h"        // Pages::AlarmsPage
#include "libbotanik/vars/declare.h"        // the A() factory

#include "doctest.h"

using namespace Botanik;
using Command = Terminal::InputEvent::Command;

namespace {
    // Drive the model the way the firmware does: write each condition's own
    // variable (bit i -> condition i), then step. There is no other way in now -
    // an alarm IS its row.
    void step_bits(Vars::Bank& io, Alarms::AlarmController& m, uint64_t bits, uint64_t now) {
        for (uint8_t i = 0; i < m.count(); i++)
            io.var(m.condition(i).ref).set(((bits >> i) & 1u) != 0);
        m.step(now);
    }

    // Four always-recent alarms (recent_s < 0 never expires), logging/notify
    // off. Declared as SCOPE_A rows, the way a site declares one; the model
    // finds them by scanning the bank.
    const Vars::Variable TABLE[] = {
        Vars::A("aaa", "AAA", -1, -1, -1, 0, 0, "alarm A", ""),
        Vars::A("bbb", "BBB", -1, -1, -1, 0, 0, "alarm B", ""),
        Vars::A("ccc", "CCC", -1, -1, -1, 0, 0, "alarm C", ""),
        Vars::A("ddd", "DDD", -1, -1, -1, 0, 0, "alarm D", ""),
    };

    /** @brief Bind a row table to a value store, as a site's descriptor does. */
    inline Vars::VarTables make_tables(const Vars::Variable* v, Vars::Value* val, uint16_t n) {
        Vars::VarTables t = {};
        t.var = v; t.val = val; t.count = n;
        return t;
    }

    // AnsiTerminal + AlarmController over a pipe; drains TX into a captured string.
    struct RenderFixture {
        int                    fds[2];
        HAL::Serial::Raw         raw;
        Utils::BufferedSerial  serial;
        uint64_t               now = 0;
        Terminal::Console      console;
        Vars::Value            vals[4]{};
        Vars::Bank             bank;
        Alarms::AlarmController     model;
        Pages::AlarmsPage      page;

        // @p rows lets a test pick a short screen so the 4-alarm set overflows the
        // data window and UP/DOWN actually scroll (the default 24 rows holds them all).
        explicit RenderFixture(uint16_t rows = 24)
            : serial(&raw),
              // Very high baud so the cooperative TX floor never throttles a paint.
              console(serial, 80, rows, &now, 4000000u),
              bank(make_tables(TABLE, vals, 4)),
              model(&bank, nullptr),
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
        // tick draws into the FRONT grid; present() diffs it to the wire; capture()
        // reads the emitted bytes off the pipe.
        std::string paint() {
            std::string out;
            for (int i = 0; i < 64; i++) {
                page.tick(true);
                console.terminal().present();
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
    CHECK(first.find("VARIABLE") != std::string::npos);   // header was in the first paint

    f.page.handle_command(Command::ENTER, 1); // request repaint
    std::string second = f.paint();
    CHECK(second.size() < first.size() / 2);  // only the footer, not the table
    CHECK(second.find("VARIABLE") == std::string::npos);  // header skipped
    CHECK(second.find("----") == std::string::npos);  // rules skipped
}

// Raise all four alarms so the table has data rows, then drive changes.
static void raise_all(RenderFixture& f) {
    step_bits(f.bank, f.model, 0xF, 1000);     // bits 0..3 active
    f.page.focus();
    f.paint();                   // initial full paint; discard
}

// Acking an active alarm flips its ON badge (active+unack -> active+ack). The
// page redraws the whole table, but only the cells that actually moved reach the
// wire: the badge and the timestamp on that one row. The static DESCRIPTION
// column is byte-identical, so "alarm B" never reaches the wire again, and the
// untouched AAA row, header and rules are all diffed away.
TEST_CASE("alarms render: a same-alarm state change patches its cells, skips the rest") {
    RenderFixture f;
    step_bits(f.bank, f.model, 0x3, 1000);                     // AAA + BBB active; row0=AAA (sel), row1=BBB
    f.page.focus();
    f.paint();                                   // initial full paint; discard

    CHECK(f.model.ack("bbb") == true);           // BBB (row1) active+unack -> active+ack
    f.page.handle_command(Command::ENTER, 1);    // repaint, selection still at row0
    std::string out = f.paint();

    CHECK(out.size()          > 0);                   // the changed cells were emitted
    CHECK(out.find("alarm B") == std::string::npos);  // static DESCRIPTION diffed away
    CHECK(out.find("AAA")     == std::string::npos);  // the unchanged AAA row is skipped
    CHECK(out.find("alarm A") == std::string::npos);  // ...its description too
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

// Raising a new alarm reorders the list, so a row's whole content changes and
// the diff has no choice but to send it - CODE and DESCRIPTION both.
TEST_CASE("alarms render: identity change redraws the row in full") {
    RenderFixture f;
    step_bits(f.bank, f.model, 0x1, 1000);             // only AAA
    f.page.focus();
    f.paint();

    step_bits(f.bank, f.model, 0x3, 2000);             // BBB now active too -> sorts in, reorders
    f.page.handle_command(Command::ENTER, 1);
    std::string out = f.paint();
    CHECK(out.find("BBB") != std::string::npos);   // a full row carried the new CODE
}

// Moving the selection within the window (no scroll) changes only the old row's
// CODE cell (un-highlighted) and the new row's CODE cell (highlighted), so the
// diff emits just those two runs. The descriptions and the other rows never reach
// the wire, proving the highlight costs cells, not full-row redraws.
TEST_CASE("alarms render: a selection move repaints both CODE cells, nothing else") {
    RenderFixture f;                             // 24 rows: all four fit, so no scroll
    step_bits(f.bank, f.model, 0xF, 1000);                     // AAA..DDD active; row0=AAA selected
    f.page.focus();
    f.paint();                                   // initial paint; discard

    f.page.handle_command(Command::DOWN, 1);     // AAA(row0) -> BBB(row1), stays in window
    std::string out = f.paint();

    CHECK(out.find("AAA")     != std::string::npos);  // old row's CODE repainted (plain)
    CHECK(out.find("BBB")     != std::string::npos);  // new row's CODE repainted (inverted)
    CHECK(out.find("alarm A") == std::string::npos);  // DESCRIPTION untouched (cell patch)
    CHECK(out.find("alarm B") == std::string::npos);
    CHECK(out.find("CCC")     == std::string::npos);  // no other row repainted
    CHECK(out.find("DDD")     == std::string::npos);
    CHECK(out.find("CODE")    == std::string::npos);  // header/rules not re-emitted
    CHECK(out.find("----")    == std::string::npos);
}

// DOWN moves the selection cursor, not the window. A short window (13 rows ->
// data window of 2, after the detail panel's bottom reserve) overflows the
// 4-alarm set; with all four raised at once they keep table order (AAA, BBB,
// CCC, DDD). top=0 shows AAA,BBB; moving the cursor down past the window bottom
// auto-scrolls so CCC comes into view.
TEST_CASE("alarms render: DOWN moves the selection and scrolls it into view") {
    RenderFixture f(13);
    step_bits(f.bank, f.model, 0xF, 1000);
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

// SPACE cycles the filter mode, and the badge is the acknowledgement of that
// keypress - so it has to reach the wire before the table rewrite the same
// keypress triggered, not after a screenful of rows. The legend row is raised to
// PRIO_INPUT for exactly that, and it must survive the tick it was raised in:
// with a repaint pending the row still shows the old badge, so a naive
// "reconciled?" check reads true and demotes it before it is ever drawn.
TEST_CASE("alarms render: a mode change shows the new mode before the rows") {
    RenderFixture f(13);                          // short screen: rows overflow
    step_bits(f.bank, f.model, 0xF, 1000);
    f.page.focus();
    f.paint();                                    // settle the initial paint

    f.page.handle_text(' ');                      // recent -> all
    // One tick + one present: the badge should already be on the wire.
    f.page.tick(true);
    f.console.terminal().present();
    std::string first = f.capture();

    CHECK(first.find("all") != std::string::npos);   // the new mode badge
    CHECK(first.find("AAA") == std::string::npos);   // ...ahead of any data row
    CHECK(first.find("DDD") == std::string::npos);
    // Just the legend, not a table's worth of bytes riding along with it.
    CHECK(first.size() < 150);      // one row, not a table
    // And the page settles rather than churning on the raised row.
    f.paint();
    CHECK(f.paint().empty());
}
