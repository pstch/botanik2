/**
 * @file test_vars.cpp
 * @brief The variable model: Scalar, Variable/Value/Bank, parameters and retention.
 *
 * Each section is its own namespace so fixtures and helpers cannot collide.
 */

#include <cstdint>                  // int64_t/uint64_t
#include "libbotanik/utils/text.h"  // Utils::compare_str
#include "libbotanik/vars/scalar.h" // Vars::Scalar + format/parse
#include "doctest.h"
#include "libbotanik/vars/declare.h"
#include "libbotanik/vars/model.h"  // Vars::Bank / Var / VarRef
#include <cstdio>                       // snprintf
#include <cstdlib>                      // setenv / unsetenv
#include <unistd.h>                     // unlink
#include "libbotanik/hal/storage.h"     // HAL::Storage
#include "libbotanik/vars/access.h"     // Vars::var_format
#include "libbotanik/vars/params.h"     // Vars::Params / KvParamStore
#include <cstring>
#include <map>
#include <string>

// --------------------------------------------------------------------------
// scalar
// --------------------------------------------------------------------------

namespace scalar {

using namespace Botanik;
using namespace Botanik::Vars;

namespace {
    const char* ups_str(uint64_t v) {
        switch (v) { case 1: return "ONLINE"; case 2: return "BATTERY"; }
        return nullptr;          // unmapped: the number should show through
    }

}

TEST_CASE("scalar: a real parse refuses nan, inf and anything past float range") {
    // strtod accepts "nan"/"inf" outright and 1e300 becomes +inf on the narrowing
    // cast - none set ERANGE. Reachable via `override <row> nan`, which persists
    // and survives reject_implausible() (it skips overridden rows).
    Scalar out;
    CHECK(parse_scalar(T_F32, "nan",   &out) == PARSE_MALFORMED);
    CHECK(parse_scalar(T_F32, "NaN",   &out) == PARSE_MALFORMED);
    CHECK(parse_scalar(T_F32, "inf",   &out) == PARSE_RANGE);
    CHECK(parse_scalar(T_F32, "-inf",  &out) == PARSE_RANGE);
    CHECK(parse_scalar(T_F32, "1e300", &out) == PARSE_RANGE);   // fits double, not float
    CHECK(parse_scalar(T_F32, "1e400", &out) == PARSE_RANGE);   // ERANGE, as before

    // Ordinary values are untouched, including the float extremes.
    REQUIRE(parse_scalar(T_F32, "42.5", &out) == PARSE_OK);
    CHECK(out.f == doctest::Approx(42.5f));
    CHECK(parse_scalar(T_F32, "3.4e38",  &out) == PARSE_OK);
    CHECK(parse_scalar(T_F32, "-3.4e38", &out) == PARSE_OK);
}

TEST_CASE("scalar: every type round-trips through its own slot") {
    // Each write lands in the union member the type names, not a neighbour's.
    CHECK(as_f32(T_F32, from_f32(T_F32, 20.5f)) == doctest::Approx(20.5f));
    CHECK(as_i64(T_U16, from_i64(T_U16, 3600)) == 3600);
    CHECK(as_i64(T_I32, from_i64(T_I32, -5000)) == -5000);
    CHECK(as_i64(T_I64, from_i64(T_I64, INT64_MIN)) == INT64_MIN);
    CHECK(as_bool(T_BOOL, from_bool(T_BOOL, true)));
    CHECK_FALSE(as_bool(T_BOOL, from_bool(T_BOOL, false)));

    // A u64 past the signed range survives: it is stored and read as unsigned.
    const uint64_t big = 0xFFFFFFFFFFFFFF00ull;
    CHECK(from_i64(T_U64, 0).u == 0);
    Scalar s = Scalar::U(big);
    CHECK(s.u == big);
    CHECK(as_f32(T_U64, s) > 0.0f);          // not negative, as a signed read would be
}

TEST_CASE("scalar: a write is coerced into the slot, not reinterpreted") {
    // A float written to an integer must round, not have its bits reinterpreted.
    CHECK(as_i64(T_U16, from_f32(T_U16, 63.6f)) == 64);
    CHECK(as_i64(T_I16, from_f32(T_I16, -63.6f)) == -64);
    CHECK(as_i64(T_U16, from_f32(T_U16, -5.0f)) == 0);   // clamps, does not wrap
    CHECK(as_i64(T_U32, from_i64(T_U32, -1)) == 0);
    // An integer written to a real variable keeps its value.
    CHECK(as_f32(T_F32, from_i64(T_F32, 42)) == doctest::Approx(42.0f));
}

TEST_CASE("scalar: digitals read as 0/1 so consumers need not branch") {
    CHECK(as_f32(T_BOOL, Scalar::B(true)) == doctest::Approx(1.0f));
    CHECK(as_f32(T_BOOL, Scalar::B(false)) == doctest::Approx(0.0f));
}

TEST_CASE("scalar: bounds compare in the variable's own type") {
    // A fractional band: int32 bounds would truncate 0.5..2.5 to 0..2.
    CHECK(in_range(T_F32, Scalar::F(-90.5f), Scalar::F(-90.5f), Scalar::F(90.5f)));
    CHECK_FALSE(in_range(T_F32, Scalar::F(-90.6f), Scalar::F(-90.5f), Scalar::F(90.5f)));
    CHECK(in_range(T_U16, Scalar::U(3600), Scalar::U(0), Scalar::U(3600)));
    CHECK_FALSE(in_range(T_U16, Scalar::U(3601), Scalar::U(0), Scalar::U(3600)));
    // Signed compares as signed: -1 is below 0, not a huge unsigned.
    CHECK_FALSE(in_range(T_I32, Scalar::I(-1), Scalar::I(0), Scalar::I(10)));
}

TEST_CASE("scalar: rendering follows the type") {
    char b[48];
    CHECK(Utils::compare_str(format_scalar(T_F32, Scalar::F(31.75f), 1, nullptr, b, sizeof(b)), "31.8"));
    CHECK(Utils::compare_str(format_scalar(T_F32, Scalar::F(31.75f), 0, nullptr, b, sizeof(b)), "32"));
    CHECK(Utils::compare_str(format_scalar(T_U16, Scalar::U(3600), 0, nullptr, b, sizeof(b)), "3600"));
    CHECK(Utils::compare_str(format_scalar(T_I32, Scalar::I(-5), 0, nullptr, b, sizeof(b)), "-5"));
    CHECK(Utils::compare_str(format_scalar(T_BOOL, Scalar::B(true), 0, nullptr, b, sizeof(b)), "1"));
    // An enum converter wins over the number...
    CHECK(Utils::compare_str(format_scalar(T_ENUM, Scalar::U(2), 0, &ups_str, b, sizeof(b)), "BATTERY"));
    // ...but an unmapped code still shows, rather than rendering as nothing.
    CHECK(Utils::compare_str(format_scalar(T_ENUM, Scalar::U(9), 0, &ups_str, b, sizeof(b)), "9"));
}

TEST_CASE("scalar: parsing rejects junk instead of taking a plausible prefix") {
    Scalar v;
    CHECK(parse_scalar(T_U16, "3600", &v) == PARSE_OK);
    CHECK(v.u == 3600);
    CHECK(parse_scalar(T_F32, "-40.5", &v) == PARSE_OK);
    CHECK(v.f == doctest::Approx(-40.5f));
    CHECK(parse_scalar(T_I32, "-5000", &v) == PARSE_OK);
    CHECK(v.i == -5000);
    CHECK(parse_scalar(T_U16, " 42 ", &v) == PARSE_OK);   // surrounding spaces are fine

    CHECK(parse_scalar(T_U16, "12abc", &v) == PARSE_MALFORMED);
    CHECK(parse_scalar(T_U16, "", &v)      == PARSE_MALFORMED);
    CHECK(parse_scalar(T_U16, nullptr, &v) == PARSE_MALFORMED);
    CHECK(parse_scalar(T_F32, "1.2.3", &v) == PARSE_MALFORMED);
    // A string is not a scalar.
    CHECK(parse_scalar(T_STR, "hello", &v) == PARSE_MALFORMED);
}

TEST_CASE("scalar: parsing enforces the type's own range") {
    Scalar v;
    CHECK(parse_scalar(T_U16, "65535", &v) == PARSE_OK);
    CHECK(parse_scalar(T_U16, "65536", &v) == PARSE_RANGE);
    CHECK(parse_scalar(T_I16, "32767", &v) == PARSE_OK);
    CHECK(parse_scalar(T_I16, "32768", &v) == PARSE_RANGE);
    CHECK(parse_scalar(T_I16, "-32768", &v) == PARSE_OK);
    // strtoull wraps a negative into a huge positive; that must not read as a
    // valid u16 of 65535.
    CHECK(parse_scalar(T_U16, "-1", &v) == PARSE_RANGE);
    CHECK(parse_scalar(T_BOOL, "2", &v) == PARSE_RANGE);
    CHECK(parse_scalar(T_BOOL, "1", &v) == PARSE_OK);
}

TEST_CASE("scalar: string validation by kind") {
    CHECK(validate_str(STR_IPV4, "192.168.1.10", 64));
    CHECK_FALSE(validate_str(STR_IPV4, "192.168.1", 64));
    CHECK_FALSE(validate_str(STR_IPV4, "999.1.1.1", 64));
    CHECK_FALSE(validate_str(STR_IPV4, "", 64));
    CHECK(validate_str(STR_IPV4_OPT, "", 64));          // empty means "none"
    CHECK(validate_str(STR_HOST, "ntp.example.org", 64));
    CHECK_FALSE(validate_str(STR_HOST, "bad host", 64));
    CHECK_FALSE(validate_str(STR_HOST, "ntp.example.org", 8));   // must fit incl NUL
    CHECK(validate_str(STR_NONE, "anything", 64));
    CHECK_FALSE(validate_str(STR_NONE, "toolongforthecap", 8));
}

TEST_CASE("parse: a literal too big for the type is a range error, not a maximum") {
    // strto* SATURATE rather than fail: in_range must catch an overflow even at
    // the widest types, not read it as a plausible maximum.
    Scalar out;
    CHECK(parse_scalar(T_U64, "99999999999999999999999", &out) == PARSE_RANGE);
    CHECK(parse_scalar(T_I64, "-99999999999999999999999", &out) == PARSE_RANGE);
    CHECK(parse_scalar(T_F32, "1e400", &out) == PARSE_RANGE);
    // And the ordinary cases still parse.
    CHECK(parse_scalar(T_U64, "12345", &out) == PARSE_OK);
    CHECK(out.u == 12345u);
}

}  // namespace scalar

