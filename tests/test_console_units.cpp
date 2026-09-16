/**
 * @file test_console_units.cpp
 * @brief The console's storage and navigation primitives, tested where they
 *        live rather than through a page that happens to use them.
 *
 * LineBuffer, Scrollback and ScrollTablePage carry every page's history and
 * cursor between them and had no direct coverage: what they do was only ever
 * asserted at one screen size, through one page, via a golden capture.
 */

#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik.h"

#include "page_fixture.h"
#include "site_bank.h"

namespace tests { namespace console_units {

using namespace Botanik;

#if BOTANIK_FEATURE_CONSOLE

// LineBuffer

typedef Pages::LineBuffer<4, 16> Buf4x16;

/** @brief The stored lines, newest LAST, so a case reads in screen order. */
static std::vector<std::string> stored(const Buf4x16& b) {
    std::vector<std::string> out;
    for (uint16_t i = b.count(); i >= 1; i--) {
        uint16_t len = 0;
        const char* s = b.line(i, len);
        REQUIRE(s != nullptr);
        out.push_back(std::string(s, len));
    }
    return out;
}

static void put(Buf4x16& b, const char* s) { b.store(s, strlen(s)); }

TEST_CASE("line buffer: the ring forgets the oldest and keeps the order") {
    Buf4x16 b;
    CHECK(b.count() == 0);

    put(b, "one"); put(b, "two"); put(b, "three");
    CHECK(b.count() == 3);
    CHECK(stored(b) == std::vector<std::string>({"one", "two", "three"}));

    // Past capacity: the count pins at L and the oldest goes, twice over, so
    // the case also crosses the modulo wrap rather than stopping at it.
    put(b, "four"); put(b, "five"); put(b, "six");
    CHECK(b.count() == 4);
    CHECK(stored(b) == std::vector<std::string>({"three", "four", "five", "six"}));
}

TEST_CASE("line buffer: an out-of-range line is null, not a wrapped one") {
    Buf4x16 b;
    uint16_t len = 0;
    CHECK(b.line(1, len) == nullptr);      // empty
    put(b, "one");
    CHECK(b.line(0, len) == nullptr);      // 1-based: 0 is not the newest
    CHECK(b.line(2, len) == nullptr);      // one past what is stored
    CHECK(b.line(1, len) != nullptr);
    CHECK(len == 3);
}

TEST_CASE("line buffer: a line longer than N is clipped, never overrun") {
    Buf4x16 b;
    const std::string long_line(64, 'x');
    b.store(long_line.data(), long_line.size());
    uint16_t len = 0;
    REQUIRE(b.line(1, len) != nullptr);
    CHECK(len == 16);                      // N, not 64
}

TEST_CASE("line buffer: store_wrapped breaks on width and on newlines") {
    Buf4x16 b;

    SUBCASE("width") {
        // 10 characters at 4 columns: three full rows and a remainder.
        CHECK(b.store_wrapped("abcdefghij", 10, 4, "", 0) == 3);
        CHECK(stored(b) == std::vector<std::string>({"abcd", "efgh", "ij"}));
    }
    SUBCASE("an embedded newline breaks the row and is not printed") {
        CHECK(b.store_wrapped("ab\ncd", 5, 40, "", 0) == 2);
        CHECK(stored(b) == std::vector<std::string>({"ab", "cd"}));
    }
    SUBCASE("cols 0 means unbounded: one row, whatever the length") {
        CHECK(b.store_wrapped("abcdefghij", 10, 0, "", 0) == 1);
        CHECK(stored(b) == std::vector<std::string>({"abcdefghij"}));
    }
    SUBCASE("a short message is one row and always at least one") {
        CHECK(b.store_wrapped("hi", 2, 40, "> ", 2) == 1);
        CHECK(stored(b) == std::vector<std::string>({"hi"}));
    }
}

TEST_CASE("line buffer: the continuation prefix marks every row but the first") {
    Buf4x16 b;
    CHECK(b.store_wrapped("abcdefgh", 8, 4, "> ", 2) == 3);
    // First row gets the full width; each continuation spends 2 of its 4 on
    // the marker, so they carry 2 characters each.
    CHECK(stored(b) == std::vector<std::string>({"abcd", "> ef", "> gh"}));
}

TEST_CASE("line buffer: a prefix that would leave nothing to read is dropped") {
    Buf4x16 b;
    // At 3 columns a 2-byte prefix leaves 1 for content, under CONT_MIN. The
    // marker goes rather than fanning the line out one character per row -
    // which at 132 columns of history is the difference between 4 rows and 12.
    CHECK(b.store_wrapped("abcdef", 6, 3, "> ", 2) == 2);
    CHECK(stored(b) == std::vector<std::string>({"abc", "def"}));
}

TEST_CASE("line buffer: a prefix wider than the segment does not underflow") {
    Buf4x16 b;
    const std::string prefix(20, '>');     // longer than N = 16
    CHECK(b.store_wrapped("abcdefghijklmnopqrst", 20, 8,
                          prefix.c_str(), (uint16_t)prefix.size()) >= 1);
    for (uint16_t i = 1; i <= b.count(); i++) {
        uint16_t len = 0;
        REQUIRE(b.line(i, len) != nullptr);
        CHECK(len <= 16);
    }
}

// Scrollback

/** @brief A Scrollback over the band a 24-row console gives it. */
struct BackFixture : botanik_test::PageFixture {
    Pages::LogsScrollback sb;

