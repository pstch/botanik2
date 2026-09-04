/**
 * @file test_alarms.cpp
 * @brief Alarm conditions, their rendering, and the MQTT notifier.
 *
 * Each section is its own namespace, isolating fixtures and helpers.
 */

#include <cstddef>
#include <vector>
#include "libbotanik/alarms/alarms.h"
#include "libbotanik/vars/alarm_rows.h"
#include "libbotanik/alarms/notifier.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/vars/declare.h"
#include "site_bank.h"
#include "doctest.h"
#include "term_fixture.h"
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/pages/alarms.h"
#include <cstring>
#include "libbotanik_config.h"
#include "libbotanik/alarms/publisher.h"

// alarms

namespace alarms {

using namespace Botanik;

namespace {
    // Sets condition i's variable to bit i, then steps: an alarm is its row.
    void step_bits(Vars::Bank& io, Vars::BankAlarms& rows, Alarms::AlarmController& m,
                   uint64_t bits, uint64_t now) {
        for (uint8_t i = 0; i < m.count(); i++)
            io.var(rows.row(i)).set(((bits >> i) & 1u) != 0);
        m.step(now);
    }

    // A K_ALARM row: A(name, code, on_s, off_s, recent_s, hold_s, reg,
    // desc, notes).

    // Two unmasked, unlatched conditions (announce disabled) at bits 0 and 1.
    const Vars::Descriptor TABLE[] = {
        Vars::A("aaa", "AAA", -1, -1, 0, 0, 800, "alarm A", ""),
        Vars::A("bbb", "BBB", -1, -1, 0, 0, 801, "alarm B", ""),
    };
    // hold_s=300, like platform STACK/CPU_LOAD/CPU_JITT/CPU_XRUN.
    const Vars::Descriptor HOLD_TABLE[] = {
        Vars::A("hld", "HLD", 5, 5, 0, 300, 805, "hold on", ""),
    };
    // START after a 2 s dwell, END at once (off_s 0); a shorter blip -> FLAP.
    const Vars::Descriptor FLAP_TABLE[] = {
        Vars::A("flp", "FLP", 2, 0, 0, 0, 803, "flap", ""),
    };
    // on_s 0 so the START is announced at once.
    const Vars::Descriptor NORMAL_TABLE[] = {
        Vars::A("nrm", "NRM", 0, 0, 0, 0, 804, "normal", ""),
    };

    /** @brief A bank over one of the tables above - what the model scans. */
    struct AlarmBank {
        Vars::Bank       bank;
        /** @brief The K_ALARM rows as an alarm source - what the controller
         *  reads now, in place of the bank itself. */
        Vars::BankAlarms rows;
        AlarmBank(const Vars::Descriptor* v, uint16_t n) {
            TestSite::build(bank, v, n);
            rows.bind(&bank);
        }
    };

    // Records the topic of every entry the logger forwards, for the FLAP tests.
    struct TopicRecorder : Logs::LogWriter {
        std::vector<Logs::LogTopic> topics;
        void     push(const Logs::LogEntry* e) override { topics.push_back(e->topic); }
        void     drain() override {}
        uint32_t loss_count() const override { return 0; }
        size_t   format(const Logs::LogEntry*, char*, size_t) override { return 0; }
        size_t   write(const char*, size_t) override { return 0; }
        bool     has(Logs::LogTopic t) const {
            for (Logs::LogTopic x : topics) if (x == t) return true;
            return false;
        }
    };
}

TEST_CASE("alarm: ack marks an active condition, ignores inactive / unknown") {
    AlarmBank f(TABLE, 2);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.rows);
    m.setup(0);   // the scan is on_setup(): the rows exist now
    step_bits(f.bank, f.rows, m, 0x1, 1000);                              // raise A (bit 0)
    CHECK(m.active_count() == 1);
    CHECK(m.condition(0).user_ack == false);
    CHECK(m.ack("aaa") == true);
    CHECK(m.condition(0).user_ack == true);
    CHECK(m.ack("bbb") == false);                   // B is not active
    CHECK(m.ack("zzz") == false);                   // unknown code
    // Both must resolve: the alarm bar shows CODE, `alarms list` shows NAME.
    CHECK(m.ack("AAA") == true);
    CHECK(m.index_of("BBB") == m.index_of("bbb"));
}

