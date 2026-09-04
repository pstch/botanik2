/**
 * @file test_shell_wire.cpp
 * @brief The SHELL page on the wire: the editor over a transcript.
 *
 * test_editor_wire.cpp bills the editor alone. Here it sits on the page that
 * owns a scrollback above it, so what a shrink or a grow costs includes the
 * log rows the band scroll reveals or hides. Same two checks: the VT lands
 * where FRONT asked, and each action has a committed byte ceiling.
 */

#include <cstdlib>
#include <cstring>
#include <string>

#include "doctest.h"

#include "libbotanik.h"

#include "page_fixture.h"

#if BOTANIK_FEATURE_SHELL_PAGE

namespace shell_wire {

using Botanik::Terminal::InputEvent;
using Command = InputEvent::Command;

struct ShellRig : botanik_test::PageFixture {
    Botanik::Pages::ShellPage page;
    ShellRig() : PageFixture(24, 80), page(console) {
        reset_wire();
        page.focus();
        (void)paint(page);
    }
    void type(const char* s) { page.handle_text_batch(s, (uint16_t)strlen(s)); }
    void key(Command c, uint16_t n = 1) { page.handle_command(c, n); }
    /** @brief @p n submitted lines, so the transcript reaches the top. */
    void transcript(int n) {
        char line[32];
        for (int i = 0; i < n; i++) {
            snprintf(line, sizeof line, "line%02d", i);
            type(line);
            key(Command::ENTER);
            (void)paint(page);
        }
    }
    /** @brief A two-row input: 100 characters of text with no short period. */
    void two_rows() {
        static const char AL[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        std::string s;
        for (uint16_t i = 0; i < 100; i++) s += AL[(i * 11u) % 36u];
        type(s.c_str());
        (void)paint(page);
    }
};

struct Scenario {
    const char* name;
    void      (*reach)(ShellRig&);
    void      (*act)(ShellRig&);
    uint16_t    bytes;          /**< A CEILING, as in test_editor_wire.cpp. */
};

static const Scenario SCENARIOS[] = {
    { "SHELL typing wraps onto a second row over a transcript",
      // 71 characters fill PS1's row exactly; the 72nd opens the second.
      [](ShellRig& f) { f.transcript(30); f.two_rows(); f.key(Command::BACKSPACE, 29); (void)f.paint(f.page); },
      [](ShellRig& f) { f.type("0"); }, 19 },

    { "SHELL backspace joins two rows over a transcript",
      [](ShellRig& f) { f.transcript(30); f.two_rows(); },
      [](ShellRig& f) { f.key(Command::BACKSPACE, 30); }, 43 },

    { "SHELL Ctrl-U clears a two-row input over a transcript",
      [](ShellRig& f) { f.transcript(30); f.two_rows(); },
      [](ShellRig& f) { f.key(Command::CTRL_U); }, 39 },

    { "SHELL Enter submits a two-row input over a transcript",
      [](ShellRig& f) { f.transcript(30); f.two_rows(); },
      [](ShellRig& f) { f.key(Command::ENTER); }, 177 },
};

static constexpr size_t N_SCENARIOS = sizeof(SCENARIOS) / sizeof(SCENARIOS[0]);

static size_t run(ShellRig& f, const Scenario& s) {
    s.reach(f);
    (void)f.paint(f.page);
    f.drain();
    s.act(f);
    return f.paint(f.page).size();
}

TEST_CASE("shell: the wire puts the screen where FRONT asked for it") {
    for (size_t i = 0; i < N_SCENARIOS; i++) {
        const Scenario& s = SCENARIOS[i];
        ShellRig* f = new ShellRig();
        (void)run(*f, s);
        f->converged(f->page, s.name);
        delete f;
    }
}

TEST_CASE("shell: an edit costs the bytes it is budgeted") {
    std::string ledger;
    bool moved = false;
    char line[128];
    for (size_t i = 0; i < N_SCENARIOS; i++) {
        const Scenario& s = SCENARIOS[i];
        ShellRig* f = new ShellRig();
        const size_t got = run(*f, s);
        f->console.terminal().establish_screen();
        f->page.redraw();
        const size_t repaint = f->paint(f->page).size();
        delete f;
        if (got != s.bytes) moved = true;
        snprintf(line, sizeof line, "  %5u  (ceiling %5u)  repaint %5u   %s\n",
                 (unsigned)got, (unsigned)s.bytes, (unsigned)repaint, s.name);
        ledger += line;
        INFO("scenario: " << s.name);
        CHECK((unsigned)got <= (unsigned)s.bytes);
    }
    if (moved || getenv("BOTANIK_LEDGER") != nullptr)
        MESSAGE("shell wire-byte ledger:\n" << ledger);
}

}  // namespace shell_wire

#endif  // BOTANIK_FEATURE_SHELL_PAGE