    /** @brief @p n stored lines, wrapped at the fixture's width. */
    void fill(uint16_t n) {
        for (uint16_t i = 0; i < n; i++) {
            char line[32];
            snprintf(line, sizeof(line), "line %u", (unsigned)i);
            sb.ingest(line, strlen(line), console.terminal().cols(), "  ", 2, true);
        }
    }
    bool scroll(bool up, uint16_t n = 1) {
        return sb.scroll(up, n, (uint16_t)(console.rows() - 1));
    }
};

TEST_CASE("scrollback: a width change drops the history, and only a change") {
    BackFixture f;
    f.fill(5);
    REQUIRE(f.sb.count() == 5);

    // The wrap is baked into what is stored, so a re-wrap is not possible and
    // clipping half a line would lie about the content. Dropping it says so.
    f.sb.reflow(80);
    CHECK(f.sb.count() == 5);              // the width it was already at
    f.sb.reflow(80);
    CHECK(f.sb.count() == 5);              // idempotent
    f.sb.reflow(0);
    CHECK(f.sb.count() == 5);              // 0 is "unknown", not a new width
    f.sb.reflow(132);
    CHECK(f.sb.count() == 0);
}

TEST_CASE("scrollback: the first width to be declared keeps the history") {
    BackFixture f;
    // _wrap_cols starts 0, so the first reflow() adopts rather than clears -
    // otherwise every page would lose its replay on the first paint.
    Pages::LogsScrollback fresh;
    fresh.ingest("hello", 5, 0, "", 0, false);
    REQUIRE(fresh.count() == 1);
    fresh.reflow(80);
    CHECK(fresh.count() == 1);
}

TEST_CASE("scrollback: scrolling stops at both ends of the history") {
    BackFixture f;                          // 24 rows: a 23-row band
    f.fill(40);

    CHECK_FALSE(f.scroll(/*up=*/false));    // already at the newest
    CHECK(f.scroll(/*up=*/true));           // into the history

    for (int i = 0; i < 200; i++) f.scroll(true);
    INFO("scrolled past the oldest line 200 times over");
    CHECK_FALSE(f.scroll(true));            // pinned at the top

    for (int i = 0; i < 200; i++) f.scroll(false);
    CHECK_FALSE(f.scroll(false));           // and back at the bottom
}

TEST_CASE("scrollback: a history shorter than the window does not scroll") {
    BackFixture f;
    f.fill(3);                              // far fewer than the 23-row band
    CHECK_FALSE(f.scroll(true));
    CHECK_FALSE(f.scroll(false));
}

TEST_CASE("scrollback: clear forgets the lines and releases the scroll") {
    BackFixture f;
    f.fill(40);
    REQUIRE(f.scroll(true));
    f.sb.clear();
    CHECK(f.sb.count() == 0);
    CHECK_FALSE(f.scroll(true));            // nothing left to scroll into
}

// ScrollTablePage

/** @brief The base with a row count and nothing else, so the navigation is
 *  answered for without a model, a bank or a real table's drawing. */
struct Table : Pages::ScrollTablePage {
    uint16_t rows_;
    uint16_t win_;

    Table(Terminal::Console& c, uint16_t rows, uint16_t win)
        : ScrollTablePage(c), rows_(rows), win_(win) {}

    const char* name() const override { return "TABLE"; }
    uint16_t _data_top()  const override { return 3; }
    uint16_t _window()    const override { return win_; }
    uint16_t _row_count() const override { return rows_; }

