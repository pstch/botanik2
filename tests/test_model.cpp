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

using namespace Botanik;
using namespace Botanik::Vars;

namespace {
    // Shorthands so a row reads like the generated tables will.
    #define BIND_NONE   { XP_NONE, T_U8, 0, 0, 0, 0, 1.0f, 0.0f }
    #define BIND_MODBUS { XP_MODBUS, T_F32, 5, 3, 18, 2, 1.0f, 0.0f }
    #define BIND_COIL   { XP_LOCAL_DIGITAL, T_BOOL, 31, 5, 0, 1, 1.0f, 0.0f }

    enum Id : uint16_t {
        V_EXT_TEMP, V_NO_TIMEOUT, V_VALVE, V_ALARM_HOT, V_SETPOINT,
        V_NTP_POLL, V_NTP_SERVER, V_COUNT
    };

    // One table, every role and several types - the shape the site will have.
    const Variable VARS[] = {
        { Scalar::F(-40.0f), Scalar::F(85.0f), Scalar::F(20.5f),
          "ext_temp", "C", "outside", nullptr, BIND_MODBUS,
          5000, 0, 0, T_F32, ROLE_INPUT, 1, STR_NONE, F_BOUNDED, {} },
        { Scalar::F(0.0f), Scalar::F(100.0f), Scalar::F(1.5f),
          "no_timeout", "C", "", nullptr, BIND_MODBUS,
          0, 0, 0, T_F32, ROLE_INPUT, 1, STR_NONE, F_BOUNDED, {} },
        { Scalar::B(false), Scalar::B(true), Scalar::B(true),
          "valve", "", "drain valve", nullptr, BIND_COIL,
          0, 0, 0, T_BOOL, ROLE_OUTPUT, 0, STR_NONE, 0, {} },
        { Scalar::B(false), Scalar::B(true), Scalar::B(false),
          "alarm_hot", "", "over temperature", nullptr, BIND_NONE,
          0, 0, 0, T_BOOL, ROLE_INTERNAL, 0, STR_NONE, 0, {} },
        { Scalar::F(0.0f), Scalar::F(40.0f), Scalar::F(18.0f),
          "setpoint", "C", "target temp", nullptr, BIND_NONE,
          0, 0, 0, T_F32, ROLE_INTERNAL, 1, STR_NONE, F_BOUNDED, {} },
        { Scalar::U(0), Scalar::U(3600), Scalar::U(64),
          "ntp_poll_s", "s", "NTP poll interval", nullptr, BIND_NONE,
          0, 0, 0, T_U16, ROLE_PARAM, 0, STR_NONE, F_BOUNDED, {} },
        { Scalar::U(0), Scalar::U(0), Scalar::S("ntp.example.org"),
          "ntp_server1", "", "primary NTP server", nullptr, BIND_NONE,
          0, 0, 32, T_STR, ROLE_PARAM, 0, STR_HOST, 0, {} },
    };

    struct Fixture {
        Value values[V_COUNT]{};
        char  pool[64]{};
        Bank  bank;
        Fixture() {
            VarTables t = {};
            t.var = VARS; t.val = values; t.count = V_COUNT;
            t.pool = pool; t.pool_bytes = sizeof(pool);
            t.param_first = V_NTP_POLL; t.param_count = 2;
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

TEST_CASE("model: the fail-safe sweep takes outputs and nothing else") {
    Fixture f;
    f.bank.var(V_VALVE).set(false);
    f.bank.var(V_ALARM_HOT).set(true);
    f.bank.var(V_SETPOINT).set(21.5f);
    f.bank.var(V_NTP_POLL).set_i(120);

    f.bank.all_outputs_safe();

    // The output drops to its declared safe state...
    CHECK_FALSE(f.bank.var(V_VALVE).valid());
    CHECK(f.bank.var(V_VALVE).b());                     // def is true
    // ...the alarm survives, because clearing it would erase the diagnosis that
    // put the firmware into safe mode...
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
    b.all_outputs_safe();                      // and sweeps cleanly
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
