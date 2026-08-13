/**
 * @file test_alarms.cpp
 * @brief Alarm conditions, their rendering, and the MQTT notifier.
 *
 * Each section is its own namespace, isolating fixtures and helpers.
 */

#include <cstddef>                      // size_t
#include <vector>                       // std::vector
#include "libbotanik/alarms/alarms.h"   // Alarms::AlarmController
#include "libbotanik/alarms/notifier.h" // Alarms::MqttAlarmNotifier
#include "libbotanik/logs/logger.h"     // Logs::Logger
#include "libbotanik/vars/declare.h"    // the A() factory
#include "doctest.h"
#include "term_fixture.h"              // botanik_test::PipeSerial
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
    // Sets condition i's variable to bit i, then steps: an alarm is its row.
    void step_bits(Vars::Bank& io, Alarms::AlarmController& m, uint64_t bits, uint64_t now) {
        for (uint8_t i = 0; i < m.count(); i++)
            io.var(m.condition(i).ref).set(((bits >> i) & 1u) != 0);
        m.step(now);
    }

    // A SCOPE_A row: A(name, code, on_s, off_s, recent_s, hold_on_s,
    // min_on_s, reg, desc, notes).

    // Two unmasked, unlatched conditions (announce disabled) at bits 0 and 1.
    const Vars::Variable TABLE[] = {
        Vars::A("aaa", "AAA", -1, -1, 0, 0, 0, 800, "alarm A", ""),
        Vars::A("bbb", "BBB", -1, -1, 0, 0, 0, 801, "alarm B", ""),
    };
    // hold_on_s=300 (announce disabled), like platform STACK/CPU_LOAD/CPU_JITT.
    const Vars::Variable HOLD_ON_TABLE[] = {
        Vars::A("hld", "HLD", 5, 5, 0, 300, 0, 805, "hold on", ""),
    };

    const Vars::Variable MIN_ON_TABLE[] = {
        Vars::A("min", "MIN", -1, -1, 0, 0, 5, 802, "min on", ""),
    };
    // START after a 2 s dwell, END at once (off_s 0); a shorter blip -> FLAP.
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
    Alarms::AlarmController m(Botanik::SetupContext(), &f.bank);
    step_bits(f.bank, m, 0x1, 1000);                              // raise A (bit 0)
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

TEST_CASE("alarm: ack clears on the next ON edge (a fresh episode is unacknowledged)") {
    AlarmBank f(TABLE, 2);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.bank);
    step_bits(f.bank, m, 0x1, 1000);
    m.ack("aaa");
    CHECK(m.condition(0).user_ack == true);
    step_bits(f.bank, m, 0x0, 2000);                              // A clears
    step_bits(f.bank, m, 0x1, 3000);                              // A re-fires
    CHECK(m.condition(0).user_ack == false);    // reset on the ON edge
}

TEST_CASE("alarm: ack_all acknowledges every active condition and counts them") {
    AlarmBank f(TABLE, 2);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.bank);
    step_bits(f.bank, m, 0x3, 1000);                              // both active
    CHECK(m.ack_all() == 2);
    CHECK(m.condition(0).user_ack == true);
    CHECK(m.condition(1).user_ack == true);

    AlarmBank f2(TABLE, 2);
    Alarms::AlarmController m2(Botanik::SetupContext(), &f2.bank);
    step_bits(f2.bank, m2, 0x2, 1000);                             // only B active
    CHECK(m2.ack_all() == 1);
    CHECK(m2.condition(1).user_ack == true);
}

TEST_CASE("alarm: min_on_s holds a condition active after the raw bit clears") {
    AlarmBank f(MIN_ON_TABLE, 1);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.bank);
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
    // A forced alarm must clear, or the next real one is invisible under it.
    AlarmBank f(MIN_ON_TABLE, 1);           // min_on_s = 5, the sticky case
    Alarms::AlarmController m(Botanik::SetupContext(), &f.bank);
    Vars::Var v = f.bank.var(m.condition(0).ref);

    m.step(0);
    f.bank.override_set(v, Vars::from_bool(Vars::T_BOOL, true), true);
    m.step(1000);
    REQUIRE(m.condition(0).active == true);

    // Forced false while still overridden, as `override x 0` does.
    f.bank.override_set(v, Vars::from_bool(Vars::T_BOOL, false), true);
    m.step(2000);
    INFO("the minimum-on hold does not outlive a force either");
    CHECK(m.condition(0).active == false);
    CHECK(m.active_count() == 0);
}