    // Page's own surface, none of which the navigation reads.
    void handle_text(char) override {}
    void handle_command(Command, uint16_t) override {}
    void deactivate() override {}
    void focus() override {}
    void tick(bool) override {}
    uint8_t _columns(Col*) const override { return 0; }

    uint16_t selected() const { return _selected; }
    uint16_t top()      const { return _top; }
    bool nav(Command cmd) { return _nav_command(cmd); }
    /** @brief The pair every real caller makes: the cursor back inside the set,
     *  then the window back under the cursor. _clamp_selection() alone moves
     *  only the cursor, which is what its name says. */
    void reshaped() { _clamp_selection(); _follow_selection(); }
};

TEST_CASE("scroll table: the cursor stops at both ends") {
    botanik_test::PageFixture f;
    Table t(f.console, 10, 5);

    CHECK(t.selected() == 0);
    // UP at the top is still UP: _nav_command answers "was this mine", not
    // "did anything move", so it consumes the key and the cursor stays.
    CHECK(t.nav(Terminal::InputEvent::Command::UP));
    CHECK(t.selected() == 0);

    for (int i = 0; i < 50; i++) t.nav(Terminal::InputEvent::Command::DOWN);
    CHECK(t.selected() == 9);                                // the last row, not the 50th
}

TEST_CASE("scroll table: the window follows the cursor and no further") {
    botanik_test::PageFixture f;
    Table t(f.console, 10, 5);

    // Inside the first window nothing scrolls: the cursor moves alone.
    for (int i = 0; i < 4; i++) t.nav(Terminal::InputEvent::Command::DOWN);
    CHECK(t.selected() == 4);
    CHECK(t.top() == 0);

    t.nav(Terminal::InputEvent::Command::DOWN);
    CHECK(t.selected() == 5);
    CHECK(t.top() == 1);                                     // one row, not a jump

    t.nav(Terminal::InputEvent::Command::END);
    CHECK(t.selected() == 9);
    CHECK(t.top() == 5);                                     // _max_top: 10 - 5
    t.nav(Terminal::InputEvent::Command::HOME);
    CHECK(t.selected() == 0);
    CHECK(t.top() == 0);
}

TEST_CASE("scroll table: a page step keeps half the window for context") {
    botanik_test::PageFixture f;
    Table t(f.console, 100, 10);

    t.nav(Terminal::InputEvent::Command::PAGE_DOWN);
    CHECK(t.selected() == 5);                                // half of 10
    t.nav(Terminal::InputEvent::Command::PAGE_UP);
    CHECK(t.selected() == 0);
}

TEST_CASE("scroll table: a table that shrinks under the cursor clamps it") {
    botanik_test::PageFixture f;
    Table t(f.console, 100, 10);
    t.nav(Terminal::InputEvent::Command::END);
    REQUIRE(t.selected() == 99);

    // What a row filter does: the set narrows while the cursor is deep in it.
    t.rows_ = 3;
    t.reshaped();
    CHECK(t.selected() == 2);
    CHECK(t.top() <= t.selected());

    // ...and all the way to nothing, which a filter matching no row reaches.
    t.rows_ = 0;
    t.reshaped();
    CHECK(t.selected() == 0);
    CHECK(t.top() == 0);
}

TEST_CASE("scroll table: a window of zero does not divide by it") {
    botanik_test::PageFixture f;
    // _window() is documented to return 0 on a screen too short for the band.
    Table t(f.console, 10, 0);
    t.nav(Terminal::InputEvent::Command::PAGE_DOWN);
    t.nav(Terminal::InputEvent::Command::END);
    t.reshaped();
    CHECK(t.selected() < 10);
}

TEST_CASE("scroll table: a key the table does not own is left to the page") {
    botanik_test::PageFixture f;
    Table t(f.console, 10, 5);
    // The return value is the contract: false means "not mine", and a page
    // that got true for its own key would stop handling it.
    CHECK_FALSE(t.nav(Terminal::InputEvent::Command::ENTER));
    CHECK(t.nav(Terminal::InputEvent::Command::DOWN));
}


#if BOTANIK_FEATURE_ALARMS_PAGE
/** @brief AlarmsPage with its scroll state readable. */
struct AlarmProbe : Pages::AlarmsPage {
    AlarmProbe(Terminal::Console& c, Alarms::AlarmController* m)
        : AlarmsPage(c, m) {}
    uint16_t top()     const { return _top; }
    uint16_t max_top() const { return _max_top(); }
    void key(Terminal::InputEvent::Command c) { handle_command(c, 1); }
};

TEST_CASE("alarms: switching to a shorter view does not strand the window") {
    botanik_test::PageFixture f;
    Vars::Bank bank;
    TestSite::build(bank);
    Vars::BankAlarms sources;
    sources.bind(&bank);
    Alarms::AlarmController model(SetupContext(), &sources);
    model.setup(0);

    AlarmProbe page(f.console, &model);
    page.focus();
    f.paint(page);

    // ALL lists every declared alarm; nothing is active, so the filtered views
    // are empty. Scroll to the bottom of the long one, then switch.
    page.key(Terminal::InputEvent::Command::RIGHT);   // -> ALL
    REQUIRE(page.max_top() > 0);   // more alarms than the band holds
    page.key(Terminal::InputEvent::Command::END);
    const uint16_t deep = page.top();
    REQUIRE(deep > 0);

    page.key(Terminal::InputEvent::Command::RIGHT);   // -> ACTIVE (empty)
    INFO("top " << page.top() << ", max_top " << page.max_top()
         << " (was " << deep << ")");
    CHECK(page.top() <= page.max_top());
}

TEST_CASE("alarms: a view that empties under a stationary cursor lets go") {
    // The other way the visible set shrinks: no key is pressed at all, the
    // alarms simply clear. _cycle_mode() zeroes _top, so the mode path above is
    // covered; nothing zeroes it here.
    botanik_test::PageFixture f;
    Vars::Bank bank;
    TestSite::build(bank);
    Vars::BankAlarms sources;
    sources.bind(&bank);
    Alarms::AlarmController model(SetupContext(), &sources);
    model.setup(0);

    // Every alarm on, so ACTIVE is as long as ALL.
    uint64_t now = 1000;
    for (uint8_t i = 0; i < model.count(); i++) bank.var(sources.row(i)).set(true);
    model.step(now);

    AlarmProbe page(f.console, &model);
    page.focus();
    page.key(Terminal::InputEvent::Command::RIGHT);
    page.key(Terminal::InputEvent::Command::RIGHT);   // -> ACTIVE
    f.paint(page);
    page.key(Terminal::InputEvent::Command::END);
    REQUIRE(page.top() > 0);

    // ...and now they all clear, with the operator's hands off the keyboard.
    for (uint8_t i = 0; i < model.count(); i++) bank.var(sources.row(i)).set(false);
    now += 60000;
    model.step(now);
    page.tick(true);
    f.paint(page);

    INFO("top " << page.top() << ", max_top " << page.max_top());
    CHECK(page.top() <= page.max_top());
}
#endif


// AnsiTerminal

/** @brief A terminal on a pipe, with the grid readable and the wire capturable. */
struct TermFixture {
    botanik_test::PipeSerial wire;
    Terminal::AnsiTerminal   term;

