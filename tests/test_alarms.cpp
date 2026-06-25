/**
 * @file test_alarms.cpp
 * @brief AlarmModel behaviour: ack / ack_all and the reset-on-ON-edge (a fresh
 *        episode is unacknowledged), the minimum-ON hold (min_on_s anti-flicker),
 *        and the END-vs-FLAP distinction (an END with no matching START is a FLAP).
 */
#include <vector>

#include "doctest.h"

#include "libbotanik/alarms/alarms.h"
#include "libbotanik/logs/logger.h"

using namespace Botanik;

namespace {
    // Static-prefix fields, in order: code, desc, masked_by, log_on_s, log_off_s,
    // mqtt_on_s, mqtt_off_s, recent_s, off_delay_s, min_on_s. The dynamic suffix is left
    // zero-initialized (the pragma silences the pedantic field-init warning).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    // Two unmasked, unlatched conditions (logging/notify disabled) at bits 0 and 1.
    const Alarms::AlarmCondition TABLE[] = {
        { "AAA", "alarm A", 0, -1, -1, -1, -1, 0, 0 },
        { "BBB", "alarm B", 0, -1, -1, -1, -1, 0, 0 },
    };
    // One condition with a 5 s minimum-ON hold (logging disabled).
    const Alarms::AlarmCondition MIN_ON_TABLE[] = {
        { "MIN", "min on", 0, -1, -1, -1, -1, 0, 0, 5 },
    };
    // One condition that logs a START only after a 2 s dwell, and an END as soon as
    // it clears (log_off_s 0). A brief blip clears before the START dwell -> FLAP.
    const Alarms::AlarmCondition FLAP_TABLE[] = {
        { "FLP", "flap", 0, 2, 0, -1, -1, 0, 0 },
    };
#pragma GCC diagnostic pop

    // Records the topic of every entry the logger forwards, for the FLAP tests.
    struct TopicRecorder : Logs::LogWriter {
        std::vector<PlcIO::LogTopic> topics;
        void     push(const PlcIO::LogEntry_t* e) override { topics.push_back(e->topic); }
        void     drain() override {}
        uint32_t loss_count() const override { return 0; }
        size_t   format(const PlcIO::LogEntry_t*, char*, size_t) override { return 0; }
        size_t   write(const char*, size_t) override { return 0; }
        bool     has(PlcIO::LogTopic t) const {
            for (PlcIO::LogTopic x : topics) if (x == t) return true;
            return false;
        }
    };
}

TEST_CASE("alarm: ack marks an active condition, ignores inactive / unknown") {
    Alarms::AlarmModel m(TABLE, 2, nullptr);
    m.step(0x1, 1000);                              // raise A (bit 0)
    CHECK(m.active_count() == 1);
    CHECK(m.condition(0).user_ack == false);
    CHECK(m.ack("AAA") == true);
    CHECK(m.condition(0).user_ack == true);
    CHECK(m.ack("BBB") == false);                   // B is not active
    CHECK(m.ack("ZZZ") == false);                   // unknown code
}

TEST_CASE("alarm: ack clears on the next ON edge (a fresh episode is unacknowledged)") {
    Alarms::AlarmModel m(TABLE, 2, nullptr);
    m.step(0x1, 1000);
    m.ack("AAA");
    CHECK(m.condition(0).user_ack == true);
    m.step(0x0, 2000);                              // A clears
    m.step(0x1, 3000);                              // A re-fires
    CHECK(m.condition(0).user_ack == false);    // reset on the ON edge
}

TEST_CASE("alarm: ack_all acknowledges every active condition and counts them") {
    Alarms::AlarmModel m(TABLE, 2, nullptr);
    m.step(0x3, 1000);                              // both active
    CHECK(m.ack_all() == 2);
    CHECK(m.condition(0).user_ack == true);
    CHECK(m.condition(1).user_ack == true);

    Alarms::AlarmModel m2(TABLE, 2, nullptr);
    m2.step(0x2, 1000);                             // only B active
    CHECK(m2.ack_all() == 1);
    CHECK(m2.condition(1).user_ack == true);
}

TEST_CASE("alarm: min_on_s holds a condition active after the raw bit clears") {
    Alarms::AlarmModel m(MIN_ON_TABLE, 1, nullptr);
    m.step(0x1, 1000);                              // raise (seeds the 5 s min-on hold)
    CHECK(m.condition(0).active == true);
    m.step(0x0, 2000);                              // raw clears 1 s in: still held
    CHECK(m.condition(0).active == true);
    m.step(0x0, 5500);                              // 4.5 s in: still within the 5 s hold
    CHECK(m.condition(0).active == true);
    m.step(0x0, 6500);                              // 5.5 s in: hold elapsed, now clears
    CHECK(m.condition(0).active == false);
}

TEST_CASE("alarm: an END with no matching START is logged as a FLAP") {
    Logs::Logger logger;
    TopicRecorder rec;
    logger.add_writer(&rec);
    Alarms::AlarmModel m(FLAP_TABLE, 1, &logger);

    // A blip shorter than the 2 s START dwell: clears before any START is logged.
    m.step(0x1, 1000);                             // on (dwell 0 < log_on_s 2 s)
    logger.step();
    CHECK(rec.has(PlcIO::ALARM_START) == false);   // too brief to announce a START
    m.step(0x0, 1500);                             // off -> END with no START = FLAP
    logger.step();
    CHECK(rec.has(PlcIO::ALARM_FLAP) == true);
    CHECK(rec.has(PlcIO::ALARM_END) == false);
}

TEST_CASE("alarm: a normal episode logs START then END (not FLAP)") {
    Logs::Logger logger;
    TopicRecorder rec;
    logger.add_writer(&rec);
    // log_on_s 0 so the START is announced at once.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    const Alarms::AlarmCondition tbl[] = { { "NRM", "normal", 0, 0, 0, -1, -1, 0, 0 } };
#pragma GCC diagnostic pop
    Alarms::AlarmModel m(tbl, 1, &logger);

    m.step(0x1, 1000);                             // on, START logged immediately
    logger.step();
    CHECK(rec.has(PlcIO::ALARM_START) == true);
    m.step(0x0, 2000);                             // off -> END (START was logged)
    logger.step();
    CHECK(rec.has(PlcIO::ALARM_END) == true);
    CHECK(rec.has(PlcIO::ALARM_FLAP) == false);
}