// --------------------------------------------------------------------------
// model
// --------------------------------------------------------------------------

namespace model {

using namespace Botanik;
using namespace Botanik::Vars;

namespace {
    enum Id : uint16_t {
        V_EXT_TEMP, V_NO_TIMEOUT, V_VALVE, V_ALARM_HOT, V_SETPOINT,
        V_NTP_POLL, V_NTP_SERVER, V_COUNT
    };

    // One table, every scope - the shape a site has. Each row's factory takes
    // only the fields its scope can use.
    const Variable VARS[] = {
        MI("ext_temp",    T_F32, 5, 3, 18, 2, SF(-40.0f), SF(85.0f), SF(20.5f), 5000, 1,
                          "C", "outside", ""),
        MI("no_timeout",  T_F32, 5, 3, 18, 2, SF(0.0f), SF(100.0f), SF(1.5f), 0, 1,
                          "C", "", ""),
        RO("valve",       31, 0, true, false, "drain valve", ""),
        A("alarm_hot",    "HOT", 2, 2, 0, 0, 0, 800, "over temperature", ""),
        G ("setpoint",    T_F32, SF(18.0f), 1, "C", "target temp", ""),
        P("ntp_poll_s",   T_U16, 64, 0, 3600, 0, "s", 1, "NTP poll interval", ""),
        PS("ntp_server1", "ntp.example.org", 32, STR_HOST, 2, "primary NTP server", ""),
    };

    struct Fixture {
        Value    values[V_COUNT]{};
        char     pool[64]{};
        uint16_t index[V_COUNT]{};
        Bank     bank;
        Fixture() {
            VarTables t = {};
            t.var = VARS; t.val = values; t.count = V_COUNT;
            t.pool = pool; t.pool_bytes = sizeof(pool);
            t.index = index;
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

TEST_CASE("model: invalid records WHY, not just that") {
    // Three different things invalidate a reading, each wanting a different
    // next move - a bare valid/invalid cannot tell them apart.
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);
    CHECK(h.live_quality() == Q_NO_DATA);          // seeded, nothing written

    h.publish(Scalar::F(20.0f), 1000);
    CHECK(h.live_quality() == Q_GOOD);

    f.bank.expire_stale(1000000);                  // well past its timeout
    CHECK(h.live_quality() == Q_STALE);

    h.publish(Scalar::F(999.0f), 2000);            // band is -40..85
    CHECK(f.bank.reject_implausible() == 1);
    CHECK(h.live_quality() == Q_IMPLAUSIBLE);

    f.bank.set_health_hold(false);   // a 4th: safe-state, not a fault
    Var out = f.bank.var(V_VALVE);
    out.set(true);
    CHECK(out.live_quality() == Q_GOOD);
    f.bank.set_health_hold(true);
    CHECK(out.live_quality() == Q_SAFE);

    // All four still read as invalid through the single bool.
    CHECK_FALSE(out.valid());
    CHECK_FALSE(h.valid());
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
    // ...and it KEEPS the reading: a moment-old sensor still knows more than
    // the 20.5 default. valid() is false either way.
    CHECK(f.bank.var(V_EXT_TEMP).value() == doctest::Approx(30.0f));
}

TEST_CASE("model: live() and raw() agree about a stale reading") {
    // live() and raw() must agree: MONITOR's live column and `get`'s Raw row
    // read one each, and a dying sensor is exactly when both get checked.
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);
    h.publish(Scalar::F(30.0f), 1000);
    f.bank.expire_stale(7000);                       // past the 5 s timeout
    REQUIRE(h.live_quality() == Q_STALE);

    CHECK(h.raw().f  == doctest::Approx(30.0f));
    CHECK(h.live().f == doctest::Approx(30.0f));     // not the 20.5 default
    CHECK_FALSE(h.valid());                          // still not to be trusted

