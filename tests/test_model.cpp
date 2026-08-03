/**
 * @file test_model.cpp
 * @brief The unified variable model: seeding by role, the invalid-reads-as-
 *        default rule, staleness expiry, the fail-safe sweep, lookup, and the
 *        inert sink.
 */
#include <cstdint>

#include "doctest.h"

#include "libbotanik/utils/text.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/declare.h"

using namespace Botanik;
using namespace Botanik::Vars;

namespace {
    enum Id : uint16_t {
        V_EXT_TEMP, V_NO_TIMEOUT, V_VALVE, V_ALARM_HOT, V_SETPOINT,
        V_NTP_POLL, V_NTP_SERVER, V_COUNT
    };

    // One table, every scope and several types - the shape a site has. Each row
    // is the factory for its scope, so a row cannot carry a field its kind has
    // no use for.
    const Variable VARS[] = {
        MI("ext_temp",    T_F32, 5, 3, 18, 2, SF(-40.0f), SF(85.0f), SF(20.5f), 5000, 1,
                          "C", "outside", ""),
        MI("no_timeout",  T_F32, 5, 3, 18, 2, SF(0.0f), SF(100.0f), SF(1.5f), 0, 1,
                          "C", "", ""),
        RO("valve",       31, 0, true, false, "drain valve", ""),
        A("alarm_hot",    "HOT", 2, 2, 0, 0, 0, "over temperature", ""),
        G ("setpoint",    T_F32, SF(18.0f), 1, "C", "target temp", ""),
        P("ntp_poll_s",   T_U16, 64, 0, 3600, 0, "s", 1, "NTP poll interval", ""),
        PS("ntp_server1", "ntp.example.org", 32, STR_HOST, 2, "primary NTP server", ""),
    };

    struct Fixture {
        Value values[V_COUNT]{};
        char  pool[64]{};
        Bank  bank;
        Fixture() {
            VarTables t = {};
            t.var = VARS; t.val = values; t.count = V_COUNT;
            t.pool = pool; t.pool_bytes = sizeof(pool);
            bank = Bank(t);
            bank.init();
        }
    };
}

TEST_CASE("model: seeding sets validity by role, not by type") {
    Fixture f;
    // A parameter always HAS a value - its factory default - so it starts valid.
    CHECK(f.bank.var(V_NTP_POLL).valid());
    CHECK(f.bank.var(V_NTP_POLL).i() == 64);
    // Everything else starts invalid, so the console can distinguish a reading
    // from a default it is standing in for.
    CHECK_FALSE(f.bank.var(V_EXT_TEMP).valid());
    CHECK_FALSE(f.bank.var(V_VALVE).valid());
    CHECK_FALSE(f.bank.var(V_ALARM_HOT).valid());
    // ...but still read as that default.
    CHECK(f.bank.var(V_EXT_TEMP).value() == doctest::Approx(20.5f));
    CHECK(f.bank.var(V_VALVE).b());
}

TEST_CASE("model: a string default is copied into the pool") {
    Fixture f;
    CHECK(Utils::compare_str(f.pool, "ntp.example.org"));
}

TEST_CASE("model: an invalid variable reads as its default") {
    Fixture f;
    f.bank.var(V_EXT_TEMP).set(31.7f);
    CHECK(f.bank.var(V_EXT_TEMP).value() == doctest::Approx(31.7f));
    f.bank.var(V_EXT_TEMP).safe();                     // driver lost it
    CHECK(f.bank.var(V_EXT_TEMP).value() == doctest::Approx(20.5f));
}

TEST_CASE("model: publish carries the timestamp, so staleness cannot be forgotten") {
    Fixture f;
    f.bank.var(V_EXT_TEMP).publish(Scalar::F(30.0f), 1000);
    CHECK(f.bank.var(V_EXT_TEMP).valid());
    CHECK(f.bank.var(V_EXT_TEMP).stamp() == 1000);

    f.bank.expire_stale(5000);                          // 4 s old, under the 5 s timeout
    CHECK(f.bank.var(V_EXT_TEMP).valid());
    f.bank.expire_stale(7000);                          // 6 s old, past it
    CHECK_FALSE(f.bank.var(V_EXT_TEMP).valid());
    CHECK(f.bank.var(V_EXT_TEMP).value() == doctest::Approx(20.5f));
}

