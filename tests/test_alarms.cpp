/**
 * @file test_alarms.cpp
 * @brief Alarm conditions, their rendering, and the MQTT notifier.
 *
 * Each section below is the former test_<name>.cpp, kept in its own
 * namespace so their fixtures and helpers cannot collide.
 */

#include <cstddef>                      // size_t
#include <vector>                       // std::vector
#include "libbotanik/alarms/alarms.h"   // Alarms::AlarmController
#include "libbotanik/logs/logger.h"     // Logs::Logger
#include "libbotanik/vars/declare.h"    // the A() factory
#include "doctest.h"
#include <cstdint>                          // uint8_t/uint16_t/uint64_t
#include <fcntl.h>                          // fcntl / O_NONBLOCK
#include <string>                           // std::string
#include <unistd.h>                         // pipe / read / close
#include "libbotanik/console/console.h"     // Console
#include "libbotanik/console/terminal.h"    // AnsiTerminal::present
#include "libbotanik/pages/alarms.h"        // Pages::AlarmsPage
#include <cstring>                          // strcmp / strstr
#include "libbotanik_config.h"              // Config::MQTT_TOPIC_PREFIX
#include "libbotanik/alarms/publisher.h"    // Alarms::Publisher

// --------------------------------------------------------------------------
// alarms
// --------------------------------------------------------------------------

namespace alarms {

using namespace Botanik;

namespace {
    // Drive the model the way the firmware does: write each condition's own
    // variable (bit i -> condition i), then step. There is no other way in now -
    // an alarm IS its row.
    void step_bits(Vars::Bank& io, Alarms::AlarmController& m, uint64_t bits, uint64_t now) {
        for (uint8_t i = 0; i < m.count(); i++)
            io.var(m.condition(i).ref).set(((bits >> i) & 1u) != 0);
        m.step(now);
    }

    // An alarm is a SCOPE_A row in a variable table now, so these read exactly
    // the way a site declares one: A(name, code, on_s, off_s, recent_s,
    // hold_on_s, min_on_s, desc, notes). The model finds them by scanning.

    // Two unmasked, unlatched conditions (announce disabled) at bits 0 and 1.
    const Vars::Variable TABLE[] = {
        Vars::A("aaa", "AAA", -1, -1, 0, 0, 0, 800, "alarm A", ""),
        Vars::A("bbb", "BBB", -1, -1, 0, 0, 0, 801, "alarm B", ""),
    };
    // One condition with a 5 s minimum-ON hold (announce disabled).
    // hold_on_s = 300, exactly like the platform's STACK / CPU_LOAD / CPU_JITT.
    const Vars::Variable HOLD_ON_TABLE[] = {
        Vars::A("hld", "HLD", 5, 5, 0, 300, 0, 805, "hold on", ""),
    };

    const Vars::Variable MIN_ON_TABLE[] = {
        Vars::A("min", "MIN", -1, -1, 0, 0, 5, 802, "min on", ""),
    };
    // One condition that announces a START only after a 2 s dwell, and an END as
    // soon as it clears (off_s 0). A brief blip clears before the START dwell -> FLAP.
    const Vars::Variable FLAP_TABLE[] = {
        Vars::A("flp", "FLP", 2, 0, 0, 0, 0, 803, "flap", ""),
    };
    // on_s 0 so the START is announced at once.
    const Vars::Variable NORMAL_TABLE[] = {
        Vars::A("nrm", "NRM", 0, 0, 0, 0, 0, 804, "normal", ""),
    };