    Var g = f.bank.var(V_EXT_TEMP);   // nothing to keep: both fall back together
    g.invalidate(Q_IMPLAUSIBLE);
    CHECK(g.raw().f  == doctest::Approx(20.5f));
    CHECK(g.live().f == doctest::Approx(20.5f));
}

TEST_CASE("model: a row that never read anything still falls back to its default") {
    // The other half: Q_STALE keeps its value because there is one, but
    // Q_NO_DATA, Q_REFUSED and Q_IMPLAUSIBLE have nothing to keep.
    Fixture f;
    CHECK(f.bank.var(V_EXT_TEMP).live_quality() == Q_NO_DATA);
    CHECK(f.bank.var(V_EXT_TEMP).value() == doctest::Approx(20.5f));

    f.bank.var(V_EXT_TEMP).publish(Scalar::F(30.0f), 1000);
    f.bank.var(V_EXT_TEMP).invalidate(Q_IMPLAUSIBLE);
    INFO("an implausible reading IS a bad number - keeping it would hand a "
         "control program the rail voltage it was rejected for");
    CHECK(f.bank.var(V_EXT_TEMP).value() == doctest::Approx(20.5f));

    f.bank.var(V_EXT_TEMP).invalidate(Q_REFUSED);
    INFO("and a refused register means the slot answered a different question");
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
    f.bank.set_health_hold(false);
    f.bank.var(V_VALVE).set(false);
    f.bank.var(V_ALARM_HOT).set(true);
    f.bank.var(V_SETPOINT).set(21.5f);
    f.bank.var(V_NTP_POLL).set_i(120);

    f.bank.set_control_mode(CONTROL_INHIBITED);

    CHECK_FALSE(f.bank.control_enabled());
    CHECK_FALSE(f.bank.var(V_VALVE).valid());
    CHECK(f.bank.var(V_VALVE).b());                     // dropped to its safe def
    CHECK(f.bank.var(V_ALARM_HOT).b());                 // alarm survives: keep the diagnosis
    CHECK(f.bank.var(V_SETPOINT).value() == doctest::Approx(21.5f));
    CHECK(f.bank.var(V_NTP_POLL).i() == 120);           // config survives too
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
    // A shell argument is a span, not a NUL-terminated string: no prefix matches.
    CHECK(f.bank.find("ext_temp_extra", 8).index == V_EXT_TEMP);
    CHECK(f.bank.find("ext", 3).index == VAR_NONE);
}

TEST_CASE("model: a bad index resolves to an inert sink, never a clamp") {
    // NOT clamped to the last entry: that would alias a typo onto a real signal.
    Fixture f;
    CHECK(f.bank.var((uint16_t)999).value() == doctest::Approx(0.0f));
    CHECK(Utils::compare_str(f.bank.var((uint16_t)999).name(), "<none>"));

    f.bank.var(V_VALVE).set(true);
    f.bank.var((uint16_t)999).set(false);      // absorbed, drives nothing
    CHECK(f.bank.var(V_VALVE).b());            // the real coil is untouched

    // VAR_NONE: same story, so a platform alarm spells "no variable" without
    // aliasing row 0.
    VarRef none = { VAR_NONE };
    CHECK_FALSE(f.bank.var(none).valid());
    CHECK(f.bank.var(none).value() == doctest::Approx(0.0f));
}

TEST_CASE("model: an empty table is safe to index and sweep") {
    VarTables t = {};
    Bank b(t);
    b.init();
    CHECK(b.count() == 0);
    CHECK(b.var((uint16_t)0).value() == doctest::Approx(0.0f));
    b.var((uint16_t)0).set(50.0f);             // absorbed
    b.set_control_mode(CONTROL_INHIBITED);                       // and sweeps cleanly
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

    f.bank.override_set(h, Scalar::F(35.0f));
    CHECK(h.overridden());
    CHECK(h.value() == doctest::Approx(35.0f));
    CHECK(h.valid());

    // The driver keeps publishing underneath; the override still wins...
    h.publish(Scalar::F(21.0f), 2000);
    CHECK(h.value() == doctest::Approx(35.0f));

    // ...and clearing it resumes the live reading, with nothing to restore.
    f.bank.override_clear(h);
    CHECK_FALSE(h.overridden());
    CHECK(h.value() == doctest::Approx(21.0f));
}

TEST_CASE("override: can force the quality, not just the value") {
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);
    h.publish(Scalar::F(20.0f), 1000);

    f.bank.override_set(h, Scalar::F(20.0f), /*valid=*/false);
    CHECK(h.overridden());
    CHECK_FALSE(h.valid());                        // reads as a dead sensor...
    CHECK(h.value() == doctest::Approx(20.0f));    // ...at a known value
}

TEST_CASE("override: freezing pins the value the variable had at that moment") {
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);
    h.publish(Scalar::F(7.5f), 1000);

    f.bank.override_freeze(h);
    h.publish(Scalar::F(99.0f), 2000);
    CHECK(h.value() == doctest::Approx(7.5f));
}

TEST_CASE("override: an expired input still reads its forced value") {
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);
    h.publish(Scalar::F(20.0f), 1000);
    f.bank.override_set(h, Scalar::F(20.0f));

    // Well past the declared timeout: the underlying slot expires, but what a
    // reader sees is the override - which is the whole point of forcing one.
    f.bank.expire_stale(1000000);
    CHECK(h.valid());
    CHECK(h.value() == doctest::Approx(20.0f));
}

TEST_CASE("control: the operator's inhibit keeps a forced output, the system's takes it") {
    Fixture f;
    f.bank.set_health_hold(false);   // bring-up finished
    REQUIRE(f.bank.control_enabled());
    Var h = f.bank.var(V_VALVE);
    h.set(true);
    f.bank.override_set(h, Scalar::B(true));

    // `control disable` takes the plant by hand: an override is the only thing
    // left that can move an actuator, and must still be TAKEN in that state.
    f.bank.set_control_mode(CONTROL_INHIBITED);
    CHECK(h.overridden());
    f.bank.override_clear(h);
    CHECK(f.bank.override_set(h, "1") == SET_OK);

    // The system's hold must fire even with the operator's already in force,
    // or a "control already off" early exit lets a hand-held actuator ride
    // through a health fault.
    f.bank.set_health_hold(true);
    CHECK_FALSE(h.overridden());
    CHECK_FALSE(h.valid());
    CHECK(h.b() == true);      // reads its declared safe state (def = true here)

    Var in = f.bank.var(V_EXT_TEMP);   // an input's override is untouched either way
    f.bank.override_set(in, Scalar::F(5.0f));
    f.bank.set_health_hold(true);
    CHECK(in.overridden());
}

TEST_CASE("control: starts disabled, and an output is forceable while it is") {
    Fixture f;
    // The system holds control until bring-up says it's ready; an override is
    // live immediately, not staged, so there is nothing to refuse.
    CHECK_FALSE(f.bank.control_enabled());
    CHECK(f.bank.health_hold());
    CHECK(f.bank.override_set(f.bank.var(V_VALVE), "1") == SET_OK);
}

TEST_CASE("control: an operator disable survives a health fault coming and going") {
    Fixture f;
    f.bank.set_health_hold(false);      // bring-up finished
    CHECK(f.bank.control_enabled());

    f.bank.set_control_mode(CONTROL_INHIBITED);   // someone at the console
    CHECK_FALSE(f.bank.control_enabled());

    // Health releasing its own hold must not hand control back too.
    f.bank.set_health_hold(true);
    f.bank.set_health_hold(false);
    CHECK_FALSE(f.bank.control_enabled());
    CHECK(f.bank.operator_mode() != CONTROL_ENABLED);
    CHECK_FALSE(f.bank.health_hold());

    // Only they can give it back.
    f.bank.set_control_mode(CONTROL_ENABLED);
    CHECK(f.bank.control_enabled());
}