TEST_CASE("alarm: an ack belongs to one episode - either edge drops it") {
    AlarmBank f(TABLE, 2);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.rows);
    m.setup(0);   // the scan is on_setup(): the rows exist now
    step_bits(f.bank, f.rows, m, 0x1, 1000);
    m.ack("aaa");
    CHECK(m.condition(0).user_ack == true);
    step_bits(f.bank, f.rows, m, 0x0, 2000);                              // A clears
    CHECK(m.condition(0).user_ack == false);    // dropped when the alarm stopped
    step_bits(f.bank, f.rows, m, 0x1, 3000);                              // A re-fires
    CHECK(m.condition(0).user_ack == false);    // and the fresh episode is unack'd
}

TEST_CASE("alarm: an ack survives the raw bit dropping while the hold is still up") {
    // hold_s extends the EPISODE past the raw bit, and the ack goes with the
    // episode: the alarm has not stopped while the hold is up.
    AlarmBank f(HOLD_TABLE, 1);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.rows);
    m.setup(0);
    step_bits(f.bank, f.rows, m, 0x1, 1000);
    CHECK(m.ack("hld") == true);
    step_bits(f.bank, f.rows, m, 0x0, 2000);        // raw drops, 300 s hold still up
    CHECK(m.condition(0).active   == true);
    CHECK(m.condition(0).user_ack == true);
    step_bits(f.bank, f.rows, m, 0x0, 400000);      // hold drained: it is down now
    CHECK(m.condition(0).active   == false);
    CHECK(m.condition(0).user_ack == false);
}

TEST_CASE("alarm: forcing a condition goes through the source, and releasing gives it back") {
    AlarmBank f(TABLE, 2);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.rows);
    m.setup(0);
    step_bits(f.bank, f.rows, m, 0x0, 1000);
    bool held = false;
    CHECK(m.forced(0, &held) == false);

    CHECK(m.force_toggle(0) == true);               // live OFF -> held ON
    m.step(2000);
    CHECK(m.forced(0, &held) == true);
    CHECK(held == true);
    CHECK(m.condition(0).active == true);

    CHECK(m.force_toggle(0) == true);               // held ON -> held OFF
    m.step(3000);
    CHECK(m.forced(0, &held) == true);
    CHECK(held == false);
    CHECK(m.condition(0).active == false);

    CHECK(m.force_clear(0) == true);                // released: the live bit again
    CHECK(m.forced(0) == false);
    CHECK(m.force_clear(0) == false);               // nothing left to release
}

TEST_CASE("alarm: an unbound model forces nothing") {
    Alarms::AlarmController m(Botanik::SetupContext(), nullptr);
    m.setup(0);
    CHECK(m.force_toggle(0) == false);
    CHECK(m.force_clear(0)  == false);
    CHECK(m.force_save(0)   == false);
}

TEST_CASE("alarm: ack_all acknowledges every active condition and counts them") {
    AlarmBank f(TABLE, 2);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.rows);
    m.setup(0);   // the scan is on_setup(): the rows exist now
    step_bits(f.bank, f.rows, m, 0x3, 1000);                              // both active
    CHECK(m.ack_all() == 2);
    CHECK(m.condition(0).user_ack == true);
    CHECK(m.condition(1).user_ack == true);

    AlarmBank f2(TABLE, 2);
    Alarms::AlarmController m2(Botanik::SetupContext(), &f2.rows);
    m2.setup(0);   // the scan is on_setup(): the rows exist now
    step_bits(f2.bank, f2.rows, m2, 0x2, 1000);                             // only B active
    CHECK(m2.ack_all() == 1);
    CHECK(m2.condition(1).user_ack == true);
}

TEST_CASE("alarm: releasing an override clears a condition it was holding on") {
    // `override x off`: the row snaps back to the live (unwritten) value, false.
    AlarmBank f(NORMAL_TABLE, 1);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.rows);
    m.setup(0);   // the scan is on_setup(): the rows exist now
    Vars::Var v = f.bank.var(f.rows.row(0));

    m.step(0);
    v.override_set(Vars::from_bool(Vars::T_BOOL, true));
    m.step(1000);
    REQUIRE(m.condition(0).active == true);

    v.override_clear();
    m.step(2000);
    CHECK(m.condition(0).active == false);
    CHECK(m.active_count() == 0);
}

