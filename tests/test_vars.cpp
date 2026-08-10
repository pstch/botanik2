/**
 * @file test_vars.cpp
 * @brief The variable model: Scalar, Variable/Value/Bank, parameters and retention.
 *
 * Each section below is the former test_<name>.cpp, kept in its own
 * namespace so their fixtures and helpers cannot collide.
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
    // strtod accepts "nan" and "inf" outright, and 1e300 is a perfectly good
    // DOUBLE that becomes +inf on the narrowing cast - none of them set ERANGE,
    // which was the only check. The console reaches this through
    // `override <row> nan`, which persists the value; reject_implausible()
    // skips overridden rows, so it survives the reboot and every hysteresis and
    // deadband downstream then compares false against it.
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
    // Each write lands in the union member the type names, so the read is exact
    // rather than a reinterpretation of a neighbouring member.
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
    // Writing a float to an integer variable rounds; the old model wrote the
    // float's bits and read them back as an integer.
    CHECK(as_i64(T_U16, from_f32(T_U16, 63.6f)) == 64);
    CHECK(as_i64(T_I16, from_f32(T_I16, -63.6f)) == -64);
    // An unsigned variable cannot hold a negative: clamp rather than wrap to a
    // huge positive, which is what a cast would do.
    CHECK(as_i64(T_U16, from_f32(T_U16, -5.0f)) == 0);
    CHECK(as_i64(T_U32, from_i64(T_U32, -1)) == 0);
    // An integer written to a real variable keeps its value.
    CHECK(as_f32(T_F32, from_i64(T_F32, 42)) == doctest::Approx(42.0f));
}

TEST_CASE("scalar: digitals read as 0/1 so consumers need not branch") {
    CHECK(as_f32(T_BOOL, Scalar::B(true)) == doctest::Approx(1.0f));
    CHECK(as_f32(T_BOOL, Scalar::B(false)) == doctest::Approx(0.0f));
}

TEST_CASE("scalar: bounds compare in the variable's own type") {
    // What int32 bounds could not express: a fractional band. Bounds of
    // 0.5..2.5 truncated to 0..2 before the comparison ever happened.
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
    // strto* SATURATE rather than fail, so an overflowing literal used to
    // arrive as a perfectly plausible type maximum. in_range catches that for
    // the narrow types; for the widest it did not.
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

    // One table, every scope and several types - the shape a site has. Each row
    // is the factory for its scope, so a row cannot carry a field its kind has
    // no use for.
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
    // The whole point of Quality: three different things invalidate a reading,
    // they want three different next moves from whoever is looking, and the
    // model used to answer all three with the same bare "invalid".
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

    // ...and an output taken to its safe state is a fourth, which is not a
    // fault at all - it is the firmware declining to drive.
    f.bank.enable_control(Bank::INHIBIT_HEALTH);
    Var out = f.bank.var(V_VALVE);
    out.set(true);
    CHECK(out.live_quality() == Q_GOOD);
    f.bank.disable_control(Bank::INHIBIT_HEALTH);
    CHECK(out.live_quality() == Q_SAFE);

    // Every one of them reads as invalid, which is what the old bool said.
    CHECK_FALSE(out.valid());
    CHECK_FALSE(h.valid());
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
    // ...and it KEEPS the reading it had. A sensor that answered a moment ago
    // and then missed its timeout still knows what the plant was doing; 30 C
    // from six seconds ago is a far better account of a greenhouse than the
    // 20.5 its declared default reads as. valid() is false either way, so
    // anything that cares still knows not to trust it.
    CHECK(f.bank.var(V_EXT_TEMP).value() == doctest::Approx(30.0f));
}

TEST_CASE("model: live() and raw() agree about a stale reading") {
    // They are the two halves of one question, and they had drifted: raw() kept
    // the stale value, live() fell back to the default. The MONITOR page's live
    // column and `get`'s Raw value row read live(); the value beside them reads
    // raw(). A dying sensor is exactly when an engineer reads those two fields,
    // and they contradicted each other.
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);
    h.publish(Scalar::F(30.0f), 1000);
    f.bank.expire_stale(7000);                       // past the 5 s timeout
    REQUIRE(h.live_quality() == Q_STALE);

    CHECK(h.raw().f  == doctest::Approx(30.0f));
    CHECK(h.live().f == doctest::Approx(30.0f));     // not the 20.5 default
    CHECK_FALSE(h.valid());                          // still not to be trusted

    // ...and where there is nothing to keep, both fall back together.
    Var g = f.bank.var(V_EXT_TEMP);
    g.invalidate(Q_IMPLAUSIBLE);
    CHECK(g.raw().f  == doctest::Approx(20.5f));
    CHECK(g.live().f == doctest::Approx(20.5f));
}

TEST_CASE("model: a row that never read anything still falls back to its default") {
    // The other half of the rule above. Q_STALE keeps its value because there
    // IS one; Q_NO_DATA has nothing to keep, and a row dropped to safe or
    // reading outside its band must not be believed at all.
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
    f.bank.enable_control(Bank::INHIBIT_HEALTH);   // bring-up finished
    REQUIRE(f.bank.control_enabled());
    Var h = f.bank.var(V_VALVE);
    h.set(true);
    f.bank.override_set(h, Scalar::B(true));

    // `control disable` is how you take the plant by hand: the programs stop
    // deciding, and the override is then the only thing that can move an
    // actuator. Releasing it would leave no way to command one at all - and one
    // can still be TAKEN in that state, which is the point.
    f.bank.disable_control(Bank::INHIBIT_OPERATOR);
    CHECK(h.overridden());
    f.bank.override_clear(h);
    CHECK(f.bank.override_set(h, "1") == SET_OK);

    // The other inhibit means something else entirely: the firmware can no
    // longer reason about what it drives, and nobody is necessarily watching.
    // It must still fire with the operator's hold already in force, or the
    // "was control already off?" early exit would let a hand-held actuator ride
    // straight through a health fault.
    f.bank.disable_control(Bank::INHIBIT_HEALTH);
    CHECK_FALSE(h.overridden());
    CHECK_FALSE(h.valid());
    CHECK(h.b() == true);      // reads its declared safe state (def = true here)

    // An input's override is untouched either way - the sweep is outputs only.
    Var in = f.bank.var(V_EXT_TEMP);
    f.bank.override_set(in, Scalar::F(5.0f));
    f.bank.disable_control(Bank::INHIBIT_HEALTH);
    CHECK(in.overridden());
}

TEST_CASE("control: starts disabled, and an output is forceable while it is") {
    Fixture f;
    // Nothing may drive the field between power-on and the point bring-up says
    // it is ready - the system holds control until then. An override is live
    // immediately rather than staged, so there is nothing to refuse.
    CHECK_FALSE(f.bank.control_enabled());
    CHECK(f.bank.control_inhibited_by(Bank::INHIBIT_HEALTH));
    CHECK(f.bank.override_set(f.bank.var(V_VALVE), "1") == SET_OK);
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
    f.bank.override_set(in, Scalar::F(5.0f));
    f.bank.disable_control(Bank::INHIBIT_HEALTH);
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

// ---- String pool: a row that does not fit must not land on another ----------

TEST_CASE("pool: a row that does not fit gets no slot, not row 0's") {
    // Two 32-byte slots into a 40-byte pool: the second cannot fit. It used to
    // be parked at offset 0 with its declared capacity intact, so every writer
    // wrote a full slot on top of the FIRST row - and with a pool smaller than
    // one slot, past the end of the array entirely.
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
    // The out-of-bounds case: offset 0 + a 32-byte capacity into 8 bytes.
    // ASan is what makes this test worth having.
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
    // What F_BOUNDED was declared FOR on an input, and what nothing enforced:
    // a shorted 4-20 mA loop reads full scale, which no timeout catches.
    Fixture f;
    Var h = f.bank.var(V_EXT_TEMP);          // band -40..85, def 20.5

    h.publish(Scalar::F(20.0f), 1000);
    CHECK(f.bank.reject_implausible() == 0);
    CHECK(h.valid());

    h.publish(Scalar::F(500.0f), 2000);      // fresh, well-formed, impossible
    CHECK(f.bank.reject_implausible() == 1);
    CHECK_FALSE(h.valid());
    // ...and it reads its declared fallback, which is what lets a control
    // program lean on `def` instead of testing valid().
    CHECK(h.value() == doctest::Approx(20.5f));
}

TEST_CASE("plausibility: a non-finite reading is a failed read") {
    // A NaN compares false against everything, so it trips no band - and it
    // makes every downstream hysteresis and deadband misbehave quietly.
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
    // No F_BOUNDED means min/max are not a band, so there is nothing to judge.
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

TEST_CASE("params: an edit applies, persists, and survives a reload") {
    StoreEnv env;
    {
        Fx f;
        CHECK(f.params.set(f.by("p_u16"), "120") == SET_OK);
        CHECK(f.params.set(f.by("p_i32"), "-4200") == SET_OK);
        CHECK(f.params.set(f.by("p_f32"), "-12.25") == SET_OK);
        CHECK(f.params.set(f.by("p_bool"), "0") == SET_OK);
        CHECK(f.params.set(f.by("p_str"), "ntp.example.org") == SET_OK);
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
    // Not sizeof(Scalar): a u16 must occupy 2 bytes, or every existing record
    // changes encoding and HAL::Storage::get starts rejecting them.
    StoreEnv env;
    Fx f;
    CHECK(f.params.set(f.by("p_u16"), "300") == SET_OK);
    CHECK(f.params.set(f.by("p_bool"), "0") == SET_OK);
    CHECK(f.params.set(f.by("p_f32"), "1.5") == SET_OK);
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

TEST_CASE("params: a refused persist still applies the value") {
    // The distinction the tri-state exists for: reporting "invalid" when the
    // storage refused a perfectly good value sends the operator to fix their
    // input, which was never the problem.
    StoreEnv env("/nonexistent-dir-botanik-test/store");
    Fx f;
    CHECK(f.params.set(f.by("p_u16"), "120") == SET_NOT_PERSISTED);
    CHECK(f.by("p_u16").i() == 120);
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
    // A table with one row per role, so the output exclusion is testable.
    const Variable OVR_VARS[] = {
        MI("sensor", T_F32, 5, 3, 0, 2, SF(-50.0f), SF(150.0f), SF(1.5f), 5000, 1,
                     "C", "a reading", ""),
        G ("derived", T_F32, SF(0.0f), 1, "", "a computed value", ""),
        RO("coil",   31, 0, false, false, "an actuator", ""),
        P ("setting", T_U16, 64, 0, 3600, 0, "s", 1, "a parameter", ""),
    };

    /** @brief A bank over OVR_VARS, rebuilt from scratch to simulate a reboot -
     *  same store, fresh RAM. */
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
            bank.enable_control(Bank::INHIBIT_HEALTH);   // so an output is drivable
            bank.load_overrides();
        }
    };
}