// The three modes differ ONLY in what happens to an output override; both
// withheld modes drop the outputs to safe and stop the scan.
TEST_CASE("control: inhibit keeps an output override, disable mutes it") {
    Fixture f;
    f.bank.set_health_hold(false);
    Var out = f.bank.var(V_VALVE);
    out.set(false);                                  // def (safe) is true here
    f.bank.override_set(out, Scalar::B(false));
    REQUIRE(out.overridden());
    REQUIRE(out.b() == false);                       // the forced value drives

    // INHIBITED: programs stopped, but the hand still reaches the actuator.
    f.bank.set_control_mode(CONTROL_INHIBITED);
    CHECK(f.bank.control_mode() == CONTROL_INHIBITED);
    CHECK(out.overridden());
    CHECK(out.b() == false);                         // still the forced value

    // DISABLED: the override is muted, so the row reads its safe state - but it
    // is KEPT, which is the whole difference from the system's withdrawal.
    f.bank.set_control_mode(CONTROL_DISABLED);
    CHECK(out.b() == true);                          // def, not the forced false
    CHECK_FALSE(out.valid());
    CHECK(out.overridden());                         // not erased

    // ...and it comes back on the way out, without being re-entered.
    f.bank.set_control_mode(CONTROL_INHIBITED);
    CHECK(out.b() == false);
}

// Muting and unmuting both change what the field is owed, so both must mark the
// row - an unmuted override that nothing carries out leaves the actuator where
// the safe state put it.
TEST_CASE("control: crossing the mute edge marks the output for the field") {
    Fixture f;
    f.bank.set_health_hold(false);
    Var out = f.bank.var(V_VALVE);
    f.bank.override_set(out, Scalar::B(false));

    f.bank.set_control_mode(CONTROL_DISABLED);
    CHECK(out.field_dirty());
    out.clear_field_dirty();

    f.bank.set_control_mode(CONTROL_ENABLED);
    CHECK(out.field_dirty());
}

// Staging a hand-drive before enabling is a real workflow, so the write is
// stored rather than refused - it simply does not reach the field yet.
TEST_CASE("control: an override forced while disabled is kept, muted, and lands later") {
    Fixture f;
    f.bank.set_health_hold(false);
    f.bank.set_control_mode(CONTROL_DISABLED);

    Var out = f.bank.var(V_VALVE);
    CHECK(f.bank.override_set(out, "0") == SET_OK);   // the text overload reports
    CHECK(out.overridden());
    CHECK(out.b() == true);                          // muted: reads its safe state

    f.bank.set_control_mode(CONTROL_ENABLED);
    CHECK(out.b() == false);                         // now it drives
}

// The operator's choice and the system's hold are independent, and the STRICTER
// one is what takes effect.
TEST_CASE("control: the effective mode is the stricter of the two sources") {
    Fixture f;
    f.bank.set_health_hold(false);
    f.bank.set_control_mode(CONTROL_INHIBITED);
    CHECK(f.bank.control_mode() == CONTROL_INHIBITED);

    // The system can only make it stricter, never looser...
    f.bank.set_health_hold(true);
    CHECK(f.bank.control_mode() == CONTROL_DISABLED);
    CHECK(f.bank.operator_mode() == CONTROL_INHIBITED);   // ...and remembers

    f.bank.set_health_hold(false);
    CHECK(f.bank.control_mode() == CONTROL_INHIBITED);

    // ...and an ENABLED operator under a system hold is still disabled.
    f.bank.set_control_mode(CONTROL_ENABLED);
    f.bank.set_health_hold(true);
    CHECK(f.bank.control_mode() == CONTROL_DISABLED);
}

// A muted override is an OUTPUT rule. Muting a forced sensor would erase the
// diagnosis the operator forced it to get.
TEST_CASE("control: disable does not mute an input's override") {
    Fixture f;
    f.bank.set_health_hold(false);
    Var in = f.bank.var(V_EXT_TEMP);
    f.bank.override_set(in, Scalar::F(35.0f));

    f.bank.set_control_mode(CONTROL_DISABLED);
    CHECK(in.overridden());
    CHECK(in.value() == doctest::Approx(35.0f));
    CHECK(in.valid());
}

TEST_CASE("control: an operator enable cannot override a health fault either") {
    Fixture f;
    f.bank.set_health_hold(false);
    f.bank.set_health_hold(true);     // health faulted

    // The console must not talk over the firmware's own judgement.
    f.bank.set_control_mode(CONTROL_ENABLED);
    CHECK_FALSE(f.bank.control_enabled());
    CHECK(f.bank.health_hold());
}

TEST_CASE("control: a second hold arriving does not re-sweep") {
    Fixture f;
    f.bank.set_health_hold(false);
    Var in = f.bank.var(V_EXT_TEMP);
    f.bank.set_control_mode(CONTROL_INHIBITED);

    f.bank.override_set(in, Scalar::F(5.0f));   // sweep already ran; survives piling on
    f.bank.set_health_hold(true);
    CHECK(in.overridden());
}

TEST_CASE("override: init and clear_overrides drop every one of them") {
    Fixture f;
    f.bank.override_set(f.bank.var(V_EXT_TEMP), Scalar::F(1.0f));
    f.bank.override_set(f.bank.var(V_VALVE), Scalar::B(true));
    CHECK(f.bank.override_count() == 2);

    CHECK(f.bank.clear_overrides() == 2);
    CHECK(f.bank.override_count() == 0);

    // A reboot re-seeds the table: an override must never be waiting there.
    f.bank.override_set(f.bank.var(V_EXT_TEMP), Scalar::F(1.0f));
    f.bank.init();
    CHECK(f.bank.override_count() == 0);
}

TEST_CASE("override: parses text, and deliberately ignores the plausibility band") {
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);          // band -40..85

    CHECK(f.bank.override_set(h, "12.5") == SET_OK);
    CHECK(h.value() == doctest::Approx(12.5f));

    // Out of band on purpose - see "plausibility: an override is exempt" below.
    CHECK(f.bank.override_set(h, "999") == SET_OK);
    CHECK(h.value() == doctest::Approx(999.0f));

    CHECK(f.bank.override_set(h, "not-a-number") == SET_INVALID);
    // A string's live text lives in the pool, so there is nothing to shadow.
    CHECK(f.bank.override_set(f.bank.var(V_NTP_SERVER), "host") == SET_INVALID);
}

TEST_CASE("set: refuses anything that is not a parameter") {
    Fixture f;
    // set() is the EDIT half only: it applies and marks the row, and reports OK
    // whether or not a store is even bound. Committing is save()'s job.
    CHECK(f.bank.set(f.bank.var(V_NTP_POLL), "120") == SET_OK);
    CHECK(f.bank.var(V_NTP_POLL).i() == 120);
    CHECK(f.bank.var(V_NTP_POLL).param_unsaved());
    CHECK(f.bank.unsaved_count() == 1);
    // No store bound: the commit is what fails, not the edit.
    CHECK(f.bank.save(f.bank.var(V_NTP_POLL)) == SET_NOT_PERSISTED);
    CHECK(f.bank.var(V_NTP_POLL).param_unsaved());   // still owed

    CHECK(f.bank.set(f.bank.var(V_EXT_TEMP), "20") == SET_INVALID);   // input
    CHECK(f.bank.set(f.bank.var(V_VALVE), "1")     == SET_INVALID);   // output
    CHECK(f.bank.set(f.bank.var(V_SETPOINT), "20") == SET_INVALID);   // internal
}

