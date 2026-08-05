/**
 * @file test_actuator.cpp
 * @brief The two-position actuator (scan/blocks/actuator.h). Time is handed in
 *        as dt, so a 60 s stroke runs in a loop of a few dozen iterations.
 */
#include <string>

#include "doctest.h"

#include "libbotanik/plc/blocks/actuator.h"

// Compiles to an empty TU when the blocks are off - the namespace
// itself is gated, so this must sit ABOVE the using-directive.
#if BOTANIK_FEATURE_PLC_BLK_BASE

using namespace Botanik::Plc;


namespace {

// 10 s open, 8 s close, 2% deadband, 1 s seating overrun, 250 ms minimum run,
// 500 ms reversal dead time.
const ActuatorConfig CFG(10000, 8000, 0.02f, 1000, 250, 500);

/** @brief Run the actuator for @p ms at a fixed target, 100 ms at a time.
 *  @return the number of scans the open coil was energised for. */
int drive(Actuator& a, float target, uint32_t ms) {
    int open_scans = 0;
    for (uint32_t t = 0; t < ms; t += 100) {
        a.step(target, CFG, 100);
        if (a.open_cmd()) open_scans++;
        // Extra parentheses: doctest decomposes the expression, and cannot
        // take one apart across a &&.
        CHECK_FALSE((a.open_cmd() && a.close_cmd()));   // never both coils
    }
    return open_scans;
}

}  // namespace

TEST_CASE("Actuator: starts UNKNOWN and datums against the closed stop") {
    Actuator a;
    CHECK(a.state() == Actuator::ST_UNKNOWN);
    CHECK_FALSE(a.known());

    // A mid target from UNKNOWN closes fully first: the position estimate is
    // worth nothing until something has reached a mechanical stop.
    a.step(0.5f, CFG, 100);
    CHECK(a.state() == Actuator::ST_CLOSING);
    CHECK(a.close_cmd());

    drive(a, 0.5f, 16000);         // 8 s travel + 1 s seating, then the move
    CHECK(a.known());
    CHECK(a.position() == doctest::Approx(0.5f).epsilon(0.05));
    CHECK(a.state() == Actuator::ST_OPEN);      // off the seat, so open
}

TEST_CASE("Actuator: a full-open target datums against the open stop instead") {
    Actuator a;
    a.step(1.0f, CFG, 100);
    CHECK(a.state() == Actuator::ST_OPENING);   // no pointless close first
    drive(a, 1.0f, 12000);
    CHECK(a.state() == Actuator::ST_OPEN);
    CHECK(a.position() == doctest::Approx(1.0f));
}

TEST_CASE("Actuator: partial openings are timed against the travel time") {
    Actuator a;
    a.preset(0.0f);                             // datumed closed already
    CHECK(a.state() == Actuator::ST_CLOSED);

    // 30% of a 10 s open travel is 3 s of coil.
    const int scans = drive(a, 0.3f, 6000);
    CHECK(scans == 30);
    CHECK(a.position() == doctest::Approx(0.3f));
    CHECK(a.state() == Actuator::ST_OPEN);
    CHECK_FALSE(a.moving());
}

TEST_CASE("Actuator: CLOSED means on the seat, and only a full close gives it") {
    Actuator a;
    a.preset(1.0f);
    drive(a, 0.5f, 6000);
    CHECK(a.state() == Actuator::ST_OPEN);      // half open is open
    drive(a, 0.0f, 10000);
    CHECK(a.state() == Actuator::ST_CLOSED);
    CHECK(a.position() == doctest::Approx(0.0f));
}

TEST_CASE("Actuator: a stroke to an end overruns, so the estimate re-datums") {
    Actuator a;
    a.preset(0.0f);
    // Walk it half open and back a few times. Without the seating overrun the
    // estimate would drift; with it, every full close puts it back on zero.
    for (int i = 0; i < 3; i++) {
        drive(a, 0.5f, 8000);
        drive(a, 0.0f, 8000);
        CHECK(a.position() == doctest::Approx(0.0f));
        CHECK(a.state() == Actuator::ST_CLOSED);
    }
}

