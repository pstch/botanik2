/**
 * @file test_hint_paced.cpp
 * @brief The SHELL page under a real byte budget, checked against the
 *        independent VT once every frame has drained.
 *
 * A hint the ring cannot take this beat stays pending, and the rows it covers
 * wait with it. What that costs is the ledger's question; whether the screen
 * survives it is this one.
 */

#include <cstring>
#include <string>

#include "doctest.h"

#include "libbotanik.h"

#include "page_fixture.h"

#if BOTANIK_FEATURE_SHELL_PAGE

namespace tests { namespace hint_paced {

using Botanik::Terminal::InputEvent;
using Command = InputEvent::Command;

struct PacedRig : botanik_test::PageFixture {
    Botanik::Pages::ShellPage page;
    PacedRig() : PageFixture(24, 80), page(console) {
        reset_wire();
        page.focus();
        (void)paint(page);
        console.terminal().set_baud(4800);     // the budget the goldens were cut at
    }
    void type(const char* s) { page.handle_text_batch(s, (uint16_t)strlen(s)); }
    void key(Command c, uint16_t n = 1) { page.handle_command(c, n); }
    /** @brief Beats until the frame has drained, then what the VT holds. */
    std::string settle() {
        int quiet = 0;
        std::string out;
        for (int beat = 0; beat < 600 && quiet < 3; beat++) {
            page.tick(true);
            console.terminal().present();
            const std::string got = wire.capture();
            out += got;
            vt.feed(got);
            quiet = got.empty() ? quiet + 1 : 0;
        }
        return out;
    }
};

TEST_CASE("hints: the shell page at 4800 baud keeps the wire and FRONT in step") {
    PacedRig f;
    char line[80];
    for (int step = 0; step < 40; step++) {
        // A command and its reply, the way `net` fills the transcript, with a
        // two-row input under it every few steps.
        snprintf(line, sizeof line, "status %02d", step);
        f.type(line);
        f.key(Command::ENTER);
        f.settle();
        INFO("after submit " << step << ": " << f.wire_diff());
        REQUIRE(f.wire_diff().empty());

        if (step % 3 == 0) {
            static const char AL[] = "abcdefghijklmnopqrstuvwxyz0123456789";
            std::string s;
            for (uint16_t i = 0; i < 100; i++) s += AL[(i * 11u) % 36u];
            f.type(s.c_str());
            f.settle();
            INFO("after two-row input " << step << ": " << f.wire_diff());
            REQUIRE(f.wire_diff().empty());
            f.key(Command::CTRL_U);
            f.settle();
            INFO("after clear " << step << ": " << f.wire_diff());
            REQUIRE(f.wire_diff().empty());
        }
    }
}

}}  // namespace tests::hint_paced

#endif
