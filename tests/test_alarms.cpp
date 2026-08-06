/**
 * @file test_alarms.cpp
 * @brief AlarmController behaviour: ack / ack_all and the reset-on-ON-edge (a fresh
 *        episode is unacknowledged), the minimum-ON hold (min_on_s anti-flicker),
 *        and the END-vs-FLAP distinction (an END with no matching START is a FLAP).
 */
#include <cstddef>                      // size_t
#include <vector>                       // std::vector

#include "libbotanik/alarms/alarms.h"   // Alarms::AlarmController
#include "libbotanik/logs/logger.h"     // Logs::Logger
#include "libbotanik/vars/declare.h"    // the A() factory

#include "doctest.h"

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
        Vars::A("aaa", "AAA", -1, -1, 0, 0, 0, "alarm A", ""),
        Vars::A("bbb", "BBB", -1, -1, 0, 0, 0, "alarm B", ""),
    };
    // One condition with a 5 s minimum-ON hold (announce disabled).
    const Vars::Variable MIN_ON_TABLE[] = {
        Vars::A("min", "MIN", -1, -1, 0, 0, 5, "min on", ""),
    };
    // One condition that announces a START only after a 2 s dwell, and an END as
    // soon as it clears (off_s 0). A brief blip clears before the START dwell -> FLAP.
    const Vars::Variable FLAP_TABLE[] = {
        Vars::A("flp", "FLP", 2, 0, 0, 0, 0, "flap", ""),
    };
    // on_s 0 so the START is announced at once.
    const Vars::Variable NORMAL_TABLE[] = {
        Vars::A("nrm", "NRM", 0, 0, 0, 0, 0, "normal", ""),
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

TEST_CASE("alarm: an END with no matching START is logged as a FLAP") {
    Logs::Logger logger;
    TopicRecorder rec;
    logger.add_writer(&rec);
    AlarmBank f(FLAP_TABLE, 1);
    Alarms::AlarmController m(&f.bank, &logger);

    // A blip shorter than the 2 s START dwell: clears before any START is logged.
    step_bits(f.bank, m, 0x1, 1000);                             // on (dwell 0 < on_s 2 s)
    logger.refill();
    CHECK(rec.has(Logs::ALARM_START) == false);   // too brief to announce a START
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
    CHECK(rec.has(Logs::ALARM_START) == true);
    step_bits(f.bank, m, 0x0, 2000);                             // off -> END (START was logged)
    logger.refill();
    CHECK(rec.has(Logs::ALARM_END) == true);
    CHECK(rec.has(Logs::ALARM_FLAP) == false);
}

TEST_CASE("alarm reader: model reads each condition's variable") {
    // Three alarm rows. The row IS the condition, so there is no second table
    // and nothing to keep in step: condition i reads variable i.
    const Vars::Variable VARS[] = {
        Vars::A("alarm_a", "AAA", -1, -1, 0, 0, 0, "a", ""),
        Vars::A("alarm_b", "BBB", -1, -1, 0, 0, 0, "b", ""),
        Vars::A("alarm_c", "CCC", -1, -1, 0, 0, 0, "c", ""),
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


