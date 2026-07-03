/**
 * @file test_shell.cpp
 * @brief CommandShell history recall (prev/next + draft restore, dup-skip) and the
 *        single-entry kill ring (Ctrl-K cut to EOL / Ctrl-Y yank), driven against a
 *        real TextEditor over a pipe-backed terminal.
 */
#include <cstdint>
#include <cstring>
#include <string>
#include <unistd.h>

#include "doctest.h"

#include "libbotanik/shell/shell.h"

using namespace Botanik;

namespace {

    // Sinks command output to nowhere; the tests only inspect the editor buffer.
    struct NullWriter : Shell::ShellWriter {
        size_t write(const char* /*buf*/, size_t len) override { return len; }
    };

    // A CommandShell wired to a real editor + terminal whose serial is a pipe (TX is
    // buffered and never flushed, so reconcile output is harmlessly discarded).
    struct ShellFixture {
        int                            pipe_fds[2];
        HAL::Serial::Raw                 raw;
        Utils::BufferedSerial          serial;
        uint32_t                       now = 0;
        Terminal::Console              console;
        Terminal::TextEditor           editor;
        NullWriter                     writer;
        Shell::CommandShell            shell;

        ShellFixture()
            : serial(&raw),
              console(serial, 80, 24, &now, 115200),
              editor(&console),
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