TEST_CASE("Actuator: no twitching inside the deadband or under the minimum run") {
    Actuator a;
    a.preset(0.5f);
    drive(a, 0.51f, 2000);                      // inside the 2% deadband
    CHECK(a.position() == doctest::Approx(0.5f));
    // Outside the deadband, but 2.3% of a 10 s travel is a 230 ms run - under
    // min_run_ms, so still not worth energising the motor.
    drive(a, 0.523f, 2000);
    CHECK(a.position() == doctest::Approx(0.5f));
    drive(a, 0.6f, 3000);                       // 1 s of travel, worth doing
    CHECK(a.position() == doctest::Approx(0.6f));
}

TEST_CASE("Actuator: reversing mid-stroke passes through the dead time") {
    Actuator a;
    a.preset(0.0f);
    for (int i = 0; i < 10; i++) a.step(1.0f, CFG, 100);   // ~9% open
    CHECK(a.open_cmd());

    // Demand flips to closed: the open coil drops in the same scan, and the
    // close coil stays off for the reversal dead time.
    a.step(0.0f, CFG, 100);
    CHECK_FALSE(a.open_cmd());
    CHECK_FALSE(a.close_cmd());
    a.step(0.0f, CFG, 100);
    CHECK_FALSE(a.close_cmd());                 // 100 of 500 ms
    a.step(0.0f, CFG, 400);
    CHECK(a.close_cmd());                       // dead time served
}

TEST_CASE("Actuator: a committed seating stroke is not cut short") {
    Actuator a;
    a.preset(0.1f);
    // Target 0 from 10% open: the estimate says 800 ms of travel, but the
    // stroke is armed for that plus the seating overrun, and nothing cuts it
    // short at the estimated arrival - that overrun IS the datum.
    for (int i = 0; i < 12; i++) a.step(0.0f, CFG, 100);
    CHECK(a.position() == doctest::Approx(0.0f));   // estimate says arrived
    CHECK(a.close_cmd());                           // ...and it is still driving
    CHECK(a.state() == Actuator::ST_CLOSING);
    drive(a, 0.0f, 1500);
    CHECK(a.state() == Actuator::ST_CLOSED);
}

TEST_CASE("Actuator: stop drops both coils, home forgets the position") {
    Actuator a;
    a.preset(0.0f);
    a.step(1.0f, CFG, 100);
    CHECK(a.moving());
    a.stop();
    CHECK_FALSE(a.moving());
    CHECK(a.known());                           // the estimate still stands

    a.home();
    CHECK_FALSE(a.known());
    CHECK(a.state() == Actuator::ST_UNKNOWN);
    // ...and the next command datums again rather than trusting the estimate.
    // (The stop left a dead time behind, which this scan serves out.)
    a.step(0.5f, CFG, 500);
    CHECK(a.state() == Actuator::ST_CLOSING);
}

TEST_CASE("Actuator: a datum stroke cut short leaves the position unknown") {
    Actuator a;
    a.step(0.5f, CFG, 100);
    CHECK(a.state() == Actuator::ST_CLOSING);
    a.stop();
    CHECK(a.state() == Actuator::ST_UNKNOWN);
    CHECK_FALSE(a.known());
}

TEST_CASE("Actuator: state_name covers all five") {
    Actuator a;
    CHECK(std::string(a.state_name()) == "UNKNOWN");
    a.step(1.0f, CFG, 100);
    CHECK(std::string(a.state_name()) == "OPENING");
    drive(a, 1.0f, 12000);
    CHECK(std::string(a.state_name()) == "OPEN");
    a.step(0.0f, CFG, 100);
    CHECK(std::string(a.state_name()) == "CLOSING");
    drive(a, 0.0f, 10000);
    CHECK(std::string(a.state_name()) == "CLOSED");
}

#endif