TEST_CASE("set: holds a parameter to its declared band") {
    Fixture f;
    CHECK(f.bank.set(f.bank.var(V_NTP_POLL), "3601") == SET_INVALID);  // max 3600
    CHECK(f.bank.var(V_NTP_POLL).i() == 64);                           // untouched
}

// ---- String pool: a row that does not fit must not land on another ----------

TEST_CASE("pool: a row that does not fit gets no slot, not row 0's") {
    // Two 32-byte slots in a 40-byte pool: the second must not fall back to
    // offset 0, or every writer would overwrite the FIRST row's slot.
    const Variable V[] = {
        PS("a", "first", 32, STR_NONE, 1, "", ""),
        PS("b", "second", 32, STR_NONE, 2, "", ""),
    };
    Value vals[2]{};
    char  pool[40]{};
    VarTables t = {};
    t.var = V; t.val = vals; t.count = 2;
    t.pool = pool; t.pool_bytes = sizeof(pool);
    Bank bank(t);
    bank.init();

    CHECK(bank.pool_overflow() == 1);
    CHECK(vals[1].str_off == STR_NO_SLOT);
    // The row that did fit still reads its own default, unclobbered.
    CHECK(bank.text_at(bank.var((uint16_t)0)) != nullptr);
    CHECK(std::strcmp(bank.text_at(bank.var((uint16_t)0)), "first") == 0);
    // The row that did not gets no pointer at all, so nothing can write it.
    CHECK(bank.text_at(bank.var((uint16_t)1)) == nullptr);
    CHECK(bank.set(bank.var((uint16_t)1), "anything") == SET_INVALID);
    CHECK(std::strcmp(bank.text_at(bank.var((uint16_t)0)), "first") == 0);
}

TEST_CASE("pool: a pool smaller than a single slot writes nothing") {
    // Out-of-bounds: offset 0 + a 32-byte capacity into 8 bytes. ASan is what
    // makes this test worth having.
    const Variable V[] = { PS("a", "aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 32, STR_NONE, 1, "", "") };
    Value vals[1]{};
    char  pool[8]{};
    VarTables t = {};
    t.var = V; t.val = vals; t.count = 1;
    t.pool = pool; t.pool_bytes = sizeof(pool);
    Bank bank(t);
    bank.init();

    CHECK(bank.pool_overflow() == 1);
    CHECK(bank.text_at(bank.var((uint16_t)0)) == nullptr);
    CHECK(bank.set(bank.var((uint16_t)0), "x") == SET_INVALID);
}

// ---- Implausible readings --------------------------------------------------

TEST_CASE("plausibility: a reading outside its declared band goes invalid") {
    // What F_BOUNDED is for: a shorted 4-20 mA loop reads full scale, and no
    // timeout catches that.
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);          // band -40..85, def 20.5

    h.publish(Scalar::F(20.0f), 1000);
    CHECK(f.bank.reject_implausible() == 0);
    CHECK(h.valid());

    h.publish(Scalar::F(500.0f), 2000);      // fresh, well-formed, impossible
    CHECK(f.bank.reject_implausible() == 1);
    CHECK_FALSE(h.valid());
    CHECK(h.value() == doctest::Approx(20.5f));   // falls back to `def`
}

TEST_CASE("plausibility: a non-finite reading is a failed read") {
    // A NaN compares false against everything, so it trips no band on its own.
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);
    const float nan = 0.0f / 0.0f, inf = 1.0f / 0.0f;

    h.publish(Scalar::F(nan), 1000);
    CHECK(f.bank.reject_implausible() == 1);
    CHECK_FALSE(h.valid());

    h.publish(Scalar::F(inf), 2000);
    CHECK(f.bank.reject_implausible() == 1);
    CHECK_FALSE(h.valid());
}

TEST_CASE("plausibility: an override is exempt") {
    // Pushing a reading past its band is precisely what an override is for -
    // it is how you exercise the alarm watching for one.
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);
    h.publish(Scalar::F(20.0f), 1000);
    f.bank.override_set(h, Scalar::F(9999.0f));

    CHECK(f.bank.reject_implausible() == 0);
    CHECK(h.valid());
    CHECK(h.value() == doctest::Approx(9999.0f));
}

TEST_CASE("plausibility: an unbounded row is left alone") {
    // No F_BOUNDED: min/max are not a band, so there is nothing to judge.
    Fixture f;
    Var h = f.bank.var(V_NO_TIMEOUT);        // bounded 0..100
    Var g = f.bank.var(V_SETPOINT);          // ROLE_COMPUTED: not an input
    g.set(1e6f);
    h.publish(Scalar::F(50.0f), 1000);
    CHECK(f.bank.reject_implausible() == 0);
    CHECK(g.valid());                        // computed rows are never swept
}

}  // namespace model

// --------------------------------------------------------------------------
// params
// --------------------------------------------------------------------------

namespace params {

using namespace Botanik;
using namespace Botanik::Vars;

namespace {
    const uint16_t CAP = 32;

    const Variable PARAMS[] = {
        //name      type    default   min    max   dgt unit   reg
        P("p_bool", T_BOOL, 1,        0,     1,    0,  "",    1, "", ""),
        P("p_u16",  T_U16,  64,       0,     3600, 0,  "",    2, "", ""),
        P("p_i32",  T_I32,  -7,       -5000, 5000, 0,  "",    3, "", ""),
        // A fractional band, which int32 bounds could not express at all.
        P("p_f32",  T_F32,  48.8566,  -90,   90,   4,  "deg", 4, "", ""),
        PS("p_str", "pool.ntp.org", CAP, STR_HOST, 5, "", ""),
    };

    struct StoreEnv {
        char path[64];
        explicit StoreEnv(const char* where = nullptr) {
            if (where) snprintf(path, sizeof(path), "%s", where);
            else       snprintf(path, sizeof(path), "/tmp/botanik_test_prm_%ld", (long)getpid());
            setenv("BOTANIK_CONFIG_STORE", path, 1);
            unlink(path);
        }
        ~StoreEnv() { unlink(path); unsetenv("BOTANIK_CONFIG_STORE"); }
    };

