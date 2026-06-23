/**
 * @file test_notifier.cpp
 * @brief AlarmNotifier formats "<prefix>/alarms/<CODE>" + ON/OFF and forwards to a
 *        Publisher; with no publisher it is a no-op.
 */
#include <cstring>
#include <string>

#include "doctest.h"

#include "libbotanik/alarms/alarms.h"
#include "libbotanik/alarms/publisher.h"

using namespace Botanik;
using namespace Botanik::Alarms;

namespace {
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

TEST_CASE("AlarmNotifier: notify_on/off build the topic + payload and forward to the publisher") {
    MockPublisher pub;
    AlarmNotifier n("botanik/greenhouse");
    n.set_publisher(&pub);

    AlarmCondition c = {};
    c.code = "NET_FLT";

    n.notify_on(c);
    CHECK(pub.calls == 1);
    CHECK(std::string(pub.topic)   == "botanik/greenhouse/alarms/NET_FLT");
    CHECK(std::string(pub.payload) == "ON");

    n.notify_off(c);
    CHECK(pub.calls == 2);
    CHECK(std::string(pub.payload) == "OFF");
}

TEST_CASE("AlarmNotifier: with no publisher set it is a safe no-op") {
    AlarmNotifier n("botanik/greenhouse");
    AlarmCondition c = {};
    c.code = "NET_FLT";
    n.notify_on(c);    // must not crash / dereference null
    n.notify_off(c);
    CHECK(true);
}