TEST_CASE("model: timeout 0 never expires, and only inputs expire at all") {
    Fixture f;
    f.bank.var(V_NO_TIMEOUT).publish(Scalar::F(9.0f), 1000);
    f.bank.var(V_ALARM_HOT).set(true);
    f.bank.var(V_NTP_POLL).set_i(120);

    f.bank.expire_stale(9999999);
    CHECK(f.bank.var(V_NO_TIMEOUT).valid());   // timeout 0
    CHECK(f.bank.var(V_ALARM_HOT).valid());    // internal: not an input
    CHECK(f.bank.var(V_NTP_POLL).valid());     // parameter: never expires
}

TEST_CASE("control: disabling takes outputs to safe and nothing else") {
    Fixture f;
    f.bank.enable_control(Bank::INHIBIT_HEALTH);
    f.bank.var(V_VALVE).set(false);
    f.bank.var(V_ALARM_HOT).set(true);
    f.bank.var(V_SETPOINT).set(21.5f);
    f.bank.var(V_NTP_POLL).set_i(120);

    f.bank.disable_control();

    // The output drops to its declared safe state...
    CHECK_FALSE(f.bank.control_enabled());
    CHECK_FALSE(f.bank.var(V_VALVE).valid());
    CHECK(f.bank.var(V_VALVE).b());                     // def is true
    // ...the alarm survives, because clearing it would erase the diagnosis that
    // took control away in the first place...
    CHECK(f.bank.var(V_ALARM_HOT).b());
    CHECK(f.bank.var(V_SETPOINT).value() == doctest::Approx(21.5f));
    // ...and the config survives, because a scan overrun must not revert it.
    CHECK(f.bank.var(V_NTP_POLL).i() == 120);
}

TEST_CASE("model: writes are coerced into the variable's own type") {
    Fixture f;
    f.bank.var(V_NTP_POLL).set(120.6f);        // float into a u16
    CHECK(f.bank.var(V_NTP_POLL).i() == 121);
    f.bank.var(V_VALVE).set(0.0f);             // float into a bool
    CHECK_FALSE(f.bank.var(V_VALVE).b());
    f.bank.var(V_SETPOINT).set_i(22);          // int into an f32
    CHECK(f.bank.var(V_SETPOINT).value() == doctest::Approx(22.0f));
}

TEST_CASE("model: lookup by name, exact and length-bounded") {
    Fixture f;
    CHECK(f.bank.find("ext_temp").index == V_EXT_TEMP);
    CHECK(f.bank.find("ntp_server1").index == V_NTP_SERVER);
    CHECK(f.bank.find("nope").index == VAR_NONE);
    CHECK(f.bank.find(nullptr).index == VAR_NONE);
    // A shell argument is a span, not a NUL-terminated string: "ext_temp" must
    // not match on a prefix of a longer name, nor a longer name on a prefix.
    CHECK(f.bank.find("ext_temp_extra", 8).index == V_EXT_TEMP);
    CHECK(f.bank.find("ext", 3).index == VAR_NONE);
}

TEST_CASE("model: a bad index resolves to an inert sink, never a clamp") {
    // NOT clamped to the last entry: clamping aliased a typo onto a real
    // signal, so a bad index would have driven an actual relay.
    Fixture f;
    CHECK(f.bank.var((uint16_t)999).value() == doctest::Approx(0.0f));
    CHECK(Utils::compare_str(f.bank.var((uint16_t)999).name(), "<none>"));

    f.bank.var(V_VALVE).set(true);
    f.bank.var((uint16_t)999).set(false);      // absorbed, drives nothing
    CHECK(f.bank.var(V_VALVE).b());            // the real coil is untouched

    // VAR_NONE is the same story, which is what lets a platform alarm spell
    // "reads no variable" without aliasing row 0.
    VarRef none = { VAR_NONE };
    CHECK_FALSE(f.bank.var(none).valid());
    CHECK(f.bank.var(none).value() == doctest::Approx(0.0f));
}

TEST_CASE("model: the sink never accumulates state between bad accesses") {
    Fixture f;
    f.bank.var((uint16_t)998).set(42.0f);
    CHECK(f.bank.var((uint16_t)999).value() == doctest::Approx(0.0f));
}

TEST_CASE("model: an empty table is safe to index and sweep") {
    VarTables t = {};
    Bank b(t);
    b.init();
    CHECK(b.count() == 0);
    CHECK(b.var((uint16_t)0).value() == doctest::Approx(0.0f));
    b.var((uint16_t)0).set(50.0f);             // absorbed
    b.disable_control();                       // and sweeps cleanly
    b.expire_stale(1000);
    CHECK(b.find("anything").index == VAR_NONE);
}

