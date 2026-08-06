/**
 * @file test_shell.cpp
 * @brief CommandShell history recall (prev/next + draft restore, dup-skip) and the
 *        single-entry kill ring (Ctrl-K cut to EOL / Ctrl-Y yank), driven against a
 *        real TextEditor over a pipe-backed terminal.
 */
#include <cstdint>                    // uint16_t - the screen geometry the shell writes into
#include <cstring>                    // strstr / strlen - checking the command output
#include <string>                     // std::string - the captured output asserted on
#include <unistd.h>                   // pipe / read - draining what the shell emitted

#include "libbotanik/shell/shell.h"   // Shell::CommandShell - under test
#include "libbotanik/vars/declare.h"  // the factories the test table is written in

#include "doctest.h"                  // the test framework (vendored single header); last so it sees our types

using namespace Botanik;
using namespace Botanik::Vars;

namespace {

    // Sinks command output to nowhere; the tests only inspect the editor buffer.
    struct NullWriter : Shell::ShellWriter {
        size_t write(const char* /*buf*/, size_t len) override { return len; }
    };

    // ...and one that keeps it, for the tests that are about what a command SAYS.
    struct CapturingWriter : Shell::ShellWriter {
        std::string text;
        size_t write(const char* buf, size_t len) override {
            text.append(buf, len); text.push_back('\n'); return len;
        }
        bool has(const char* s) const { return text.find(s) != std::string::npos; }
    };

    // A CommandShell wired to a real editor + terminal whose serial is a pipe (TX is
    // buffered and never flushed, so reconcile output is harmlessly discarded).
    struct ShellFixture {
        int                            pipe_fds[2];
        HAL::Serial::Raw                 raw;
        Utils::BufferedSerial          serial;
        uint64_t                       now = 0;
        Terminal::Console              console;
        Terminal::EditorBuffers<Config::SHELL_INPUT_LEN,
                                Config::EDITOR_MAX_ROWS> bufs;
        Terminal::TextEditor           editor;
        NullWriter                     writer;
        Shell::CommandShell            shell;

        ShellFixture()
            : serial(&raw),
              console(serial, 80, 24, &now, 115200),
              editor(&console, bufs.storage()),
              shell(console, editor, writer) {
            REQUIRE(::pipe(pipe_fds) == 0);
            raw.set_fds(pipe_fds[0], pipe_fds[1]);
            editor.reset();
        }
        ~ShellFixture() { ::close(pipe_fds[0]); ::close(pipe_fds[1]); }

        std::string line() const { return std::string(editor.buffer(), editor.buffer_len()); }
    };

}

TEST_CASE("shell history: prev/next recall with draft restore") {
    ShellFixture f;
    f.shell.run("version", 7);
    f.shell.run("uptime", 6);

    f.shell.history_prev();              // newest
    CHECK(f.line() == "uptime");
    f.shell.history_prev();              // older
    CHECK(f.line() == "version");
    f.shell.history_prev();              // already oldest: no change
    CHECK(f.line() == "version");
    f.shell.history_next();              // newer
    CHECK(f.line() == "uptime");
    f.shell.history_next();              // back to the (empty) draft
    CHECK(f.line() == "");
}

TEST_CASE("shell history: an in-progress line is saved as the draft and restored") {
    ShellFixture f;
    f.shell.run("stats", 5);
    f.editor.insert_characters("dra", 3);   // in-progress line
    f.shell.history_prev();                  // saves "dra", recalls "stats"
    CHECK(f.line() == "stats");
    f.shell.history_next();                  // restores the draft
    CHECK(f.line() == "dra");
}

TEST_CASE("shell history: consecutive duplicates are not stored twice") {
    ShellFixture f;
    f.shell.run("help", 4);
    f.shell.run("help", 4);              // duplicate of the most recent: skipped
    f.shell.history_prev();
    CHECK(f.line() == "help");
    f.shell.history_prev();              // only one distinct entry: no older
    CHECK(f.line() == "help");
}

TEST_CASE("shell kill ring: Ctrl-K cuts to EOL, Ctrl-Y yanks it back") {
    ShellFixture f;
    f.editor.insert_characters("hello world", 11);
    for (int i = 0; i < 5; ++i) f.editor.move_left();   // cursor before "world"
    f.shell.kill_to_line_end();
    CHECK(f.line() == "hello ");
    f.editor.move_to_line_end();
    f.shell.paste_kill();
    CHECK(f.line() == "hello world");
}