TEST_CASE("override: a forced value survives a reboot") {
    StoreEnv env("/tmp/botanik_test_ovr_a");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "42.5") == SET_OK);
        CHECK(f.bank.var("sensor").value() == doctest::Approx(42.5f));
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
        // "force it, and make it read as a dead sensor" - the two are separate
        // facts and both have to come back.
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "42.5", false) == SET_OK);
        CHECK_FALSE(f.bank.var("sensor").valid());
    }
    {
        OvrFx f;
        CHECK(f.bank.var("sensor").overridden());
        CHECK_FALSE(f.bank.var("sensor").valid());
        // The forced value is still what it reads - forcing the QUALITY is a
        // separate thing from simulating the timeout fallback (see Value's
        // header note), and both halves came back from storage.
        CHECK(f.bank.var("sensor").value() == doctest::Approx(42.5f));
        // Clearing it hands the live value back.
        f.bank.override_clear(f.bank.var("sensor"));
        CHECK_FALSE(f.bank.var("sensor").overridden());
    }
}

TEST_CASE("override: releasing one stops it coming back") {
    StoreEnv env("/tmp/botanik_test_ovr_c");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("derived"), "7") == SET_OK);
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
        CHECK(f.bank.clear_overrides() == 2);
    }
    {
        OvrFx f;
        CHECK(f.bank.override_count() == 0);
    }
}