TEST_CASE("alarm: releasing an override clears a condition it was holding on") {
    // `override x off`: the row snaps back to the live (unwritten) value, false.
    AlarmBank f(NORMAL_TABLE, 1);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.bank);
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
    // A forced alarm must not inherit hold_on_s's 5-minute latch.
    AlarmBank f(HOLD_ON_TABLE, 1);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.bank);
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
    // hold_on_s must still latch a real signal that clears immediately.
    AlarmBank f(HOLD_ON_TABLE, 1);
    Alarms::AlarmController m(Botanik::SetupContext(), &f.bank);

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
    Alarms::AlarmController m(Botanik::SetupContext(nullptr, &logger), &f.bank);

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
    Alarms::AlarmController m(Botanik::SetupContext(nullptr, &logger), &f.bank);

    step_bits(f.bank, m, 0x1, 1000);                             // on, START logged immediately
    logger.refill();
    CHECK(rec.has(Logs::ALARM_BEG) == true);
    step_bits(f.bank, m, 0x0, 2000);                             // off -> END (START was logged)
    logger.refill();
    CHECK(rec.has(Logs::ALARM_END) == true);
    CHECK(rec.has(Logs::ALARM_FLAP) == false);
}

TEST_CASE("alarm reader: model reads each condition's variable") {
    // The row IS the condition: condition i reads variable i, nothing to sync.
    const Vars::Variable VARS[] = {
        Vars::A("alarm_a", "AAA", -1, -1, 0, 0, 0, 805, "a", ""),
        Vars::A("alarm_b", "BBB", -1, -1, 0, 0, 0, 806, "b", ""),
        Vars::A("alarm_c", "CCC", -1, -1, 0, 0, 0, 807, "c", ""),
    };
    AlarmBank f(VARS, 3);
    Vars::Bank& io = f.bank;

    Logs::Logger logger;
    Alarms::AlarmController m(Botanik::SetupContext(nullptr, &logger), &io);

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
    // As above: bit i drives condition i's variable, then step.
    void step_bits(Vars::Bank& io, Alarms::AlarmController& m, uint64_t bits, uint64_t now) {
        for (uint8_t i = 0; i < m.count(); i++)
            io.var(m.condition(i).ref).set(((bits >> i) & 1u) != 0);
        m.step(now);
    }

    // Four always-recent alarms (recent_s<0 never expires), logging/notify off.
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
        botanik_test::PipeSerial wire;
        uint64_t               now = 0;
        Terminal::Console      console;
        Vars::Value            vals[4]{};
        Vars::Bank             bank;
        Alarms::AlarmController     model;
        Pages::AlarmsPage      page;

        // @p rows: a short screen overflows the 4-alarm set so UP/DOWN actually scroll.
        explicit RenderFixture(uint16_t rows = 24)
            // Very high baud so the cooperative TX floor never throttles a paint.
            : console(Botanik::SetupContext(), wire.serial, 80, rows, &now, 4000000u),
              bank(make_tables(TABLE, vals, 4)),
              model(Botanik::SetupContext(), &bank),
              page(console, &model) {
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

// Acking flips one row's badge; only the moved cells (badge+timestamp) reach
// the wire.
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

// A newly-raised alarm reorders the list, so the diff resends the whole row.
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

// The cursor sits on the VARIABLE cell; moving selection (no scroll) touches
// only the old/new row's cells - highlight costs cells, not full rows.
TEST_CASE("alarms render: a selection move repaints both VARIABLE cells, nothing else") {
    RenderFixture f;                             // 24 rows: all four fit, so no scroll
    step_bits(f.bank, f.model, 0xF, 1000);                     // aaa..ddd active; row0=aaa selected
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

// SPACE's badge must reach the wire before the table rewrite it triggers;
// the legend row must survive the tick it's raised in, or a naive
// "reconciled?" check demotes it before it is ever drawn.
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
    // Just the legend, not a table's worth of bytes.
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

// The #if guards below need BOTANIK_FEATURE_* defined (undefined reads as 0).


using namespace Botanik;
using namespace Botanik::Alarms;

namespace {
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

// Only MqttAlarmNotifier needs BOTANIK_FEATURE_ALARMS_PUBLISH (the fan-out
// case below uses a stub notifier).
#if BOTANIK_FEATURE_ALARMS_PUBLISH
TEST_CASE("MqttAlarmNotifier: notify_on/off build the topic + payload and forward to the publisher") {
    MockPublisher pub;
    MqttAlarmNotifier n("botanik/greenhouse");
    n.set_publisher(&pub);

    // The condition borrows its row (no text copy), so this needs a real
    // SCOPE_A descriptor.
    static const Botanik::Vars::Variable ROW =
        Botanik::Vars::A("NET_FLT", "NET_FLT", 0, 0, 0, 0, 0, 990, "", "");
    AlarmCondition c = {};
    c.var = &ROW;

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
    const Vars::Variable TABLE[] = { Vars::A("x", "X", 0, 0, 0, 0, 0, 800, "", "") };
    Vars::Value vals[1]{};
    Vars::VarTables t = {};
    t.var = TABLE; t.val = vals; t.count = 1;
    Vars::Bank bank(t);
    bank.init();
    AlarmController m(Botanik::SetupContext(), &bank);
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
