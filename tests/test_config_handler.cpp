/**
 * @file test_config_handler.cpp
 * @brief Tests for the Component lifecycle base - the first-step setup()+announce(),
 *        the config-serial watch + reset ordering (shared by the network / NTP /
 *        MQTT / sun components), and the healthy() default. Driven through a test
 *        subclass so the base logic is exercised without a live PLC context.
 */
#include <cstdint>
#include <cstring>
#include <string>

#include "doctest.h"

#include "libbotanik/core/component.h"
#include "libbotanik/logs/logger.h"
#include "recording_writer.h"

using namespace Botanik;

namespace {

    // A Component with a controllable serial and call counters: lets us drive the
    // base's setup / watch / reset / step ordering directly, no PLC context needed.
    struct TestHandler : Component {
        uint32_t serial          = 0;
        int      setups          = 0;
        int      changes         = 0;
        int      steps           = 0;
        uint64_t last_change_now = 0;
        explicit TestHandler(Logs::Logger* lg) : Component(lg, "TestHandler") {}
        uint32_t config_serial() const override { return serial; }
        void setup(uint64_t now) override { (void)now; setups++; }
        void on_config_changed(uint64_t now) override { changes++; last_change_now = now; }
        void on_step(uint64_t now) override { (void)now; steps++; }
    };

}  // namespace

TEST_CASE("Component: on_step runs every step, on_config_changed only on a serial change") {
    TestHandler h(nullptr);

    // Serial stays 0 (never configured): on_step runs, on_config_changed does not.
    h.step(100);
    CHECK(h.steps == 1);
    CHECK(h.changes == 0);

    // First non-zero serial: a change, stamped with the step's time.
    h.serial = 1;
    h.step(200);
    CHECK(h.steps == 2);
    CHECK(h.changes == 1);
    CHECK(h.last_change_now == 200);

    // Unchanged serial: still steps, no reset.
    h.step(300);
    CHECK(h.steps == 3);
    CHECK(h.changes == 1);

    // Bumped again: another reset.
    h.serial = 2;
    h.step(400);
    CHECK(h.changes == 2);
    CHECK(h.last_change_now == 400);

    // Cleared back to 0 (config removed): that is a change too - a component
    // like NetworkHandler tears the link down when its config is cleared.
    h.serial = 0;
    h.step(500);
    CHECK(h.changes == 3);
}

TEST_CASE("Component: a serial that jumps several values still resets exactly once") {
    TestHandler h(nullptr);
    h.serial = 5;          // skips 1..4 (e.g. several PLC edits before our first step)
    h.step(10);
    h.step(20);
    CHECK(h.changes == 1);
    CHECK(h.steps == 2);
}

TEST_CASE("Component: setup() runs once on the first step, before any on_step") {
    TestHandler h(nullptr);
    CHECK(h.setups == 0);
    h.step(10);
    CHECK(h.setups == 1);
    CHECK(h.steps == 1);
    for (int i = 0; i < 4; ++i) h.step((uint64_t)i);
    CHECK(h.setups == 1);       // still just the one
    CHECK(h.steps == 5);
}

TEST_CASE("Component: healthy() defaults to true") {
    TestHandler h(nullptr);
    CHECK(h.healthy());
}

TEST_CASE("Component: announces once on the first step, on the BOOT topic") {
    RecordingWriter w;
    Logs::Logger log;
    log.set_min_level(Logs::LOG_DEBUG);   // let the DEBUG announce through
    log.add_writer(&w);

    TestHandler h(&log);
    for (int i = 0; i < 5; ++i) h.step((uint64_t)i);

    CHECK(h.steps == 5);
#ifdef DEBUG
    CHECK(w.pushes == 1);                   // one announce total, not one per step
    CHECK(w.last_level == Logs::LOG_DEBUG);
    CHECK(w.last_topic == Logs::BOOT);
    CHECK(std::string(w.last_msg).find("TestHandler") != std::string::npos);
#else
    CHECK(w.pushes == 0);                   // compiled out when DEBUG is undefined
#endif
}
