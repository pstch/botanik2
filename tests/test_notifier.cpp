/**
 * @file test_notifier.cpp
 * @brief MqttAlarmNotifier formats "<prefix>/alarms/<CODE>" + ON/OFF/FLAP and forwards
 *        to a Publisher; with no publisher it is a no-op.
 */
#include <cstring>
#include <string>

#include "doctest.h"

#include "libbotanik/alarms/alarms.h"
#include "libbotanik/alarms/publisher.h"

using namespace Botanik;
using namespace Botanik::Alarms;

namespace {
    // Drive a model without a PLC payload: set each condition's local override
    // from @p bits (bit i -> condition i), then step.
    void step_bits(AlarmModel& m, uint64_t bits, uint64_t now) {
        for (uint8_t i = 0; i < m.count(); i++)
            m.set_local(i, ((bits >> i) & 1u) != 0);
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

TEST_CASE("MqttAlarmNotifier: notify_on/off build the topic + payload and forward to the publisher") {
    MockPublisher pub;
    MqttAlarmNotifier n("botanik/greenhouse");
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

    n.notify_flap(c);   // an OFF with no matching ON this episode
    CHECK(pub.calls == 3);
    CHECK(std::string(pub.topic)   == "botanik/greenhouse/alarms/NET_FLT");
    CHECK(std::string(pub.payload) == "FLAP");
}

TEST_CASE("MqttAlarmNotifier: with no publisher set it is a safe no-op") {
    MqttAlarmNotifier n("botanik/greenhouse");
    AlarmCondition c = {};
    c.code = "NET_FLT";
    n.notify_on(c);    // must not crash / dereference null
    n.notify_off(c);
    CHECK(true);
}

namespace {
    // Counts the transitions the model fans out to it.
    struct CountingNotifier : AlarmNotifier {
        int on = 0, off = 0, flap = 0;
        void notify_on(const AlarmCondition&)   override { on++; }
        void notify_off(const AlarmCondition&)  override { off++; }
        void notify_flap(const AlarmCondition&) override { flap++; }
    };
}

TEST_CASE("AlarmModel: a transition fans out to every registered notifier") {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    // announce ON/OFF at once (on_s = off_s = 0).
    const AlarmCondition TABLE[] = { { "X", "x", 0, 0, 0, 0, 0 } };
#pragma GCC diagnostic pop
    AlarmModel m(TABLE, 1, nullptr);
    CountingNotifier a, b;
    CHECK(m.add_notifier(&a));
    CHECK(m.add_notifier(&b));
    CHECK(m.has_notifiers());
    CHECK(m.add_notifier(&a) == false);   // no double-register

    step_bits(m, 0x1, 1000);                    // ON -> both notified
    CHECK(a.on == 1);
    CHECK(b.on == 1);
    step_bits(m, 0x0, 2000);                    // clean OFF -> both notified
    CHECK(a.off == 1);
    CHECK(b.off == 1);

    // Drop one; the remaining notifier still fires, the removed one does not.
    CHECK(m.remove_notifier(&a));
    step_bits(m, 0x1, 3000);
    CHECK(a.on == 1);                     // unchanged after removal
    CHECK(b.on == 2);
}