    struct Fx {
        Value        vals[5]{};
        char         pool[CAP]{};
        uint16_t     index[5]{};
        Bank         bank;
        KvParamStore store;
        Params       params;
        Fx() : params(&bank, &store, nullptr) {
            VarTables t = {};
            t.var = PARAMS; t.val = vals; t.count = 5; t.index = index;
            t.pool = pool; t.pool_bytes = CAP;
            bank = Bank(t);
            bank.init();
            params = Params(&bank, &store, nullptr);
        }
        Var by(const char* n) { return bank.var(bank.find(n)); }
    };
}

TEST_CASE("params: a parameter starts at its factory default, and valid") {
    StoreEnv env;
    Fx f;
    CHECK(f.by("p_u16").i() == 64);
    CHECK(f.by("p_u16").valid());
    CHECK(f.by("p_f32").value() == doctest::Approx(48.8566f));
    CHECK(Utils::compare_str(get_str(&f.bank, "p_str"), "pool.ntp.org"));
}

TEST_CASE("params: an edit applies, saves, and survives a reload") {
    StoreEnv env;
    {
        Fx f;
        CHECK(f.params.set(f.by("p_u16"), "120") == SET_OK);
        CHECK(f.params.set(f.by("p_i32"), "-4200") == SET_OK);
        CHECK(f.params.set(f.by("p_f32"), "-12.25") == SET_OK);
        CHECK(f.params.set(f.by("p_bool"), "0") == SET_OK);
        CHECK(f.params.set(f.by("p_str"), "ntp.example.org") == SET_OK);
        // Live but not durable until this: five rows owed, five written.
        CHECK(f.params.unsaved() == 5);
        CHECK(f.params.save_all() == 5);
        CHECK(f.params.unsaved() == 0);
    }
    {   // a fresh bank, loading from the same store
        Fx f;
        f.params.step(0);           // first step runs setup(): the boot load
        CHECK(f.by("p_u16").i() == 120);
        CHECK(f.by("p_i32").i() == -4200);          // sign survives the narrow width
        CHECK(f.by("p_f32").value() == doctest::Approx(-12.25f));
        CHECK_FALSE(f.by("p_bool").b());
        CHECK(Utils::compare_str(get_str(&f.bank, "p_str"), "ntp.example.org"));
    }
}

TEST_CASE("params: a missing key keeps the declared default") {
    StoreEnv env;
    Fx f;
    f.params.step(0);
    CHECK(f.by("p_u16").i() == 64);
    CHECK(Utils::compare_str(get_str(&f.bank, "p_str"), "pool.ntp.org"));
}

TEST_CASE("params: stored values keep the type's NARROW width") {
    // Not sizeof(Scalar): a u16 must occupy 2 bytes, or existing records change
    // encoding and HAL::Storage::get starts rejecting them.
    StoreEnv env;
    Fx f;
    CHECK(f.params.set(f.by("p_u16"), "300") == SET_OK);
    CHECK(f.params.set(f.by("p_bool"), "0") == SET_OK);
    CHECK(f.params.set(f.by("p_f32"), "1.5") == SET_OK);
    CHECK(f.params.save_all() == 3);
    uint8_t buf[16]; size_t n = 0;
    REQUIRE(HAL::Storage::get("p_u16", buf, sizeof(buf), &n));
    CHECK(n == 2);
    REQUIRE(HAL::Storage::get("p_bool", buf, sizeof(buf), &n));
    CHECK(n == 1);
    REQUIRE(HAL::Storage::get("p_f32", buf, sizeof(buf), &n));
    CHECK(n == 4);
}

TEST_CASE("params: edits are bounds-checked in the parameter's own type") {
    StoreEnv env;
    Fx f;
    CHECK(f.params.set(f.by("p_u16"), "3601") == SET_INVALID);
    CHECK(f.by("p_u16").i() == 64);                 // unchanged
    CHECK(f.params.set(f.by("p_i32"), "-6000") == SET_INVALID);
    // A fractional bound is a real bound now: 90.5 is out, 89.5 is in.
    CHECK(f.params.set(f.by("p_f32"), "90.5") == SET_INVALID);
    CHECK(f.params.set(f.by("p_f32"), "89.5") == SET_OK);
    // ...and junk is junk.
    CHECK(f.params.set(f.by("p_u16"), "12abc") == SET_INVALID);
}

TEST_CASE("params: a string edit is validated by its declared kind") {
    StoreEnv env;
    Fx f;
    CHECK(f.params.set(f.by("p_str"), "ntp.example.org") == SET_OK);
    CHECK(f.params.set(f.by("p_str"), "bad host") == SET_INVALID);
    // Must fit the slot including the NUL.
    CHECK(f.params.set(f.by("p_str"), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == SET_INVALID);
    CHECK(Utils::compare_str(get_str(&f.bank, "p_str"), "ntp.example.org"));
}

TEST_CASE("params: a refused save leaves the value live and still owed") {
    // The tri-state exists for this: reporting "invalid" for a storage refusal
    // would send the operator to fix input that was never the problem. And a
    // row the store refused stays marked, so the next save tries it again.
    StoreEnv env("/nonexistent-dir-botanik-test/store");
    Fx f;
    CHECK(f.params.set(f.by("p_u16"), "120") == SET_OK);
    CHECK(f.by("p_u16").i() == 120);
    CHECK(f.params.save(f.by("p_u16")) == SET_NOT_PERSISTED);
    CHECK(f.by("p_u16").param_unsaved());
    uint16_t refused = 0;
    CHECK(f.params.save_all(&refused) == 0);
    CHECK(refused == 1);
    CHECK(f.params.set(f.by("p_u16"), "99999") == SET_INVALID);   // still invalid
}

TEST_CASE("params: by-name reads are null-safe and type-aware") {
    StoreEnv env;
    Fx f;
    CHECK(get_int(&f.bank, "p_u16", 7) == 64);
    CHECK(get_f32(&f.bank, "p_f32", 0.0f) == doctest::Approx(48.8566f));
    CHECK(get_int(&f.bank, "nope", 7) == 7);
    CHECK(get_int(nullptr, "p_u16", 7) == 7);
    CHECK(Utils::compare_str(get_str(&f.bank, "nope"), ""));
    // A non-string read as a string yields "", not the bytes of a number.
    CHECK(Utils::compare_str(get_str(&f.bank, "p_u16"), ""));
    // The commit serial is just the parameter named after the group.
    CHECK(group_serial(&f.bank, "nope") == 0);
}

TEST_CASE("params: find only matches parameters, not any variable") {
    StoreEnv env;
    Fx f;
    CHECK(f.params.find("p_u16", 5).index != VAR_NONE);
    CHECK(f.params.find("nope", 4).index == VAR_NONE);
}

// ---- Persistent overrides ---------------------------------------------------

namespace {
    // One row per role, so the output exclusion is testable.
    const Variable OVR_VARS[] = {
        MI("sensor", T_F32, 5, 3, 0, 2, SF(-50.0f), SF(150.0f), SF(1.5f), 5000, 1,
                     "C", "a reading", ""),
        G ("derived", T_F32, SF(0.0f), 1, "", "a computed value", ""),
        RO("coil",   31, 0, false, false, "an actuator", ""),
        P ("setting", T_U16, 64, 0, 3600, 0, "s", 1, "a parameter", ""),
    };

    /** @brief A bank over OVR_VARS, rebuilt to simulate a reboot: same store, fresh RAM. */
    struct OvrFx {
        Value        vals[4]{};
        uint16_t     index[4]{};
        Bank         bank;
        KvParamStore store;
        OvrFx() {
            VarTables t = {};
            t.var = OVR_VARS; t.val = vals; t.count = 4; t.index = index;
            bank = Bank(t);
            bank.set_store(&store, nullptr);
            bank.init();
            bank.set_health_hold(false);   // so an output is drivable
            bank.load_overrides();
        }
    };
}

// The store is read while the system still holds control (Params::on_setup runs
// inside _tasks.setup(), before the hold is released), so this is the one window
// the mute bit exists to cover: a restored OUTPUT override must not reach the
// field until control does.
TEST_CASE("override: an output restored at boot is muted until control returns") {
    StoreEnv env("/tmp/botanik_test_ovr_boot");
    {
        OvrFx f;   // control released, so the force is takeable
        REQUIRE(f.bank.override_set(f.bank.var("coil"), "1") == SET_OK);
        CHECK(f.bank.var("coil").b() == true);
        REQUIRE(f.bank.save_override(f.bank.var("coil")) == SET_OK);
    }
    {
        // Fresh RAM, same store, and the hold NOT released - as at bring-up.
        Value    vals[4]{};
        uint16_t index[4]{};
        VarTables t = {};
        t.var = OVR_VARS; t.val = vals; t.count = 4; t.index = index;
        Bank bank;
        KvParamStore store;
        bank = Bank(t);
        bank.set_store(&store, nullptr);
        bank.init();
        REQUIRE(bank.control_mode() == CONTROL_DISABLED);   // the system holds it
        bank.load_overrides();

        CHECK(bank.var("coil").overridden());   // restored...
        CHECK(bank.var("coil").b() == false);   // ...but muted: reads its safe state

        bank.set_health_hold(false);            // bring-up done
        CHECK(bank.var("coil").b() == true);    // and now it drives
    }
}

TEST_CASE("override: a forced value survives a reboot") {
    StoreEnv env("/tmp/botanik_test_ovr_a");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "42.5") == SET_OK);
        CHECK(f.bank.var("sensor").value() == doctest::Approx(42.5f));
        CHECK(f.bank.var("sensor").override_unsaved());
        REQUIRE(f.bank.save_override(f.bank.var("sensor")) == SET_OK);
        CHECK_FALSE(f.bank.var("sensor").override_unsaved());
    }
    {
        OvrFx f;   // fresh RAM, same store
        CHECK(f.bank.var("sensor").overridden());
        CHECK(f.bank.var("sensor").value() == doctest::Approx(42.5f));
        CHECK(f.bank.var("sensor").valid());
        CHECK(f.bank.override_count() == 1);
    }
}

