/**
 * @file test_notifier.cpp
 * @brief MqttAlarmNotifier formats "<prefix>/alarms/<CODE>" + ON/OFF/FLAP and forwards
 *        to a Publisher; with no publisher it is a no-op.
 */
#include <cstring>                          // strcmp / strstr
#include <string>                           // std::string

#include "doctest.h"                        // the test framework (vendored single header)

// Pulls the site config, then the feature defaults: the guards below
// need the flags DEFINED, and an undefined one reads as 0 in #if.
#include "libbotanik_config.h"              // Config::MQTT_TOPIC_PREFIX

#include "libbotanik/alarms/alarms.h"       // Alarms::MqttAlarmNotifier
#include "libbotanik/alarms/publisher.h"    // Alarms::Publisher
#include "libbotanik/vars/declare.h"        // the A() factory

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
    const Vars::Variable TABLE[] = { Vars::A("x", "X", 0, 0, 0, 0, 0, "", "") };
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