TEST_CASE("alarm: a forced alarm comes down when the force does") {
    // A forced alarm must not inherit hold_s's 5-minute latch, or the next real
    // one is invisible under it.
    AlarmBank f(HOLD_TABLE, 1);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.rows);
    m.setup(0);   // the scan is on_setup(): the rows exist now
    Vars::Var v = f.bank.var(f.rows.row(0));

    m.step(0);
    v.override_set(Vars::from_bool(Vars::T_BOOL, true));
    m.step(1000);
    REQUIRE(m.condition(0).active == true);

    v.override_set(Vars::from_bool(Vars::T_BOOL, false));
    m.step(2000);
    INFO("the force is authoritative: the hold does not outlive it");
    CHECK(m.condition(0).active == false);
    CHECK(m.active_count() == 0);
}

TEST_CASE("alarm: hold_s still latches a condition nobody is forcing") {
    // hold_s must still latch a real signal that clears immediately.
    AlarmBank f(HOLD_TABLE, 1);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.rows);
    m.setup(0);   // the scan is on_setup(): the rows exist now

    step_bits(f.bank, f.rows, m, 0x1, 1000);      // a real assertion, not an override
    REQUIRE(m.condition(0).active == true);

    step_bits(f.bank, f.rows, m, 0x0, 2000);      // and it clears at once
    INFO("held for the declared 300 s, so somebody gets to see it");
    CHECK(m.condition(0).active == true);

    step_bits(f.bank, f.rows, m, 0x0, 200000);
    CHECK(m.condition(0).active == true);

    step_bits(f.bank, f.rows, m, 0x0, 310000);
    CHECK(m.condition(0).active == false);
}

TEST_CASE("alarm: the hold latches a blip, and the dwell still refuses to announce it") {
    // The two policies pull opposite ways and both have to hold: hold_s keeps a
    // one-beat condition VISIBLE for five minutes, and on_s says five minutes
    // of latch is not five seconds of signal. Measured on the episode instead,
    // the dwell would be unreachable on any row declaring both - which is
    // thirteen of this firmware's - and every blip would annunciate in full.
    Logs::Logger logger;
    TopicRecorder rec;
    logger.add_writer(&rec);
    AlarmBank f(HOLD_TABLE, 1);
    Alarms::AlarmController m(Botanik::SetupContext(nullptr, &logger), &f.rows);
    m.setup(0);

    step_bits(f.bank, f.rows, m, 0x1, 1000);      // one activation, then gone
    step_bits(f.bank, f.rows, m, 0x0, 2000);
    step_bits(f.bank, f.rows, m, 0x0, 30000);
    logger.refill();
    INFO("latched, so somebody gets to see it");
    CHECK(m.condition(0).active == true);
    INFO("...and unannounced, because the SIGNAL never lasted its dwell");
    CHECK_FALSE(rec.has(Logs::ALARM_BEG));
}

TEST_CASE("alarm: an END with no matching START is logged as a FLAP") {
    Logs::Logger logger;
    TopicRecorder rec;
    logger.add_writer(&rec);
    AlarmBank f(FLAP_TABLE, 1);
    Alarms::AlarmController m(Botanik::SetupContext(nullptr, &logger), &f.rows);
    m.setup(0);   // the scan is on_setup(): the rows exist now

    // A blip shorter than the 2 s START dwell: clears before any START is logged.
    step_bits(f.bank, f.rows, m, 0x1, 1000);                             // on (dwell 0 < on_s 2 s)
    logger.refill();
    CHECK(rec.has(Logs::ALARM_BEG) == false);   // too brief to announce a start
    step_bits(f.bank, f.rows, m, 0x0, 1500);                             // off -> END with no START = FLAP
    logger.refill();
    CHECK(rec.has(Logs::ALARM_FLAP) == true);
    CHECK(rec.has(Logs::ALARM_END) == false);
}