// ---------------------------------------------------------------------------
// The variable commands, driven against a real bank so the output is the real
// output.
// ---------------------------------------------------------------------------

namespace {

    const Vars::Variable CMD_VARS[] = {
        MI("ext_temp", T_F32, 5, 3, 18, 2, SF(-40.0f), SF(85.0f), SF(20.5f), 0, 1,
                       "C", "outside", ""),
        G ("setpoint", T_F32, SF(18.0f), 1, "C", "target temp", ""),
    };

    /** @brief A shell with a bank behind it and its output kept. */
    struct CmdFixture {
        int                            pipe_fds[2];
        HAL::Serial::Raw               raw;
        Utils::BufferedSerial          serial;
        uint64_t                       now = 0;
        Terminal::Console              console;
        Terminal::EditorBuffers<Config::SHELL_INPUT_LEN,
                                Config::EDITOR_MAX_ROWS> bufs;
        Terminal::TextEditor           editor;
        CapturingWriter                writer;
        Vars::Value                    values[2]{};
        uint16_t                       index[2]{};
        Vars::Bank                     bank;
        Shell::CommandShell            shell;

        CmdFixture()
            : serial(&raw),
              console(serial, 80, 24, &now, 115200),
              editor(&console, bufs.storage()),
              shell(console, editor, writer) {
            REQUIRE(::pipe(pipe_fds) == 0);
            raw.set_fds(pipe_fds[0], pipe_fds[1]);
            editor.reset();
            Vars::VarTables t = {};
            t.var = CMD_VARS; t.val = values; t.count = 2; t.index = index;
            bank = Vars::Bank(t);
            bank.init();
            SystemContext ctx = console.context();
            ctx.vars = &bank;
            console.set_context(ctx);
        }
        ~CmdFixture() { ::close(pipe_fds[0]); ::close(pipe_fds[1]); }

        void run(const char* cmd) {
            writer.text.clear();
            shell.run(cmd, (uint16_t)std::strlen(cmd));
        }
    };

}

TEST_CASE("shell override: a bare name forces a GOOD reading; `clear` releases all") {
    // Freezing used to carry the row's current quality through, so a bare
    // `override` on a variable nothing had written yet handed back an override
    // that read as a dead sensor. Only the word `invalid` says that.
    CmdFixture f;
    REQUIRE_FALSE(f.bank.var("ext_temp").valid());   // nothing has published it

    f.run("override ext_temp");
    CHECK(f.bank.var("ext_temp").valid());
    f.run("override ext_temp invalid");
    CHECK_FALSE(f.bank.var("ext_temp").valid());

    f.run("override setpoint 5");
    REQUIRE(f.bank.override_count() == 2);
    f.run("override clear");
    CHECK(f.writer.has("cleared 2"));
    CHECK(f.bank.override_count() == 0);
}

TEST_CASE("shell get: the detail view, and what it adds when a row is forced") {
    CmdFixture f;
    f.bank.var("ext_temp").publish(Vars::Scalar::F(21.0f), 1000);

    f.run("get ext_temp");
    MESSAGE("get ext_temp ->\n" << f.writer.text);
    CHECK(f.writer.has("ext_temp = 21.0"));
    CHECK(f.writer.has("MI input, direct"));      // what kind of row
    CHECK(f.writer.has("group ext"));             // derived from the name
    CHECK(f.writer.has("read-only"));             // which command applies
    CHECK(f.writer.has("-40.0 .. 85.0"));         // the declared band
    CHECK(f.writer.has("Modbus dev 5"));          // where it comes from
    CHECK_FALSE(f.writer.has("Override"));        // nothing is forced

    // Forced: both numbers, because "has the sensor come back?" is the question
    // someone deciding whether to release an override actually has.
    f.run("override ext_temp 35");
    f.run("get ext_temp");
    MESSAGE("get ext_temp (forced) ->\n" << f.writer.text);
    CHECK(f.writer.has("forced by hand"));
    CHECK(f.writer.has("35.0"));
    CHECK(f.writer.has("live 21.0"));
}