TEST_CASE("override: the validity it reads as survives too") {
    StoreEnv env("/tmp/botanik_test_ovr_b");
    {
        OvrFx f;
        // Force it, and make it read as a dead sensor: two separate facts.
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "42.5", false) == SET_OK);
        CHECK_FALSE(f.bank.var("sensor").valid());
        REQUIRE(f.bank.save_override(f.bank.var("sensor")) == SET_OK);
    }
    {
        OvrFx f;
        CHECK(f.bank.var("sensor").overridden());
        CHECK_FALSE(f.bank.var("sensor").valid());
        // Forcing the QUALITY is separate from the value; both came back.
        CHECK(f.bank.var("sensor").value() == doctest::Approx(42.5f));
        f.bank.override_clear(f.bank.var("sensor"));   // clearing hands the live value back
        CHECK_FALSE(f.bank.var("sensor").overridden());
    }
}

TEST_CASE("override: releasing one stops it coming back") {
    StoreEnv env("/tmp/botanik_test_ovr_c");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("derived"), "7") == SET_OK);
        REQUIRE(f.bank.save_override(f.bank.var("derived")) == SET_OK);
    }
    {
        OvrFx f;
        REQUIRE(f.bank.var("derived").overridden());
        f.bank.override_clear(f.bank.var("derived"));
    }
    {
        OvrFx f;
        CHECK_FALSE(f.bank.var("derived").overridden());
        CHECK(f.bank.override_count() == 0);
    }
}

TEST_CASE("override: `override clear` clears the stored copies too") {
    StoreEnv env("/tmp/botanik_test_ovr_d");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "1") == SET_OK);
        REQUIRE(f.bank.override_set(f.bank.var("derived"), "2") == SET_OK);
        CHECK(f.bank.save_overrides() == 2);
        CHECK(f.bank.clear_overrides() == 2);
    }
    {
        OvrFx f;
        CHECK(f.bank.override_count() == 0);
    }
}

TEST_CASE("override: an actuator override persists like any other") {
    // An override means "a person has taken this by hand" on any row, so an
    // actuator's must persist across a reboot exactly like a sensor's.
    StoreEnv env("/tmp/botanik_test_ovr_e");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("coil"), "1") == SET_OK);
        REQUIRE(f.bank.save_override(f.bank.var("coil")) == SET_OK);
        CHECK(f.bank.var("coil").overridden());
    }
    {
        OvrFx f;
        CHECK(f.bank.var("coil").overridden());
        CHECK(f.bank.var("coil").b());
        CHECK(f.bank.override_count() == 1);
    }
    // A health withdrawal takes it away, stored copy included, for good.
    {
        OvrFx f;
        f.bank.set_health_hold(true);
        CHECK_FALSE(f.bank.var("coil").overridden());
    }
    {
        OvrFx f;
        CHECK_FALSE(f.bank.var("coil").overridden());
        CHECK(f.bank.override_count() == 0);
    }
}

TEST_CASE("override: the system withdrawing control wipes the stored copy") {
    // Otherwise a stored copy hands the override back at the next boot.
    StoreEnv env("/tmp/botanik_test_ovr_f");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "5") == SET_OK);
        REQUIRE(f.bank.save_override(f.bank.var("sensor")) == SET_OK);
        f.bank.set_health_hold(true);
        CHECK(f.bank.var("sensor").overridden());   // inputs: reading isn't driving
    }
    {
        OvrFx f;
        CHECK(f.bank.var("sensor").overridden());
    }
}

TEST_CASE("override: a parameter override shadows without disturbing the setting") {
    StoreEnv env("/tmp/botanik_test_ovr_g");
    {
        OvrFx f;
        REQUIRE(f.bank.set(f.bank.var("setting"), "100") == SET_OK);
        REQUIRE(f.bank.save(f.bank.var("setting")) == SET_OK);
        REQUIRE(f.bank.override_set(f.bank.var("setting"), "7") == SET_OK);
        REQUIRE(f.bank.save_override(f.bank.var("setting")) == SET_OK);
        CHECK(f.bank.var("setting").i() == 7);
    }
    {
        OvrFx f;
        f.bank.load_params();
        CHECK(f.bank.var("setting").i() == 7);         // the override still wins
        f.bank.override_clear(f.bank.var("setting"));
        CHECK(f.bank.var("setting").i() == 100);       // the setting was untouched
    }
}

TEST_CASE("override: forcing only the quality persists the quality") {
    // Forcing quality through the handle alone cannot reach the store; it must
    // persist or a reboot hands back the old quality.
    StoreEnv env("/tmp/botanik_test_ovr_q");
    {
        OvrFx f;
        f.bank.var("sensor").publish(Scalar::F(21.0f), 1000);
        REQUIRE(f.bank.var("sensor").valid());
        f.bank.override_freeze(f.bank.var("sensor"));
        f.bank.override_set(f.bank.var("sensor"), f.bank.var("sensor").raw(), false);
        REQUIRE(f.bank.save_override(f.bank.var("sensor")) == SET_OK);
        CHECK_FALSE(f.bank.var("sensor").valid());
    }
    {
        OvrFx f;
        CHECK(f.bank.var("sensor").overridden());
        CHECK_FALSE(f.bank.var("sensor").valid());
        CHECK(f.bank.var("sensor").value() == doctest::Approx(21.0f));
    }
}