TEST_CASE("alarm: a normal episode logs START then END (not FLAP)") {
    Logs::Logger logger;
    TopicRecorder rec;
    logger.add_writer(&rec);
    AlarmBank f(NORMAL_TABLE, 1);
    Alarms::AlarmController m(Botanik::SetupContext(nullptr, &logger), &f.rows);
    m.setup(0);   // the scan is on_setup(): the rows exist now

    step_bits(f.bank, f.rows, m, 0x1, 1000);                             // on, START logged immediately
    logger.refill();
    CHECK(rec.has(Logs::ALARM_BEG) == true);
    step_bits(f.bank, f.rows, m, 0x0, 2000);                             // off -> END (START was logged)
    logger.refill();
    CHECK(rec.has(Logs::ALARM_END) == true);
    CHECK(rec.has(Logs::ALARM_FLAP) == false);
}

TEST_CASE("alarm reader: model reads each condition's variable") {
    // The row IS the condition: condition i reads variable i, nothing to sync.
    const Vars::Descriptor VARS[] = {
        Vars::A("alarm_a", "AAA", -1, -1, 0, 0, 805, "a", ""),
        Vars::A("alarm_b", "BBB", -1, -1, 0, 0, 806, "b", ""),
        Vars::A("alarm_c", "CCC", -1, -1, 0, 0, 807, "c", ""),
    };
    AlarmBank f(VARS, 3);
    Vars::Bank& io = f.bank;

    Logs::Logger logger;
    Alarms::AlarmController m(Botanik::SetupContext(nullptr, &logger), &f.rows);
    m.setup(0);   // the scan is on_setup(): the rows exist now

    m.step(1000);
    CHECK(m.condition(0).active == false);
    io.var((uint16_t)0).set(true);
    m.step(2000);
    CHECK(m.condition(0).active == true);
    CHECK(m.condition(2).active == false);
    io.var((uint16_t)2).set(true);
    m.step(3000);
    CHECK(m.condition(0).active == true);
    CHECK(m.condition(2).active == true);
    io.var((uint16_t)0).set(false);
    m.step(4000);
    CHECK(m.condition(0).active == false);
    CHECK(m.condition(2).active == true);
}

}  // namespace alarms

// alarms_render

namespace alarms_render {

using namespace Botanik;
using Command = Terminal::InputEvent::Command;

namespace {
    // As above: bit i drives condition i's variable, then step.
    void step_bits(Vars::Bank& io, Vars::BankAlarms& rows, Alarms::AlarmController& m,
                   uint64_t bits, uint64_t now) {
        for (uint8_t i = 0; i < m.count(); i++)
            io.var(rows.row(i)).set(((bits >> i) & 1u) != 0);
        m.step(now);
    }

    // Four always-recent alarms (recent_s<0 never expires), logging/notify off.
    const Vars::Descriptor TABLE[] = {
        Vars::A("aaa", "AAA", -1, -1, -1, 0, 800, "alarm A", ""),
        Vars::A("bbb", "BBB", -1, -1, -1, 0, 801, "alarm B", ""),
        Vars::A("ccc", "CCC", -1, -1, -1, 0, 802, "alarm C", ""),
        Vars::A("ddd", "DDD", -1, -1, -1, 0, 803, "alarm D", ""),
    };

    // AnsiTerminal + AlarmController over a pipe; drains TX into a captured string.
    struct RenderFixture {
        botanik_test::PipeSerial wire;
        uint64_t               now = 0;
        Terminal::Console      console;
        Vars::Bank             bank;
        /** @brief The K_ALARM rows as an alarm source, bound after the table
         *  below. NOT `rows` - the ctor's screen-height parameter is. */
        Vars::BankAlarms       alarm_rows;
        Alarms::AlarmController     model;
        Pages::AlarmsPage      page;

        // @p rows: a short screen overflows the 4-alarm set so UP/DOWN actually scroll.
        explicit RenderFixture(uint16_t rows = 24)
            // Very high baud so the cooperative TX floor never throttles a paint.
            : console(Botanik::SetupContext(), wire.serial, &now),
              model(Botanik::SetupContext(), &alarm_rows),
              page(console, &model) {
            // Geometry and rate are the bank's job in production; a fixture
            // with no bank sets them through the same path.
            console.reconfigure(Terminal::ConsoleConfig().type, 80, rows);
            console.terminal().set_baud(4000000u);
            TestSite::build(bank, TABLE, 4);
            // The scan is on_setup() now, and the source had nothing to offer
            // before the table above - so both run after it, in this order.
            alarm_rows.bind(&bank);
            model.setup(0);
        }