    /** @brief A bank over one of the tables above - what the model scans. */
    struct AlarmBank {
        Vars::Value      vals[4];
        Vars::Bank       bank;
        AlarmBank(const Vars::Variable* v, uint16_t n) : vals(), bank(make(v, vals, n)) {
            bank.init();
        }
        static Vars::VarTables make(const Vars::Variable* v, Vars::Value* val, uint16_t n) {
            Vars::VarTables t = {};
            t.var = v; t.val = val; t.count = n;
            return t;
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
    Alarms::AlarmController m(&f.bank, nullptr);
    step_bits(f.bank, m, 0x1, 1000);                              // raise A (bit 0)
    CHECK(m.active_count() == 1);
    CHECK(m.condition(0).user_ack == false);
    CHECK(m.ack("aaa") == true);
    CHECK(m.condition(0).user_ack == true);
    CHECK(m.ack("bbb") == false);                   // B is not active
    CHECK(m.ack("zzz") == false);                   // unknown code
    // Both spellings are on screen - the alarm bar and this command's own help
    // show the CODE, `alarms list` and the variable table show the NAME - so
    // both have to resolve, or the one the operator is looking at is the one
    // that does not work.
    CHECK(m.ack("AAA") == true);
    CHECK(m.index_of("BBB") == m.index_of("bbb"));
}

TEST_CASE("alarm: ack clears on the next ON edge (a fresh episode is unacknowledged)") {
    AlarmBank f(TABLE, 2);
    Alarms::AlarmController m(&f.bank, nullptr);
    step_bits(f.bank, m, 0x1, 1000);
    m.ack("aaa");
    CHECK(m.condition(0).user_ack == true);
    step_bits(f.bank, m, 0x0, 2000);                              // A clears
    step_bits(f.bank, m, 0x1, 3000);                              // A re-fires
    CHECK(m.condition(0).user_ack == false);    // reset on the ON edge
}

TEST_CASE("alarm: ack_all acknowledges every active condition and counts them") {
    AlarmBank f(TABLE, 2);
    Alarms::AlarmController m(&f.bank, nullptr);
    step_bits(f.bank, m, 0x3, 1000);                              // both active
    CHECK(m.ack_all() == 2);
    CHECK(m.condition(0).user_ack == true);
    CHECK(m.condition(1).user_ack == true);

    AlarmBank f2(TABLE, 2);
    Alarms::AlarmController m2(&f2.bank, nullptr);
    step_bits(f2.bank, m2, 0x2, 1000);                             // only B active
    CHECK(m2.ack_all() == 1);
    CHECK(m2.condition(1).user_ack == true);
}

TEST_CASE("alarm: min_on_s holds a condition active after the raw bit clears") {
    AlarmBank f(MIN_ON_TABLE, 1);
    Alarms::AlarmController m(&f.bank, nullptr);
    step_bits(f.bank, m, 0x1, 1000);                              // raise (seeds the 5 s min-on hold)
    CHECK(m.condition(0).active == true);
    step_bits(f.bank, m, 0x0, 2000);                              // raw clears 1 s in: still held
    CHECK(m.condition(0).active == true);
    step_bits(f.bank, m, 0x0, 5500);                              // 4.5 s in: still within the 5 s hold
    CHECK(m.condition(0).active == true);
    step_bits(f.bank, m, 0x0, 6500);                              // 5.5 s in: hold elapsed, now clears
    CHECK(m.condition(0).active == false);
}

TEST_CASE("alarm: forcing a condition on and then off clears it") {
    // How an operator actually exercises an alarm: override the row true, watch
    // it raise, override it back to false. It has to clear - a forced alarm that
    // will not come down is worse than one that never went up, because the next
    // real one is now invisible under it.
    AlarmBank f(MIN_ON_TABLE, 1);           // min_on_s = 5, the sticky case
    Alarms::AlarmController m(&f.bank, nullptr);
    Vars::Var v = f.bank.var(m.condition(0).ref);

    m.step(0);
    f.bank.override_set(v, Vars::from_bool(Vars::T_BOOL, true), true);
    m.step(1000);
    REQUIRE(m.condition(0).active == true);

    // Forced back to false, still overridden - which is what `override x 0` does.
    f.bank.override_set(v, Vars::from_bool(Vars::T_BOOL, false), true);
    m.step(2000);
    INFO("the minimum-on hold does not outlive a force either");
    CHECK(m.condition(0).active == false);
    CHECK(m.active_count() == 0);
}

TEST_CASE("alarm: releasing an override clears a condition it was holding on") {
    // `override x off` rather than `override x 0`: the row snaps back to the
    // live value underneath, which nothing has written, so it reads false.
    AlarmBank f(NORMAL_TABLE, 1);
    Alarms::AlarmController m(&f.bank, nullptr);
    Vars::Var v = f.bank.var(m.condition(0).ref);

    m.step(0);
    f.bank.override_set(v, Vars::from_bool(Vars::T_BOOL, true), true);
    m.step(1000);
    REQUIRE(m.condition(0).active == true);

    f.bank.override_clear(v);
    m.step(2000);
    CHECK(m.condition(0).active == false);
    CHECK(m.active_count() == 0);
}

TEST_CASE("alarm: a forced alarm comes down when the force does") {
    // The platform alarms (STACK, CPU_LOAD, CPU_JITT) carry hold_on_s = 300, so
    // a load spike stays annunciated long enough for somebody to see it. That
    // same latch applies when an OPERATOR forces the row: override it on, take
    // it back off, and it holds for five more minutes with nothing asserting it.
    //
    // That was exactly "I forced alarm_stack and it will not come down". A
    // forced row follows the force now: the latches smooth a real signal that
    // flaps, and a hand-held row does not flap.
    AlarmBank f(HOLD_ON_TABLE, 1);
    Alarms::AlarmController m(&f.bank, nullptr);
    Vars::Var v = f.bank.var(m.condition(0).ref);

    m.step(0);
    f.bank.override_set(v, Vars::from_bool(Vars::T_BOOL, true), true);
    m.step(1000);
    REQUIRE(m.condition(0).active == true);

    f.bank.override_set(v, Vars::from_bool(Vars::T_BOOL, false), true);
    m.step(2000);
    INFO("the force is authoritative: the hold does not outlive it");
    CHECK(m.condition(0).active == false);
    CHECK(m.active_count() == 0);
}

TEST_CASE("alarm: hold_on_s still latches a condition nobody is forcing") {
    // The fix above must not cost the latch its actual job - a real signal that
    // clears immediately still stays annunciated, which is what hold_on_s is for.
    AlarmBank f(HOLD_ON_TABLE, 1);
    Alarms::AlarmController m(&f.bank, nullptr);

    step_bits(f.bank, m, 0x1, 1000);      // a real assertion, not an override
    REQUIRE(m.condition(0).active == true);

    step_bits(f.bank, m, 0x0, 2000);      // and it clears at once
    INFO("held for the declared 300 s, so somebody gets to see it");
    CHECK(m.condition(0).active == true);

    step_bits(f.bank, m, 0x0, 200000);
    CHECK(m.condition(0).active == true);

    step_bits(f.bank, m, 0x0, 310000);
    CHECK(m.condition(0).active == false);
}

TEST_CASE("alarm: an END with no matching START is logged as a FLAP") {
    Logs::Logger logger;
    TopicRecorder rec;
    logger.add_writer(&rec);
    AlarmBank f(FLAP_TABLE, 1);
    Alarms::AlarmController m(&f.bank, &logger);

    // A blip shorter than the 2 s START dwell: clears before any START is logged.
    step_bits(f.bank, m, 0x1, 1000);                             // on (dwell 0 < on_s 2 s)
    logger.refill();
    CHECK(rec.has(Logs::ALARM_BEG) == false);   // too brief to announce a start
    step_bits(f.bank, m, 0x0, 1500);                             // off -> END with no START = FLAP
    logger.refill();
    CHECK(rec.has(Logs::ALARM_FLAP) == true);
    CHECK(rec.has(Logs::ALARM_END) == false);
}

TEST_CASE("alarm: a normal episode logs START then END (not FLAP)") {
    Logs::Logger logger;
    TopicRecorder rec;
    logger.add_writer(&rec);
    AlarmBank f(NORMAL_TABLE, 1);
    Alarms::AlarmController m(&f.bank, &logger);

    step_bits(f.bank, m, 0x1, 1000);                             // on, START logged immediately
    logger.refill();
    CHECK(rec.has(Logs::ALARM_BEG) == true);
    step_bits(f.bank, m, 0x0, 2000);                             // off -> END (START was logged)
    logger.refill();
    CHECK(rec.has(Logs::ALARM_END) == true);
    CHECK(rec.has(Logs::ALARM_FLAP) == false);
}

TEST_CASE("alarm reader: model reads each condition's variable") {
    // Three alarm rows. The row IS the condition, so there is no second table
    // and nothing to keep in step: condition i reads variable i.
    const Vars::Variable VARS[] = {
        Vars::A("alarm_a", "AAA", -1, -1, 0, 0, 0, 805, "a", ""),
        Vars::A("alarm_b", "BBB", -1, -1, 0, 0, 0, 806, "b", ""),
        Vars::A("alarm_c", "CCC", -1, -1, 0, 0, 0, 807, "c", ""),
    };
    AlarmBank f(VARS, 3);
    Vars::Bank& io = f.bank;

    Logs::Logger logger;
    Alarms::AlarmController m(&io, &logger);

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

// --------------------------------------------------------------------------
// alarms_render
// --------------------------------------------------------------------------

namespace alarms_render {

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
        Vars::A("aaa", "AAA", -1, -1, -1, 0, 0, 800, "alarm A", ""),
        Vars::A("bbb", "BBB", -1, -1, -1, 0, 0, 801, "alarm B", ""),
        Vars::A("ccc", "CCC", -1, -1, -1, 0, 0, 802, "alarm C", ""),
        Vars::A("ddd", "DDD", -1, -1, -1, 0, 0, 803, "alarm D", ""),
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

}  // namespace alarms_render

// --------------------------------------------------------------------------
// notifier
// --------------------------------------------------------------------------

namespace notifier {

// Pulls the site config, then the feature defaults: the guards below
// need the flags DEFINED, and an undefined one reads as 0 in #if.


using namespace Botanik;
using namespace Botanik::Alarms;

namespace {
    // Drive the model the way the firmware does: write each condition's own
    // variable (bit i -> condition i), then step. There is no other way in now -
    // an alarm IS its row.
    void step_bits(Vars::Bank& io, AlarmController& m, uint64_t bits, uint64_t now) {
        for (uint8_t i = 0; i < m.count(); i++)
            io.var(m.condition(i).ref).set(((bits >> i) & 1u) != 0);
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

// Gated: the AlarmController fan-out case below is NOT - it uses a stub
// notifier and tests the controller, which is always present.
#if BOTANIK_FEATURE_ALARMS_PUBLISH
TEST_CASE("MqttAlarmNotifier: notify_on/off build the topic + payload and forward to the publisher") {
    MockPublisher pub;
    MqttAlarmNotifier n("botanik/greenhouse");
    n.set_publisher(&pub);

    AlarmCondition c = {};
    c.name = "NET_FLT";

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

// The AlarmController fan-out case below is NOT gated: it uses a stub notifier
// and tests the controller, which is always present.

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
    const Vars::Variable TABLE[] = { Vars::A("x", "X", 0, 0, 0, 0, 0, 800, "", "") };
    Vars::Value vals[1]{};
    Vars::VarTables t = {};
    t.var = TABLE; t.val = vals; t.count = 1;
    Vars::Bank bank(t);
    bank.init();
    AlarmController m(&bank, nullptr);
    CountingNotifier a, b;
    CHECK(m.add_notifier(&a));
    CHECK(m.add_notifier(&b));
    CHECK(m.has_notifiers());
    CHECK(m.add_notifier(&a) == false);   // no double-register

    step_bits(bank, m, 0x1, 1000);                    // ON -> both notified
    CHECK(a.on == 1);
    CHECK(b.on == 1);
    step_bits(bank, m, 0x0, 2000);                    // clean OFF -> both notified
    CHECK(a.off == 1);
    CHECK(b.off == 1);

    // Drop one; the remaining notifier still fires, the removed one does not.
    CHECK(m.remove_notifier(&a));
    step_bits(bank, m, 0x1, 3000);
    CHECK(a.on == 1);                     // unchanged after removal
    CHECK(b.on == 2);
}

}  // namespace notifier
