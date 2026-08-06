/**
 * @file test_mqtt_log.cpp
 * @brief Tests for the MqttLogWriter sink: the lowercase level/topic name maps,
 *        the "<MQTT_LOG_TOPIC>/logs/<level>/<topic>" topic assembly, and the
 *        "<timestamp> <message>" payload. The publish path itself needs a live
 *        broker, so it is exercised by the sim, not here.
 */
#include <cstring>                          // memset / strcmp / strncpy / strstr
#include <string>                           // std::string

#include "doctest.h"

// Pulls the site config, then the feature defaults: the guards below
// need the flags DEFINED, and an undefined one reads as 0 in #if.
#include "libbotanik_config.h"              // Config::MQTT_TOPIC_PREFIX / LOG_TOPIC

#include "libbotanik/logs/format.h"         // Logs::format_log_level_name / _topic_name
#include "libbotanik/network/mqtt_log.h"    // Network::MqttLogWriter::build_topic

// Compiles to an empty TU when the feature is off (MqttLogWriter is the LOGS_PUBLISH transport),
// contributing no CTest cases - the shape test_sun.cpp uses.
#if BOTANIK_FEATURE_LOGS_PUBLISH

using namespace Botanik;

namespace {

    // Expose the protected format()/build_topic() seam for direct assertions.
    struct TestSink : Network::MqttLogWriter {
        using MqttLogWriter::build_topic;
        using MqttLogWriter::format;
    };

    Logs::LogEntry make_entry(Logs::LogLevel level, Logs::LogTopic topic,
                                 const char* msg) {
        Logs::LogEntry e;
        memset(&e, 0, sizeof(e));
        e.timestamp = 0;
        e.level = level;
        e.topic = topic;
        strncpy(e.message, msg, sizeof(e.message) - 1);
        return e;
    }

}

TEST_CASE("mqtt log: topic assembly") {
    char buf[96];
    size_t n = TestSink::build_topic(buf, sizeof(buf), Logs::LOG_DEBUG, Logs::SHARED_IO);
    CHECK(n > 0);
    // "<prefix>/<segment>/<level>/<topic>", both segments from config.
    std::string want = std::string(Config::MQTT_TOPIC_PREFIX) + "/"
                     + Config::MQTT_LOG_TOPIC + "/debug/shared_io";
    CHECK(want == buf);

    // Truncation reports 0 (no partial topic published).
    char tiny[8];
    CHECK(TestSink::build_topic(tiny, sizeof(tiny), Logs::LOG_ERROR, Logs::BOOT) == 0);
}

TEST_CASE("mqtt log: payload carries timestamp then message") {
    TestSink sink;
    Logs::LogEntry e = make_entry(Logs::LOG_NOTICE, Logs::NETWORK, "link up");
    char buf[128];
    size_t n = sink.format(&e, buf, sizeof(buf));
    CHECK(n > 0);
    // "<timestamp> <message>": ends with the message, and has a separating space.
    const char* tail = std::strstr(buf, "link up");
    REQUIRE(tail != nullptr);
    CHECK(tail > buf);           // something (the timestamp) precedes it
    CHECK(*(tail - 1) == ' ');
}

TEST_CASE("mqtt log: no publisher wired is a safe no-op") {
    Network::MqttLogWriter sink;   // never call set_publisher()
    Logs::LogEntry e = make_entry(Logs::LOG_CRITICAL, Logs::HEALTH, "boom");
    sink.push(&e);                 // must not crash / deref a null transport
    CHECK(sink.loss_count() == 0); // best-effort: link-down drops aren't losses
}

#endif