TEST_CASE("model: parameters are a dense block addressable by index") {
    Fixture f;
    CHECK(f.bank.param_count() == 2);
    CHECK(Utils::compare_str(f.bank.param(0).name(), "ntp_poll_s"));
    CHECK(Utils::compare_str(f.bank.param(1).name(), "ntp_server1"));
    // Past the block resolves to the sink rather than to a neighbouring row.
    CHECK(Utils::compare_str(f.bank.param(9).name(), "<none>"));
}

// ---------------------------------------------------------------------------
// Overrides: the maintenance shadow over a variable's live value.
// ---------------------------------------------------------------------------

TEST_CASE("override: shadows the live value, and clearing snaps straight back") {
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);

    h.publish(Scalar::F(20.0f), 1000);
    CHECK(h.value() == doctest::Approx(20.0f));
    CHECK_FALSE(h.overridden());

    h.override_set(Scalar::F(35.0f));
    CHECK(h.overridden());
    CHECK(h.value() == doctest::Approx(35.0f));
    CHECK(h.valid());

    // The driver keeps publishing underneath; the override still wins...
    h.publish(Scalar::F(21.0f), 2000);
    CHECK(h.value() == doctest::Approx(35.0f));

    // ...and clearing it resumes the live reading, with nothing to restore.
    h.override_clear();
    CHECK_FALSE(h.overridden());
    CHECK(h.value() == doctest::Approx(21.0f));
}

TEST_CASE("override: can force the quality, not just the value") {
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);
    h.publish(Scalar::F(20.0f), 1000);

    h.override_set(Scalar::F(20.0f), /*valid=*/false);
    CHECK(h.overridden());
    CHECK_FALSE(h.valid());                        // reads as a dead sensor...
    CHECK(h.value() == doctest::Approx(20.0f));    // ...at a known value
}

TEST_CASE("override: freezing pins the value the variable had at that moment") {
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);
    h.publish(Scalar::F(7.5f), 1000);

    h.override_freeze();
    h.publish(Scalar::F(99.0f), 2000);
    CHECK(h.value() == doctest::Approx(7.5f));
}

TEST_CASE("override: an expired input still reads its forced value") {
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);
    h.publish(Scalar::F(20.0f), 1000);
    h.override_set(Scalar::F(20.0f));

    // Well past the declared timeout: the underlying slot expires, but what a
    // reader sees is the override - which is the whole point of forcing one.
    f.bank.expire_stale(1000000);
    CHECK(h.valid());
    CHECK(h.value() == doctest::Approx(20.0f));
}

TEST_CASE("control: disabling releases a forced output, and outranks it") {
    Fixture f;
    f.bank.enable_control(Bank::INHIBIT_HEALTH);   // bring-up finished
    REQUIRE(f.bank.control_enabled());
    Var h = f.bank.var(V_VALVE);
    h.set(true);
    h.override_set(Scalar::B(true));
    CHECK(h.overridden());

    // Control wins: an override is for a machine someone is watching, and this
    // runs when the firmware can no longer reason about what it drives.
    // Released, not merely ignored - a staged override would fire the moment
    // control came back.
    f.bank.disable_control();
    CHECK_FALSE(h.overridden());
    CHECK_FALSE(h.valid());
    CHECK(h.b() == true);      // reads its declared safe state (def = true here)

    // An input's override is untouched - the sweep is outputs only.
    Var in = f.bank.var(V_EXT_TEMP);
    in.override_set(Scalar::F(5.0f));
    f.bank.disable_control();
    CHECK(in.overridden());
}

TEST_CASE("control: starts disabled, and refuses to arm an output while it is") {
    Fixture f;
    // Nothing may drive the field between power-on and the point bring-up says
    // it is ready - the system holds control until then.
    CHECK_FALSE(f.bank.control_enabled());
    CHECK(f.bank.control_inhibited_by(Bank::INHIBIT_HEALTH));

    Var valve = f.bank.var(V_VALVE);
    CHECK(f.bank.override_set(valve, "1") == SET_INVALID);
    CHECK_FALSE(valve.overridden());

    // An input is still forceable - troubleshooting a reading does not move an
    // actuator.
    CHECK(f.bank.override_set(f.bank.var(V_EXT_TEMP), "12.5") == SET_OK);

    f.bank.enable_control(Bank::INHIBIT_HEALTH);
    CHECK(f.bank.control_enabled());
    CHECK(f.bank.override_set(valve, "1") == SET_OK);
    CHECK(valve.overridden());
}