    TermFixture(uint16_t cols = 80, uint16_t rows = 24) : term(wire.serial) {
        term.reconfigure(Terminal::Ansi::TerminalType::VT102, cols, rows);
        term.set_baud(4000000u);           // pacing is not what these assert
        term.establish_screen();
        flush();
    }

    /** @brief Push whatever present() staged, and hand back the wire bytes. */
    std::string flush() {
        std::string out;
        for (int i = 0; i < 64; i++) { term.present(); out += wire.capture(); }
        return out;
    }

    void put(uint16_t row, uint16_t col, const char* s) {
        term.move_to({row, col});
        term.write_chars(s, (uint16_t)strlen(s));
    }

    std::string row(uint16_t r) {
        char buf[160];
        const uint16_t n = term.front_row(r, buf, sizeof buf);
        std::string out(buf, n);
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }
};

TEST_CASE("terminal: what is written is what the grid holds") {
    TermFixture f;
    f.put(3, 2, "hello");
    CHECK(f.row(3) == "  hello");

    // Overwriting in place replaces rather than accumulating.
    f.put(3, 2, "world");
    CHECK(f.row(3) == "  world");
}

TEST_CASE("terminal: a run past the last column stops at it") {
    TermFixture f(20, 24);
    const std::string wide(40, 'x');
    f.put(1, 15, wide.c_str());
    INFO("wrote 40 characters at column 15 of a 20-column screen");
    CHECK(f.row(1).size() <= 20);
}

TEST_CASE("terminal: present emits nothing when nothing changed") {
    TermFixture f;
    f.put(2, 0, "steady");
    REQUIRE(f.flush().size() > 0);          // the first paint reaches the wire

    // The whole point of the FRONT/BACK diff: an unchanged frame is free. A
    // page that repaints every tick is still correct, so nothing else notices.
    f.put(2, 0, "steady");
    CHECK(f.flush() == "");
}

TEST_CASE("terminal: only the changed cells reach the wire") {
    TermFixture f;
    f.put(5, 0, "aaaaaaaaaaaaaaaaaaaa");
    f.flush();

    f.put(5, 10, "b");                      // one cell of twenty
    const std::string sent = f.flush();
    INFO("sent: '" << sent << "'");
    CHECK(sent.find('b') != std::string::npos);
    // A cursor address plus one byte, not the row again. Generous bound: the
    // assertion is "not a repaint", not an exact encoding.
    CHECK(sent.size() < 20);
}

TEST_CASE("terminal: rows repainted one higher go out as a row slide") {
    TermFixture f;
    // Rows that differ throughout, so rewriting one over its neighbour costs.
    char line[6][61];
    for (uint16_t r = 0; r < 6; r++) {
        for (uint16_t c = 0; c < 60; c++) line[r][c] = (char)('a' + (r * 7 + c * 3) % 26);
        line[r][60] = '\0';
        f.put((uint16_t)(r + 1), 0, line[r]);
    }
    f.flush();

    for (uint16_t r = 0; r < 5; r++) f.put((uint16_t)(r + 1), 0, line[r + 1]);
    f.put(6, 0, "                                                            ");
    const std::string sent = f.flush();
    INFO("sent: '" << sent << "'");
    CHECK(sent.find("\x1b[M") != std::string::npos);      // one DL
    CHECK(sent.size() < 30);                               // and no repaint
    CHECK(f.row(1) == std::string(line[1]));
}

TEST_CASE("terminal: a row's priority is a standing property") {
    TermFixture f;
    CHECK(f.term.row_priority(4) == Terminal::PRIO_STRUCT);   // the default

    f.term.set_row_priority(0, Terminal::PRIO_CHROME);
    f.term.set_row_priority(4, 8, Terminal::PRIO_INPUT);
    CHECK(f.term.row_priority(0) == Terminal::PRIO_CHROME);
    CHECK(f.term.row_priority(4) == Terminal::PRIO_INPUT);
    CHECK(f.term.row_priority(8) == Terminal::PRIO_INPUT);
    CHECK(f.term.row_priority(9) == Terminal::PRIO_STRUCT);   // past the band

    // Out of range answers rather than reading off the end.
    CHECK(f.term.row_priority(60000) == Terminal::PRIO_STRUCT);
}

TEST_CASE("terminal: the urgent row goes first when the budget is short") {
    // At 4800 baud a tick carries a handful of bytes, which is the whole reason
    // the classes exist: the edit point must not queue behind a page repaint.
    TermFixture f;
    f.term.set_baud(4800);
    f.term.set_row_priority(1, 20, Terminal::PRIO_STRUCT);
    f.term.set_row_priority(23, Terminal::PRIO_INPUT);

    for (uint16_t r = 1; r <= 20; r++)
        f.put(r, 0, "a page repaint, twenty rows of it, all at once");
    f.put(23, 0, "URGENT");
    f.term.restart_scan();

    // One present(), not the drain loop: what does the first tick carry?
    std::string first;
    for (int i = 0; i < 3 && first.find("URGENT") == std::string::npos; i++) {
        f.term.present();
        first += f.wire.capture();
    }
    INFO("first ticks carried: '" << first << "'");
    CHECK(first.find("URGENT") != std::string::npos);
    INFO("...and the page repaint must still be waiting, or the budget was "
         "never short and this case asserts nothing");
    CHECK(first.find("twenty rows") == std::string::npos);
}

TEST_CASE("terminal: establish_screen makes the next paint a full one") {
    TermFixture f;
    f.put(6, 0, "content");
    f.flush();
    REQUIRE(f.flush() == "");               // settled

    // What Ctrl+L does: FRONT is blanked, so everything is a change again.
    f.term.establish_screen();
    CHECK(f.row(6) == "");
}

#endif // BOTANIK_FEATURE_CONSOLE

}}  // namespace tests::console_units
