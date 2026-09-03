/**
 * @file test_shell.cpp
 * @brief The shell answered for by its OUTPUT rather than by a screen.
 *
 * CommandShell writes through a ShellWriter and depends on the terminal device
 * only for the width it wraps at, so a test needs neither a Console nor a page:
 * collect the writes and read them back. That is what makes the lifecycle
 * guard testable at all - `reset confirm` on a live firmware resets the host.
 */

#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik.h"

#include "term_fixture.h"

namespace shell_behaviour {

using namespace Botanik;

#if BOTANIK_FEATURE_SHELL_PAGE

/** @brief A ShellWriter that keeps what it was given. */
struct Collector : Shell::ShellWriter {
    std::string text;
    bool        logs = true;

    size_t write(const char* buf, size_t len) override {
        text.append(buf, len);
        return len;
    }
    void clear() override { text.clear(); }
    void set_logs_enabled(bool on) override { logs = on; }
    bool logs_enabled() const override { return logs; }

    bool said(const char* s) const { return text.find(s) != std::string::npos; }
};

struct ShellFixture {
    botanik_test::PipeSerial          wire;
    uint64_t                          now = 0;
    // A real Console: TextEditor's constructor takes console->terminal(), so a
    // null one is a dereference before the body runs. The shell never draws
    // through it here - what it says comes back through the Collector.
    Terminal::Console                 console;
    Terminal::EditorBuffers<2048, 16> store;
    Terminal::TextEditor              editor;
    Collector                         out;
    Shell::CommandShell               shell;

    ShellFixture()
        : console(SetupContext(), wire.serial, &now),
          editor(&console, store.storage()),
          shell(console.terminal(), editor, out) {}

    /** @brief Run one command line and hand back everything it printed. */
    const std::string& run(const char* line) {
        out.clear();
        shell.run(line, (uint16_t)strlen(line));
        return out.text;
    }
};

TEST_CASE("shell: a bare lifecycle verb refuses and says what it would cost") {
    ShellFixture f;

    struct Case { const char* verb; const char* consequence; };
    static const Case CASES[] = {
        { "halt",   "console"  },   // ...and that only `reset` recovers
        { "reset",  "mid-beat" },
        { "reboot", "watchdog" },
        { "exit",   "hardware" },
    };

    for (const Case& c : CASES) {
        INFO(c.verb);
        const std::string said = f.run(c.verb);
        CHECK(said.find(c.verb) != std::string::npos);
        INFO("the refusal must name the consequence, not just the syntax");
        CHECK(said.find(c.consequence) != std::string::npos);
        INFO("...and say what to type");
        CHECK(said.find("confirm") != std::string::npos);

        // One row at 80 columns, or the instruction is the half that wraps.
        INFO("the refusal must fit an 80-column row: '" << said << "'");
        CHECK(said.size() <= 79);
    }
}

TEST_CASE("shell: a lifecycle verb refuses anything that is not exactly confirm") {
    ShellFixture f;

    // Not the word, the word twice, and a prefix of it. A prefix matching would
    // be the dangerous failure: `halt c` is a typo, not a decision.
    static const char* const NOT_CONFIRMED[] = {
        "halt yes", "halt c", "halt confir", "halt confirmed",
        "halt confirm confirm", "halt CONFIRM x",
    };
    for (const char* line : NOT_CONFIRMED) {
        INFO(line);
        CHECK(f.run(line).find("type `halt confirm`") != std::string::npos);
    }
}

TEST_CASE("shell: halt confirm reaches the health monitor") {
    ShellFixture f;

    // No health monitor wired, so a guard that PASSED lands on the handler's
    // own unavailability line. The only way to prove acceptance without
    // resetting the process running this test.
    const std::string said = f.run("halt confirm");
    CHECK(said.find("confirm") == std::string::npos);
    CHECK(said.find("unavailable") != std::string::npos);
}

TEST_CASE("shell: the help table states the requirement it enforces") {
    ShellFixture f;
    // `help` reads the Builtin row, so a guard added to a handler alone would
    // leave help telling an operator to type a command that now refuses.
    const std::string said = f.run("help system");
    for (const char* verb : { "halt", "reset", "reboot", "exit" }) {
        INFO(verb);
        const size_t at = said.find(verb);
        REQUIRE(at != std::string::npos);
        CHECK(said.find("confirm", at) != std::string::npos);
    }
}

TEST_CASE("shell: breakpoint refuses when nothing is attached to trap into") {
    // Guarded on the real predicate, not skipped: under gdb the command is
    // meant to fire, and asserting the refusal there would SIGTRAP the suite.
    if (HAL::Debug::under_debugger()) return;

    ShellFixture f;
    const std::string said = f.run("breakpoint");
    CHECK(said.find("no debugger") != std::string::npos);
    INFO("the refusal has to say why, or it reads as a broken command");
    CHECK(said.find("fault") != std::string::npos);
}

TEST_CASE("shell: an unguarded command still runs on its own") {
    ShellFixture f;
    // The guard must not have leaked into the rest of the table.
    CHECK(f.run("logs off").find("disabled") != std::string::npos);
    CHECK(f.out.logs == false);
    CHECK(f.run("logs on").find("enabled") != std::string::npos);
    CHECK(f.out.logs == true);
}

#endif // BOTANIK_FEATURE_SHELL_PAGE

}  // namespace shell_behaviour