        // Drain TX to the pipe and return whatever has accumulated since last call.
        std::string capture() {
            std::string out;
            char buf[8192];
            for (int i = 0; i < 64; i++) wire.serial.transmit();
            ssize_t n;
            while ((n = ::read(wire.fds[0], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
            return out;
        }
        // Pump ticks until a paint settles: draw into FRONT, present() diffs
        // to the wire, capture() reads it.
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

// Focus paints structural rows once; an unchanged repaint skips them all but
// the footer.
TEST_CASE("alarms render: unchanged repaint skips the structural rows") {
    RenderFixture f;
    f.page.focus();
    std::string first = f.paint();
    CHECK(first.size() > 0);                  // structural rows drawn once
    CHECK(first.find("VARIABLE") != std::string::npos);   // header was in the first paint

    f.page.handle_command(Command::UP, 1);    // request repaint (already at row 0)
    std::string second = f.paint();
    CHECK(second.size() < first.size() / 2);  // only the footer, not the table
    CHECK(second.find("VARIABLE") == std::string::npos);  // header skipped
    CHECK(second.find("----") == std::string::npos);  // rules skipped
}

// Raise all four alarms so the table has data rows, then drive changes.
static void raise_all(RenderFixture& f) {
    step_bits(f.bank, f.alarm_rows, f.model, 0xF, 1000);     // bits 0..3 active
    f.page.focus();
    f.paint();                   // initial full paint; discard
}

// Acking flips one row's badge; only the moved cells (badge+timestamp) reach
// the wire.
TEST_CASE("alarms render: a same-alarm state change patches its cells, skips the rest") {
    RenderFixture f;
    step_bits(f.bank, f.alarm_rows, f.model, 0x3, 1000);                     // AAA + BBB active; row0=AAA (sel), row1=BBB
    f.page.focus();
    f.paint();                                   // initial full paint; discard

    CHECK(f.model.ack("bbb") == true);           // BBB (row1) active+unack -> active+ack
    f.page.handle_command(Command::UP, 1);       // repaint, selection still at row0
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
    f.page.handle_command(Command::UP, 1);
    std::string out = f.paint();
    CHECK(out.find("CCC") == std::string::npos);   // no data row re-emitted
    CHECK(out.find("----") == std::string::npos);  // no rule re-emitted
}

// A newly-raised alarm reorders the list, so the diff resends the whole row.
TEST_CASE("alarms render: identity change redraws the row in full") {
    RenderFixture f;
    step_bits(f.bank, f.alarm_rows, f.model, 0x1, 1000);             // only AAA
    f.page.focus();
    f.paint();

    step_bits(f.bank, f.alarm_rows, f.model, 0x3, 2000);             // BBB now active too -> sorts in, reorders
    f.page.handle_command(Command::UP, 1);
    std::string out = f.paint();
    CHECK(out.find("BBB") != std::string::npos);   // a full row carried the new CODE
}

// The cursor sits on the VARIABLE cell; moving selection (no scroll) touches
// only the old/new row's cells - highlight costs cells, not full rows.
TEST_CASE("alarms render: a selection move repaints both VARIABLE cells, nothing else") {
    RenderFixture f;                             // 24 rows: all four fit, so no scroll
    step_bits(f.bank, f.alarm_rows, f.model, 0xF, 1000);                     // aaa..ddd active; row0=aaa selected
    f.page.focus();
    f.paint();                                   // initial paint; discard

    f.page.handle_command(Command::DOWN, 1);     // aaa(row0) -> bbb(row1), stays in window
    std::string out = f.paint();

    CHECK(out.find("aaa")     != std::string::npos);  // old row's VARIABLE repainted (plain)
    CHECK(out.find("bbb")     != std::string::npos);  // new row's VARIABLE repainted (inverted)
    CHECK(out.find("AAA")     == std::string::npos);  // CODE cells untouched (cell patch)
    CHECK(out.find("BBB")     == std::string::npos);
    CHECK(out.find("ccc")     == std::string::npos);  // no other row repainted
    CHECK(out.find("ddd")     == std::string::npos);
    CHECK(out.find("CODE")    == std::string::npos);  // header/rules not re-emitted
    CHECK(out.find("----")    == std::string::npos);
}

// DOWN moves the cursor, not the window. A 13-row screen (window of 2)
// overflows the 4 alarms; scrolling past the bottom brings CCC into view.
TEST_CASE("alarms render: DOWN moves the selection and scrolls it into view") {
    RenderFixture f(13);
    step_bits(f.bank, f.alarm_rows, f.model, 0xF, 1000);
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

// The badge must reach the wire before the table rewrite it triggers; the
// legend row must survive the tick it's raised in, or a naive "reconciled?"
// check demotes it before it is ever drawn.
TEST_CASE("alarms render: a mode change shows the new mode before the rows") {
    RenderFixture f(13);                          // short screen: rows overflow
    step_bits(f.bank, f.alarm_rows, f.model, 0xF, 1000);
    f.page.focus();
    f.paint();                                    // settle the initial paint

    f.page.handle_command(Command::RIGHT, 1);     // recent -> all
    // One tick + one present: the badge should already be on the wire.
    f.page.tick(true);
    f.console.terminal().present();
    std::string first = f.capture();

    CHECK(first.find("all") != std::string::npos);   // the new mode badge
    CHECK(first.find("AAA") == std::string::npos);   // ...ahead of any data row
    CHECK(first.find("DDD") == std::string::npos);
    // Just the legend, not a table's worth of bytes.
    CHECK(first.size() < 150);      // one row, not a table
    // And the page settles rather than churning on the raised row.
    f.paint();
    CHECK(f.paint().empty());
}

// The filter is the base's, so this covers every table's row filter; what is
// per-page is only which cell it matches against.
TEST_CASE("alarms render: a typed filter keeps the matching rows, ESC gives the rest back") {
    RenderFixture f;
    raise_all(f);

    f.page.handle_text('b');
    std::string out = f.paint();
    CHECK(out.find("[/b]") != std::string::npos);   // the badge carries what was typed
    CHECK(out.find("bbb")  != std::string::npos);   // the one match is drawn
    CHECK(out.find("aaa")  == std::string::npos);   // and the rest are gone
    CHECK(out.find("ddd")  == std::string::npos);

    f.page.handle_command(Command::ESC, 1);
    std::string back = f.paint();
    CHECK(back.find("aaa") != std::string::npos);   // cleared: the whole set again
    CHECK(back.find("ddd") != std::string::npos);
}

// 'a' used to ack; it is a filter character now, so the ack moved to a key that
// cannot be typed.
TEST_CASE("alarms render: ENTER acknowledges the selected condition") {
    RenderFixture f;
    raise_all(f);
    REQUIRE(f.model.condition(0).active);
    REQUIRE(f.model.condition(0).user_ack == false);

    f.page.handle_command(Command::ENTER, 1);
    CHECK(f.model.condition(0).user_ack == true);
    CHECK(f.model.condition(1).user_ack == false);   // only the selected one
}

TEST_CASE("alarms render: the ACK column says which conditions were acknowledged") {
    RenderFixture f;
    step_bits(f.bank, f.alarm_rows, f.model, 0xF, 1000);
    f.page.focus();
    std::string first = f.paint();
    CHECK(first.find("ACK") != std::string::npos);       // the column is there
    CHECK(first.find("yes") == std::string::npos);       // nothing ack'd yet

    f.page.handle_command(Command::ENTER, 1);            // ack the selected row
    REQUIRE(f.model.condition(0).user_ack == true);
    CHECK(f.paint().find("yes") != std::string::npos);   // and its cell fills in
}

TEST_CASE("alarms render: ^O flips the selected condition's override, ^R releases it") {
    RenderFixture f;
    raise_all(f);                                          // all four ON; row 0 = AAA
    REQUIRE(f.model.forced(0) == false);

    bool held = false;
    f.page.handle_command(Command::CTRL_O, 1);             // live ON -> held OFF
    CHECK(f.model.forced(0, &held) == true);
    CHECK(held == false);

    f.page.handle_command(Command::CTRL_O, 1);             // held OFF -> held ON
    CHECK(f.model.forced(0, &held) == true);
    CHECK(held == true);

    f.page.handle_command(Command::CTRL_R, 1);             // released
    CHECK(f.model.forced(0) == false);
    f.model.step(2000);
    CHECK(f.model.condition(0).active == true);            // the live bit, back
}

TEST_CASE("alarms render: ^S on a row with nothing forced says so rather than saving") {
    RenderFixture f;
    raise_all(f);
    f.page.handle_command(Command::CTRL_S, 1);
    // The terminal diff skips unchanged cells, so the words arrive separated by
    // cursor moves: match one of them, not the sentence.
    CHECK(f.paint().find("nothing") != std::string::npos);
}

}  // namespace alarms_render

// notifier

namespace notifier {

// The #if guards below need BOTANIK_FEATURE_* defined (undefined reads as 0).


using namespace Botanik;
using namespace Botanik::Alarms;

namespace {
    void step_bits(Vars::Bank& io, Vars::BankAlarms& rows, AlarmController& m,
                   uint64_t bits, uint64_t now) {
        for (uint8_t i = 0; i < m.count(); i++)
            io.var(rows.row(i)).set(((bits >> i) & 1u) != 0);
        m.step(now);
    }

    struct MockPublisher : Publisher {
        int  calls = 0;
        char topic[128] = {0};
        char payload[16] = {0};
        bool publish(const char* t, const char* p) override {
            calls++;
            std::strncpy(topic, t, sizeof(topic) - 1);
            std::strncpy(payload, p, sizeof(payload) - 1);
            return true;
        }
    };
}

// Only MqttAlarmNotifier needs BOTANIK_FEATURE_ALARMS_PUBLISH (the fan-out
// case below uses a stub notifier).
#if BOTANIK_FEATURE_ALARMS_PUBLISH
TEST_CASE("MqttAlarmNotifier: notify_on/off build the topic + payload and forward to the publisher") {
    MockPublisher pub;
    MqttAlarmNotifier n("botanik/greenhouse");
    n.set_publisher(&pub);

    // The condition borrows its metadata from the source, so this needs a real
    // one - and a notifier has no business knowing which kind it is, so the
    // table-free source is what stands in here.
    static const bool RAW = true;
    static const Alarms::StaticAlarm ONE = {
        "NET_FLT", "", "", { "NET_FLT", 0, 0, 0, 0 }, &RAW
    };
    static const Alarms::StaticAlarmTable TAB(&ONE, 1);
    AlarmCondition c = {};
    c.src = &TAB;
    c.idx = 0;

    n.notify_on(c);
    CHECK(pub.calls == 1);
    CHECK(std::string(pub.topic)   == "botanik/greenhouse/alarms/NET_FLT");
    CHECK(std::string(pub.payload) == "ON");

    n.notify_off(c);
    CHECK(pub.calls == 2);
    CHECK(std::string(pub.payload) == "OFF");

    n.notify_flap(c);   // an OFF with no matching ON this episode
    CHECK(pub.calls == 3);
    CHECK(std::string(pub.topic)   == "botanik/greenhouse/alarms/NET_FLT");
    CHECK(std::string(pub.payload) == "FLAP");
}
#endif

namespace {
    // Counts the transitions the model fans out to it.
    struct CountingNotifier : AlarmNotifier {
        int on = 0, off = 0, flap = 0;
        void notify_on(const AlarmCondition&)   override { on++; }
        void notify_off(const AlarmCondition&)  override { off++; }
        void notify_flap(const AlarmCondition&) override { flap++; }
    };
}

TEST_CASE("AlarmController: a transition fans out to every registered notifier") {
    // announce ON/OFF at once (on_s = off_s = 0).
    const Vars::Descriptor TABLE[] = { Vars::A("x", "X", 0, 0, 0, 0, 800, "", "") };
    Vars::Bank bank;
    TestSite::build(bank, TABLE, 1);
    Vars::BankAlarms rows;
    rows.bind(&bank);
    AlarmController m(Botanik::SetupContext(), &rows);
    m.setup(0);   // the scan is on_setup(): the rows exist now
    CountingNotifier a, b;
    CHECK(m.add_notifier(&a));
    CHECK(m.add_notifier(&b));
    CHECK(m.has_notifiers());
    CHECK(m.add_notifier(&a) == false);   // no double-register

    step_bits(bank, rows, m, 0x1, 1000);                    // ON -> both notified
    CHECK(a.on == 1);
    CHECK(b.on == 1);
    step_bits(bank, rows, m, 0x0, 2000);                    // clean OFF -> both notified
    CHECK(a.off == 1);
    CHECK(b.off == 1);

    // Drop one; the remaining notifier still fires, the removed one does not.
    CHECK(m.remove_notifier(&a));
    step_bits(bank, rows, m, 0x1, 3000);
    CHECK(a.on == 1);                     // unchanged after removal
    CHECK(b.on == 2);
}

}  // namespace notifier

namespace static_source {

using namespace Botanik;

TEST_CASE("alarm: a StaticAlarmTable is a source like any other") {
    // What a build with no variable table runs on: the rows are plain structs
    // whose `raw` points at bools somebody else owns, and the controller reads
    // them through the same AlarmSourceTable interface a bank presents.
    bool state[3] = { false, false, false };
    Alarms::StaticAlarm rows[3] = {
        { "alarm_one",   "the first",  "", { "ONE",   0, 0, 0, 0 }, &state[0] },
        { "alarm_two",   "the second", "", { "TWO",   0, 0, 0, 0 }, &state[1] },
        // A null `raw` is a row nobody publishes, and must read false rather
        // than dereference.
        { "alarm_three", "the third",  "", { "THREE", 0, 0, 0, 0 }, nullptr   },
    };
    Alarms::StaticAlarmTable table(rows, 3);
    Alarms::AlarmController m(Botanik::SetupContext(), &table);
    m.setup(0);

    REQUIRE(m.count() == 3);
    CHECK(m.active_count() == 0);

    state[1] = true;
    m.step(1000, Botanik::Tasks::PHASE_SERVICE);
    CHECK(m.active_count() == 1);
    CHECK(m.condition(1).active);
    CHECK(m.index_of("TWO") == 1);

    state[1] = false;
    m.step(2000, Botanik::Tasks::PHASE_SERVICE);
    CHECK(m.active_count() == 0);

    // The unpublished row never raises, whatever happens around it.
    state[0] = true;
    m.step(3000, Botanik::Tasks::PHASE_SERVICE);
    CHECK(m.condition(2).active == false);
}

TEST_CASE("alarm: a default StaticAlarmTable declares nothing") {
    // The shape HealthMonitor's no-VARS members had before they were filled:
    // a table over no rows. It must be inert rather than a null dereference.
    Alarms::StaticAlarmTable empty;
    Alarms::AlarmController m(Botanik::SetupContext(), &empty);
    m.setup(0);
    CHECK(m.count() == 0);
    m.step(1000, Botanik::Tasks::PHASE_SERVICE);
    CHECK(m.active_count() == 0);
}

}  // namespace static_source

namespace slot_overflow {

using namespace Botanik;

TEST_CASE("alarm: a source declaring more than the slots hold loses the tail safely") {
    // AlarmSourceTable's contract: count() may EXCEED the controller's slots,
    // and the controller is what reports the loss. So a name past the last
    // slot resolves in the table and has no condition to act on - index_of()
    // must say so rather than handing ack() an index past _conditions[].
    const uint16_t n = (uint16_t)(Config::ALARM_SLOTS + 3);
    std::vector<bool>                state((size_t)n, false);
    std::vector<Alarms::StaticAlarm> rows((size_t)n);
    std::vector<std::string>         names((size_t)n), codes((size_t)n);
    for (uint16_t i = 0; i < n; i++) {
        names[i] = "alarm_" + std::to_string(i);
        codes[i] = "C" + std::to_string(i);
        // vector<bool> is a bitfield, so the raw pointers come from a plain array.
        rows[i].name  = names[i].c_str();
        rows[i].desc  = "";
        rows[i].notes = "";
        rows[i].spec  = Alarms::AlarmSpec{ codes[i].c_str(), 0, 0, 0, 0 };
        rows[i].raw   = nullptr;
    }
    Alarms::StaticAlarmTable table(rows.data(), n);
    Alarms::AlarmController m(Botanik::SetupContext(), &table);
    m.setup(0);

    INFO("the loss is visible, which is what the POST reports on");
    CHECK(m.declared_count() == n);
    CHECK(m.count() == Config::ALARM_SLOTS);
    CHECK(m.declared_count() > m.count());

    // A row that got a slot resolves; one past the end does not, and neither
    // ack nor unack may reach past the array.
    CHECK(m.index_of(codes[0].c_str()) == 0);
    CHECK(m.index_of(names[n - 1].c_str()) < 0);
    CHECK(m.ack(names[n - 1].c_str()) == false);
    CHECK(m.unack(names[n - 1].c_str()) == false);
}

}  // namespace slot_overflow
