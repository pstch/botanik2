/**
 * @file test_alarms.cpp
 * @brief AlarmModel acknowledge behaviour added in the review: ack / ack_all and
 *        the reset-on-ON-edge (a fresh episode is unacknowledged).
 */
#include "doctest.h"

#include "libbotanik/alarms/alarms.h"

using namespace Botanik;

namespace {
    // Two unmasked, unlatched conditions (logging/notify disabled) at bits 0 and 1.
    const Alarms::AlarmCondition TABLE[] = {
        { "AAA", "alarm A", 0, -1, -1, -1, -1, 0, 0 },
        { "BBB", "alarm B", 0, -1, -1, -1, -1, 0, 0 },
    };
}

TEST_CASE("alarm: ack marks an active condition, ignores inactive / unknown") {
    Alarms::AlarmModel m(TABLE, 2, nullptr);
    m.step(0x1, 1000);                              // raise A (bit 0)
    CHECK(m.active_count() == 1);
    CHECK(m.condition(0).acknowledged == false);
    CHECK(m.ack("AAA") == true);
    CHECK(m.condition(0).acknowledged == true);
    CHECK(m.ack("BBB") == false);                   // B is not active
    CHECK(m.ack("ZZZ") == false);                   // unknown code
}

TEST_CASE("alarm: ack clears on the next ON edge (a fresh episode is unacknowledged)") {
    Alarms::AlarmModel m(TABLE, 2, nullptr);
    m.step(0x1, 1000);
    m.ack("AAA");
    CHECK(m.condition(0).acknowledged == true);
    m.step(0x0, 2000);                              // A clears
    m.step(0x1, 3000);                              // A re-fires
    CHECK(m.condition(0).acknowledged == false);    // reset on the ON edge
}

TEST_CASE("alarm: ack_all acknowledges every active condition and counts them") {
    Alarms::AlarmModel m(TABLE, 2, nullptr);
    m.step(0x3, 1000);                              // both active
    CHECK(m.ack_all() == 2);
    CHECK(m.condition(0).acknowledged == true);
    CHECK(m.condition(1).acknowledged == true);

    Alarms::AlarmModel m2(TABLE, 2, nullptr);
    m2.step(0x2, 1000);                             // only B active
    CHECK(m2.ack_all() == 1);
    CHECK(m2.condition(1).acknowledged == true);
}