TEST_CASE("control: an operator disable survives a health fault coming and going") {
    Fixture f;
    f.bank.enable_control(Bank::INHIBIT_HEALTH);      // bring-up finished
    CHECK(f.bank.control_enabled());

    f.bank.disable_control(Bank::INHIBIT_OPERATOR);   // someone at the console
    CHECK_FALSE(f.bank.control_enabled());

    // A fault arrives and clears underneath them. Health releasing its own hold
    // must not hand control back - the operator never asked for it back.
    f.bank.disable_control(Bank::INHIBIT_HEALTH);
    f.bank.enable_control(Bank::INHIBIT_HEALTH);
    CHECK_FALSE(f.bank.control_enabled());
    CHECK(f.bank.control_inhibited_by(Bank::INHIBIT_OPERATOR));
    CHECK_FALSE(f.bank.control_inhibited_by(Bank::INHIBIT_HEALTH));

    // Only they can give it back.
    f.bank.enable_control(Bank::INHIBIT_OPERATOR);
    CHECK(f.bank.control_enabled());
}

TEST_CASE("control: an operator enable cannot override a health fault either") {
    Fixture f;
    f.bank.enable_control(Bank::INHIBIT_HEALTH);
    f.bank.disable_control(Bank::INHIBIT_HEALTH);     // health faulted

    // The symmetric case, and the more important one: the console must not be
    // able to talk over the firmware's own judgement that it cannot drive.
    f.bank.enable_control(Bank::INHIBIT_OPERATOR);
    CHECK_FALSE(f.bank.control_enabled());
    CHECK(f.bank.control_inhibited_by(Bank::INHIBIT_HEALTH));
}

TEST_CASE("control: a second hold arriving does not re-sweep") {
    Fixture f;
    f.bank.enable_control(Bank::INHIBIT_HEALTH);
    Var in = f.bank.var(V_EXT_TEMP);
    f.bank.disable_control(Bank::INHIBIT_OPERATOR);

    // An input override set to diagnose the fault survives health piling on:
    // the sweep already ran, and the outputs cannot have moved since.
    in.override_set(Scalar::F(5.0f));
    f.bank.disable_control(Bank::INHIBIT_HEALTH);
    CHECK(in.overridden());
}

TEST_CASE("override: init and clear_overrides drop every one of them") {
    Fixture f;
    f.bank.var(V_EXT_TEMP).override_set(Scalar::F(1.0f));
    f.bank.var(V_VALVE).override_set(Scalar::B(true));
    CHECK(f.bank.override_count() == 2);

    CHECK(f.bank.clear_overrides() == 2);
    CHECK(f.bank.override_count() == 0);

    // A reboot re-seeds the table: an override must never be waiting there.
    f.bank.var(V_EXT_TEMP).override_set(Scalar::F(1.0f));
    f.bank.init();
    CHECK(f.bank.override_count() == 0);
}

TEST_CASE("override: parses text, and deliberately ignores the plausibility band") {
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);          // band -40..85

    CHECK(f.bank.override_set(h, "12.5") == SET_OK);
    CHECK(h.value() == doctest::Approx(12.5f));

    // Out of band on purpose: forcing a reading past its plausibility limit is
    // how you exercise the alarm that watches for exactly that.
    CHECK(f.bank.override_set(h, "999") == SET_OK);
    CHECK(h.value() == doctest::Approx(999.0f));

    CHECK(f.bank.override_set(h, "not-a-number") == SET_INVALID);
    // A string's live text lives in the pool, so there is nothing to shadow.
    CHECK(f.bank.override_set(f.bank.var(V_NTP_SERVER), "host") == SET_INVALID);
}

TEST_CASE("set: refuses anything that is not a parameter") {
    Fixture f;
    // No store bound, so a valid write reports NOT_PERSISTED rather than OK.
    CHECK(f.bank.set(f.bank.var(V_NTP_POLL), "120") == SET_NOT_PERSISTED);
    CHECK(f.bank.var(V_NTP_POLL).i() == 120);

    CHECK(f.bank.set(f.bank.var(V_EXT_TEMP), "20") == SET_INVALID);   // input
    CHECK(f.bank.set(f.bank.var(V_VALVE), "1")     == SET_INVALID);   // output
    CHECK(f.bank.set(f.bank.var(V_SETPOINT), "20") == SET_INVALID);   // internal
}

TEST_CASE("set: holds a parameter to its declared band") {
    Fixture f;
    CHECK(f.bank.set(f.bank.var(V_NTP_POLL), "3601") == SET_INVALID);  // max 3600
    CHECK(f.bank.var(V_NTP_POLL).i() == 64);                           // untouched
}