TEST_CASE("override: an unsaved one does not survive a reboot") {
    // The default is deliberate: forcing a signal to chase a fault is the
    // common case, and it should die with the session rather than outlive the
    // person who set it. Committing is what says "leave this in place".
    StoreEnv env("/tmp/botanik_test_ovr_u");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "33") == SET_OK);
        CHECK(f.bank.var("sensor").overridden());
        CHECK(f.bank.var("sensor").override_unsaved());
    }
    {
        OvrFx f;
        CHECK_FALSE(f.bank.var("sensor").overridden());
        CHECK(f.bank.override_count() == 0);
    }
}

TEST_CASE("override: every release path also forgets the stored copy") {
    // A release must clear the stored copy too, not just RAM, or it comes back
    // at the next boot - why Var's override mutators are private to Bank.
    StoreEnv env("/tmp/botanik_test_ovr_r");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "9") == SET_OK);
        REQUIRE(f.bank.save_override(f.bank.var("sensor")) == SET_OK);
        f.bank.override_clear(f.bank.var("sensor"));
        CHECK_FALSE(f.bank.var("sensor").overridden());
    }
    {
        OvrFx f;
        CHECK_FALSE(f.bank.var("sensor").overridden());
        CHECK(f.bank.override_count() == 0);
    }
}

}  // namespace params

// --------------------------------------------------------------------------
// retain
// --------------------------------------------------------------------------

namespace retain {

using namespace Botanik;
using namespace Botanik::Vars;

namespace {

/** @brief A store in a std::map, standing in for the flash. */
struct MemStore : public VarStore {
    std::map<std::string, uint64_t> params, overrides, retained;
    std::map<std::string, bool>     ovr_valid;
    int saves = 0;                       ///< retained writes attempted
    bool refuse = false;                 ///< make the "storage said no" path testable

    bool load(const Variable& d, Value& v, char*, uint16_t) override {
        auto it = params.find(d.name);
        if (it == params.end()) return false;
        v.v = Scalar::U(it->second);
        return true;
    }
    bool save(const Variable& d, const Value& v, const char*, uint16_t) override {
        params[d.name] = v.v.u;
        return true;
    }
    bool load_override(const Variable& d, Value& v) override {
        auto it = overrides.find(d.name);
        if (it == overrides.end()) return false;
        v.ovr = Scalar::U(it->second);
        v.ovr_valid = ovr_valid[d.name];
        return true;
    }
    bool save_override(const Variable& d, const Value& v) override {
        overrides[d.name] = v.ovr.u;
        ovr_valid[d.name] = v.ovr_valid;
        return true;
    }
    bool clear_override(const Variable& d) override {
        overrides.erase(d.name);
        return true;
    }
    bool load_retained(const Variable& d, Value& v) override {
        auto it = retained.find(d.name);
        if (it == retained.end()) return false;
        v.v = Scalar::U(it->second);
        return true;
    }
    bool save_retained(const Variable& d, const Value& v) override {
        saves++;
        if (refuse) return false;
        retained[d.name] = v.v.u;
        return true;
    }
};

// One of each interesting kind: the retained rows carry F_RETAIN, the plain one does not.
const Variable g_rows[] = {
    GR("total_litres", T_U32, Scalar::U(0), 0, "L", "litres delivered", ""),
    G ("plain_count",  T_U32, Scalar::U(0), 0, "",  "not retained",     ""),
    GR("total_hours",  T_F32, Scalar::F(0.0f), 1, "h", "hours run",     ""),
};
const uint16_t COUNT = (uint16_t)(sizeof(g_rows) / sizeof(g_rows[0]));

Value    g_vals[COUNT];
uint16_t g_index[COUNT];

VarTables tables() {
    VarTables t;
    t.var   = g_rows;
    t.val   = g_vals;
    t.count = COUNT;
    t.index = g_index;
    t.pool  = nullptr;
    t.pool_bytes = 0;
    return t;
}

}  // namespace

TEST_CASE("retain: only a computed row carrying F_RETAIN is retained") {
    // One predicate, so load/save cannot disagree: restoring an OUTPUT from
    // flash would energise a contactor before anything looked at the plant.
    const Variable computed = G("c", T_U32, Scalar::U(0), 0, "", "", "");
    CHECK_FALSE(Bank::retainable(computed));      // no flag

    for (uint8_t role = 0; role < 4; ++role) {
        Variable v = computed;
        v.flags = (uint8_t)(v.flags | F_RETAIN);
        v.role  = (VarRole)role;
        const bool want = (v.role == ROLE_COMPUTED);
        INFO("role " << (int)role);
        CHECK(Bank::retainable(v) == want);
    }
}

TEST_CASE("retain: a total survives the reboot, a plain computed row does not") {
    MemStore store;

    {
        VarTables t = tables();
        Bank a(t);
        a.init();
        a.set_store(&store);

        a.var(a.find("total_litres")).set_i(4321);
        a.var(a.find("plain_count")).set_i(99);
        CHECK(a.save_retained() >= 1);
    }

    VarTables t2 = tables();   // down and back: a fresh bank over the same store
    Bank b(t2);
    b.init();
    b.set_store(&store);

    CHECK(b.var(b.find("total_litres")).i() == 0);   // nothing restored yet
    const uint16_t n = b.load_retained();
    CHECK(n >= 1);

    INFO("the total carries on from where it was");
    CHECK(b.var(b.find("total_litres")).i() == 4321);
    INFO("...and a computed row that did not ask to be retained starts clean");
    CHECK(b.var(b.find("plain_count")).i() == 0);
}

TEST_CASE("retain: a restored total reads as good, not as no-data") {
    // Otherwise every reader falls back to the zero the restore exists to avoid.
    MemStore store;
    store.retained["total_litres"] = 777;

    VarTables t = tables();
    Bank b(t);
    b.init();
    b.set_store(&store);
    REQUIRE(b.var(b.find("total_litres")).live_quality() == Q_NO_DATA);

    b.load_retained();

    CHECK(b.var(b.find("total_litres")).live_valid());
    CHECK(b.var(b.find("total_litres")).i() == 777);
}

TEST_CASE("retain: what is saved is what the program computed, not a force") {
    // Forcing a totalizer to exercise an alarm must not corrupt the plant's history.
    MemStore store;
    VarTables t = tables();
    Bank a(t);
    a.init();
    a.set_store(&store);

    const VarRef r = a.find("total_litres");
    a.var(r).set_i(500);
    a.override_set(a.var(r), Scalar::U(999999), true);
    REQUIRE(a.var(r).i() == 999999);          // the program's readers see the force

    a.save_retained();

    INFO("the stored value is the live one under the override");
    CHECK(store.retained["total_litres"] == 500u);
}

TEST_CASE("retain: a store that refuses is not silently believed") {
    MemStore store;
    store.refuse = true;

    VarTables t = tables();
    Bank a(t);
    a.init();
    a.set_store(&store);
    a.var(a.find("total_litres")).set_i(7);

    CHECK(a.save_retained() == 0);            // nothing counted as written
    CHECK(store.saves > 0);                   // ...but it was attempted
    CHECK(store.retained.empty());
}

TEST_CASE("retain: with no store at all, nothing pretends to persist") {
    VarTables t = tables();
    Bank a(t);
    a.init();
    CHECK(a.save_retained() == 0);
    CHECK(a.load_retained() == 0);
}

}  // namespace retain