TEST_CASE("override: an actuator override persists like any other") {
    // An override means one thing whatever row it is on - "a person has taken
    // this by hand" - so an actuator's survives the reboot the same way a
    // sensor's does. Carving out an exception made it mean two things, and left
    // an operator wondering why the valve they forced came back and the reading
    // they forced did not.
    StoreEnv env("/tmp/botanik_test_ovr_e");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("coil"), "1") == SET_OK);
        CHECK(f.bank.var("coil").overridden());
    }
    {
        OvrFx f;
        CHECK(f.bank.var("coil").overridden());
        CHECK(f.bank.var("coil").b());
        CHECK(f.bank.override_count() == 1);
    }
    // ...and the system withdrawing control still takes it away, stored copy
    // included, so the restore cannot bring it back a second time.
    {
        OvrFx f;
        f.bank.disable_control(Bank::INHIBIT_HEALTH);
        CHECK_FALSE(f.bank.var("coil").overridden());
    }
    {
        OvrFx f;
        CHECK_FALSE(f.bank.var("coil").overridden());
        CHECK(f.bank.override_count() == 0);
    }
}

TEST_CASE("override: the system withdrawing control wipes the stored copy") {
    // A health withdrawal releases output overrides; a stored copy would hand
    // one back at the next boot, which is the hole the release exists to shut.
    StoreEnv env("/tmp/botanik_test_ovr_f");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "5") == SET_OK);
        f.bank.disable_control(Bank::INHIBIT_HEALTH);
        // An input's override is NOT released by a control withdrawal - control
        // authority is about driving the field, not about reading it.
        CHECK(f.bank.var("sensor").overridden());
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
        REQUIRE(f.bank.set(f.bank.var("setting"), "100") != SET_INVALID);
        REQUIRE(f.bank.override_set(f.bank.var("setting"), "7") == SET_OK);
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
    // Regression. `override <name> invalid` froze through the bank (persisting
    // the CURRENT quality) and then forced the new quality through the HANDLE,
    // which cannot reach the store - so the reboot handed back the old one.
    StoreEnv env("/tmp/botanik_test_ovr_q");
    {
        OvrFx f;
        f.bank.var("sensor").publish(Scalar::F(21.0f), 1000);
        REQUIRE(f.bank.var("sensor").valid());
        f.bank.override_freeze(f.bank.var("sensor"));
        f.bank.override_set(f.bank.var("sensor"), f.bank.var("sensor").raw(), false);
        CHECK_FALSE(f.bank.var("sensor").valid());
    }
    {
        OvrFx f;
        CHECK(f.bank.var("sensor").overridden());
        CHECK_FALSE(f.bank.var("sensor").valid());
        CHECK(f.bank.var("sensor").value() == doctest::Approx(21.0f));
    }
}

TEST_CASE("override: every release path also forgets the stored copy") {
    // Regression. Var's mutators were public and cleared RAM only, so a release
    // through the handle left the record behind and the override came back at
    // the next boot. They are Bank's now, which is why this can only be spelled
    // one way.
    StoreEnv env("/tmp/botanik_test_ovr_r");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "9") == SET_OK);
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

// A table with one of each interesting kind. G() is the computed factory; the
// retained one carries F_RETAIN, its neighbour does not.
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
    // The predicate is one function so the load and save paths cannot disagree.
    // A parameter already persists, an input is overwritten by the next scan,
    // and restoring an OUTPUT from flash would energise a contactor before
    // anything had looked at the plant.
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

    // The unit goes down and comes back: a fresh bank over the same store.
    VarTables t2 = tables();
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
    // Otherwise every reader falls back to the declared default - which is the
    // zero the restore existed to avoid.
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
    // A maintainer forcing a totalizer to exercise an alarm must not have that
    // written into the plant's history.
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
