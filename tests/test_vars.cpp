/**
 * @file test_vars.cpp
 * @brief The variable model: the conversions on their own, then what a
 *        reference that resolved to nothing does.
 */

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/utils/text.h"
#include "site_bank.h"

// The differential parse test below reads the HOST's strtod as its reference.
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Botanik;

namespace {

// Three rows to read through. A status value with register 0 is the computed
// row that is not published; a Value carries its descriptor, so the conversions
// know which arm of the union it is holding.
const Vars::Descriptor D_I32 =
    Vars::SV("t_i32", Vars::T_I32, 0, 0, "", 0, "an i32 row", "");
const Vars::Descriptor D_U32 =
    Vars::SV("t_u32", Vars::T_U32, 0, 0, "", 0, "a u32 row", "");
const Vars::Descriptor D_F32 =
    Vars::SV("t_f32", Vars::T_F32, 0, 2, "", 0, "an f32 row", "");

Vars::Value of_i(int64_t x)  { return Vars::Value{ Vars::Scalar::I(x), &D_I32, Vars::T_I32 }; }
Vars::Value of_u(uint64_t x) { return Vars::Value{ Vars::Scalar::U(x), &D_U32, Vars::T_U32 }; }
Vars::Value of_f(float x)    { return Vars::Value{ Vars::Scalar::F(x), &D_F32, Vars::T_F32 }; }

}  // namespace

TEST_CASE("vars: a narrowed value pins at its rail rather than wrapping") {
    // 300 as a u8 is 255, not 44. A wrapped reading is indistinguishable from
    // a real one; a pinned reading is obviously at the end of the scale.
    CHECK(of_i(300).as<uint8_t>()  == 255u);
    CHECK(of_i(300).as<int8_t>()   == 127);
    CHECK(of_i(70000).as<uint16_t>() == 65535u);
    CHECK(of_i(70000).as<int16_t>()  == 32767);

    // ...and at the bottom, which is where the signed/unsigned split shows:
    // a negative read as unsigned is 0, not a huge positive.
    CHECK(of_i(-300).as<int8_t>()  == -128);
    CHECK(of_i(-300).as<uint8_t>() == 0u);
    CHECK(of_i(-1).as<uint32_t>()  == 0u);
}

TEST_CASE("vars: a value inside the rails narrows exactly") {
    CHECK(of_i(42).as<uint8_t>()   == 42u);
    CHECK(of_i(-42).as<int8_t>()   == -42);
    CHECK(of_u(4000000000u).as<uint32_t>() == 4000000000u);
    // The widest whole type is not narrowed at all.
    CHECK(of_u(0xFFFFFFFFFFFFFFFFull).as<uint64_t>() == 0xFFFFFFFFFFFFFFFFull);
}

TEST_CASE("vars: a float row narrows through its own saturation first") {
    CHECK(of_f(1.9f).as<uint8_t>()  == 1u);     // truncates toward zero
    CHECK(of_f(1e12f).as<uint32_t>() == 4294967295u);
    CHECK(of_f(-1e12f).as<int32_t>() == -2147483647 - 1);
    // NaN is 0 rather than whichever rail the comparison happens to take.
    const float nan = 0.0f / 0.0f;
    CHECK(of_f(nan).as<uint8_t>() == 0u);
    CHECK(of_f(nan).as<int32_t>() == 0);
}

TEST_CASE("vars: retention is a flag on the factory, not a factory of its own") {
    // SV takes `retained` the way P does. What the flag means is split by KIND:
    // a status row goes to the retained store, a parameter already persists
    // through its own key and only becomes reset-proof.
    const Vars::Descriptor plain =
        Vars::SV("t_plain", Vars::T_F32, 0, 1, "L", 0, "not retained", "");
    const Vars::Descriptor kept =
        Vars::SV("t_kept", Vars::T_F32, 0, 1, "L", 0, "retained", "", true);

    CHECK_FALSE(Vars::Persist::retainable(plain));
    CHECK(Vars::Persist::retainable(kept));
    // ...and not as a parameter, which is the other half of the same flag.
    CHECK_FALSE(Vars::Persist::param_retained(kept));

    const Vars::Descriptor pk =
        Vars::P("t_pk", Vars::T_U16, 1, 0, 10, Vars::VAR_NONE, Vars::VAR_NONE,
                0, "", 900, "a retained param", "", true, true);
    CHECK(Vars::Persist::param_retained(pk));
    CHECK_FALSE(Vars::Persist::retainable(pk));
}

TEST_CASE("text: a listing pattern is a prefix until it has a wildcard") {
    auto m = [](const char* s, const char* pat) {
        return Utils::glob_bounded(s, pat, (uint16_t)strlen(pat));
    };

    // What every shell listing meant before wildcards existed, unchanged.
    CHECK(m("cli_temp_in", "cli_"));
    CHECK(m("cli_temp_in", "cli_temp_in"));
    CHECK_FALSE(m("cli_temp_in", "pwr_"));
    // A prefix longer than the name is not a match.
    CHECK_FALSE(m("cli_", "cli_temp_in"));

    // With a wildcard the pattern is anchored at BOTH ends, so a bare group
    // name stops matching and `*` is how you say "the rest".
    CHECK(m("cli_window1_pos", "cli_win*_pos"));
    CHECK(m("cli_window1_pos", "*_pos"));
    CHECK(m("cli_window1_pos", "cli_*"));
    CHECK(m("cli_window1_pos", "*"));
    CHECK(m("cli_window1_pos", "*win*pos"));
    CHECK_FALSE(m("cli_window1_tgt", "*_pos"));
    CHECK_FALSE(m("cli_window1_pos", "cli_win*_tgt"));

    // ? is exactly one character.
    CHECK(m("cli_intr1", "cli_intr?"));
    CHECK_FALSE(m("cli_intr12", "cli_intr?"));

    // The backtrack: the first star must give characters back for the tail.
    CHECK(m("aaa_pos", "*a_pos"));
    CHECK(m("abcabc", "*abc"));
    CHECK_FALSE(m("abcabd", "*abc"));

    // Null and empty: no pattern filters nothing, a null name matches nothing.
    CHECK(Utils::glob_bounded("anything", nullptr, 0));
    CHECK(Utils::glob_bounded("anything", "cli_", 0));
    CHECK_FALSE(Utils::glob_bounded(nullptr, "cli_", 4));
}

TEST_CASE("text: a table filter is a substring until it has a wildcard") {
    auto m = Utils::glob_ci;

    // What every table filter meant before wildcards existed, unchanged: the
    // typed text anywhere in the name, either case.
    CHECK(m("cli_temp_in", "temp"));
    CHECK(m("cli_temp_in", "TEMP"));
    CHECK(m("CLI_TEMP_IN", "temp"));
    CHECK_FALSE(m("cli_temp_in", "pwr"));

    // With a wildcard it is anchored at both ends, so a fragment that matched
    // anywhere stops matching.
    CHECK(m("cli_window1_pos", "cli_*"));
    CHECK(m("cli_window1_pos", "*_POS"));
    CHECK(m("cli_window1_pos", "cli_win*_pos"));
    CHECK(m("cli_window1_pos", "cli_window?_pos"));
    CHECK(m("cli_window1_pos", "window"));         // no wildcard: still anywhere
    CHECK_FALSE(m("cli_window1_pos", "window*"));  // one: anchored at the start
    CHECK_FALSE(m("cli_window1_pos", "*_tgt"));

    // Null and empty: no filter narrows nothing, a null name matches nothing.
    CHECK(m("anything", ""));
    CHECK(m("anything", nullptr));
    CHECK_FALSE(m(nullptr, "cli_"));
    CHECK_FALSE(m(nullptr, "*"));
}

TEST_CASE("vars: a calibration with no span reads as implausible, not as a number") {
    // raw_min == raw_max leaves to_eng() with no slope, so every count the ADC
    // reads collapses to eng_min - which is inside the row's own band, so it
    // would sit at Q_GOOD for ever and the under-range alarm would never fire.
    Vars::AnalogValues a = {};
    a.raw_min = 4000.0f; a.raw_max = 4000.0f;
    a.eng_min = 20.0f;   a.eng_max = 30.0f;
    a.min     = 20.0f;   a.max     = 30.0f;

    CHECK(a.degenerate());
    CHECK(a.scale() == 0.0f);
    // The trap: the collapsed value is a PLAUSIBLE one, which is why the span
    // has to be tested separately from the band.
    CHECK(a.to_eng(9999.0f) == 20.0f);
    CHECK(a.plausible(a.to_eng(9999.0f)));

    // A real span converts and stays believable.
    a.raw_max = 6000.0f;
    CHECK_FALSE(a.degenerate());
    CHECK(a.to_eng(4000.0f) == doctest::Approx(20.0f));
    CHECK(a.to_eng(6000.0f) == doctest::Approx(30.0f));
    CHECK(a.to_eng(5000.0f) == doctest::Approx(25.0f));
}

TEST_CASE("vars: an index that is not a row reads the sink descriptor") {
    Vars::Bank bank;
    TestSite::build(bank);
    REQUIRE(bank.count() > 0);

    INFO("VAR_NONE is what an unbound reference carries, and 0xFFFF into the "
         "table is 46 KB past the end of it");
    CHECK(std::string(bank.descriptor(Vars::VAR_NONE).name) == "<none>");
    CHECK(std::string(bank.descriptor(bank.count()).name) == "<none>");
    CHECK(std::string(bank.descriptor(0).name) != "<none>");

    INFO("...and the sink is never good: a law reading one gets the declared "
         "substitute rather than a zero it cannot tell from a reading");
    CHECK(bank.var(Vars::VAR_NONE).quality() == Vars::Q_NONE);
    CHECK_FALSE(bank.var(Vars::VAR_NONE).usable());
}

TEST_CASE("vars: a handle with no bank reads nothing and drives nothing") {
    // What a default-built Var is, and what Map::row() hands back with no table
    // behind it.
    Vars::Var h;
    CHECK(h.null());
    CHECK(h.row() == Vars::VAR_NONE);
    CHECK(h.as_f32() == doctest::Approx(0.0f));
    CHECK_FALSE(h.valid());
    CHECK(h.text() == nullptr);
    CHECK(h.param_set("1") == Vars::SET_INVALID);

    h.set(1.0f);          // drives the sink, which forgets it
    h.override_set(Vars::Scalar::F(2.0f));
    CHECK(h.as_f32() == doctest::Approx(0.0f));
}

TEST_CASE("vars: a reference with no bank behind it reads nothing") {
    // Every generated POU holds these as members; a build with no table has
    // no bank for them to read.
    Vars::Bank* was = Vars::runtime_bank;
    Vars::runtime_bank = nullptr;

    const Vars::Ref<float>    f = { Vars::VAR_NONE };
    const Vars::Ref<bool>     b = { Vars::VAR_NONE };
    const Vars::Ref<uint16_t> u = { Vars::VAR_NONE };

    CHECK(f.get() == doctest::Approx(0.0f));
    CHECK(b.get() == false);
    CHECK(u.get() == 0u);
    CHECK_FALSE(f.valid());
    f.drive(1.0f);
    b.drive(true);
    u.drive(7u);

    Vars::runtime_bank = was;
}

namespace {

Vars::ParseResult parsed(Vars::VarType t, const char* text, Vars::Scalar* out) {
    return Vars::parse_scalar(t, text, out, nullptr);
}

}  // namespace

TEST_CASE("vars: parsing an integer separates junk from out of range") {
    Vars::Scalar s;

    INFO("a value the type cannot hold is RANGE, so the shell can say which");
    CHECK(parsed(Vars::T_U8, "255", &s) == Vars::PARSE_OK);
    CHECK(s.u == 255u);
    CHECK(parsed(Vars::T_U8, "256", &s) == Vars::PARSE_RANGE);
    CHECK(parsed(Vars::T_I8, "-128", &s) == Vars::PARSE_OK);
    CHECK(s.i == -128);
    CHECK(parsed(Vars::T_I8, "-129", &s) == Vars::PARSE_RANGE);

    INFO("trailing junk is never a quiet prefix");
    CHECK(parsed(Vars::T_U32, "12abc", &s) == Vars::PARSE_MALFORMED);
    CHECK(parsed(Vars::T_U32, "", &s)      == Vars::PARSE_MALFORMED);
    CHECK(parsed(Vars::T_U32, "  ", &s)    == Vars::PARSE_MALFORMED);
    CHECK(parsed(Vars::T_U32, "+", &s)     == Vars::PARSE_MALFORMED);
    CHECK(parsed(Vars::T_U32, "0x10", &s)  == Vars::PARSE_MALFORMED);

    INFO("spaces and a leading + are tolerated on both ends");
    CHECK(parsed(Vars::T_U32, "  42  ", &s) == Vars::PARSE_OK);
    CHECK(s.u == 42u);
    CHECK(parsed(Vars::T_U32, "+42", &s) == Vars::PARSE_OK);
    CHECK(s.u == 42u);
}

TEST_CASE("vars: a 64-bit literal at the rail parses, one past it does not") {
    Vars::Scalar s;

    CHECK(parsed(Vars::T_U64, "18446744073709551615", &s) == Vars::PARSE_OK);
    CHECK(s.u == UINT64_MAX);
    CHECK(parsed(Vars::T_U64, "18446744073709551616", &s) == Vars::PARSE_RANGE);

    INFO("INT64_MIN has no positive counterpart, so it is scanned unsigned");
    CHECK(parsed(Vars::T_I64, "-9223372036854775808", &s) == Vars::PARSE_OK);
    CHECK(s.i == INT64_MIN);
    CHECK(parsed(Vars::T_I64, "-9223372036854775809", &s) == Vars::PARSE_RANGE);
    CHECK(parsed(Vars::T_I64, "9223372036854775807", &s) == Vars::PARSE_OK);
    CHECK(s.i == INT64_MAX);
    CHECK(parsed(Vars::T_I64, "9223372036854775808", &s) == Vars::PARSE_RANGE);
}

TEST_CASE("vars: a negative into an unsigned row is out of range, not a wrap") {
    // strtoull wrapped it into a huge positive, which reads as a real setpoint.
    Vars::Scalar s;
    CHECK(parsed(Vars::T_U32, "-1", &s)  == Vars::PARSE_RANGE);
    CHECK(parsed(Vars::T_U64, "-1", &s)  == Vars::PARSE_RANGE);
    CHECK(parsed(Vars::T_BOOL, "-1", &s) == Vars::PARSE_RANGE);
}

TEST_CASE("vars: parsing a real accepts the shapes an operator types") {
    Vars::Scalar s;

    CHECK(parsed(Vars::T_F32, "21.5", &s) == Vars::PARSE_OK);
    CHECK(s.f == 21.5f);
    CHECK(parsed(Vars::T_F32, "-0.2", &s) == Vars::PARSE_OK);
    CHECK(s.f == -0.2f);
    CHECK(parsed(Vars::T_F32, "24000", &s) == Vars::PARSE_OK);
    CHECK(s.f == 24000.0f);

    INFO("a bare point on either side, and an exponent");
    CHECK(parsed(Vars::T_F32, ".5", &s) == Vars::PARSE_OK);
    CHECK(s.f == 0.5f);
    CHECK(parsed(Vars::T_F32, "5.", &s) == Vars::PARSE_OK);
    CHECK(s.f == 5.0f);
    CHECK(parsed(Vars::T_F32, "1.5e2", &s) == Vars::PARSE_OK);
    CHECK(s.f == 150.0f);
    CHECK(parsed(Vars::T_F32, "15e-1", &s) == Vars::PARSE_OK);
    CHECK(s.f == 1.5f);
    CHECK(parsed(Vars::T_F32, "  +2.5  ", &s) == Vars::PARSE_OK);
    CHECK(s.f == 2.5f);
}

TEST_CASE("vars: a real that is not a number is refused, not coerced") {
    Vars::Scalar s;

    INFO("strtod took these; the row then held a NaN nothing compares true to");
    CHECK(parsed(Vars::T_F32, "nan", &s)  == Vars::PARSE_MALFORMED);
    CHECK(parsed(Vars::T_F32, "inf", &s)  == Vars::PARSE_MALFORMED);
    CHECK(parsed(Vars::T_F32, "-inf", &s) == Vars::PARSE_MALFORMED);
    CHECK(parsed(Vars::T_F32, "0x1p3", &s) == Vars::PARSE_MALFORMED);

    INFO("and the ordinary junk");
    CHECK(parsed(Vars::T_F32, "1.2.3", &s) == Vars::PARSE_MALFORMED);
    CHECK(parsed(Vars::T_F32, "1e", &s)    == Vars::PARSE_MALFORMED);
    CHECK(parsed(Vars::T_F32, ".", &s)     == Vars::PARSE_MALFORMED);
    CHECK(parsed(Vars::T_F32, "1.5abc", &s) == Vars::PARSE_MALFORMED);

    INFO("a good double is still not a good float");
    CHECK(parsed(Vars::T_F32, "1e300", &s)  == Vars::PARSE_RANGE);
    CHECK(parsed(Vars::T_F32, "-1e300", &s) == Vars::PARSE_RANGE);
    CHECK(parsed(Vars::T_F32, "1e99999", &s) == Vars::PARSE_RANGE);

    INFO("...but one that merely underflows the float is a legitimate zero");
    CHECK(parsed(Vars::T_F32, "1e-300", &s) == Vars::PARSE_OK);
    CHECK(s.f == 0.0f);
}

TEST_CASE("vars: parse_f64 is correctly rounded once narrowed to a float") {
    // The differential test the replacement of strtod rests on. The HOST's
    // strtod is correctly rounded to double, so narrowing both to float and
    // comparing the bits is the whole claim: parse_f64 need not be exact to a
    // double, only to the float every caller stores.
    //
    // Deterministic LCG rather than <random>, so a failure reproduces.
    uint64_t seed = 0x9E3779B97F4A7C15ull;
    auto next = [&seed]() {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return (uint32_t)(seed >> 32);
    };

    char text[64];
    size_t checked = 0, bad = 0;

    // The shapes a person types into a setpoint, and the ones format_float emits.
    for (int i = 0; i < 60000; i++) {
        const double v = ((double)next() / 4294967296.0) * 48000.0 - 24000.0;
        std::snprintf(text, sizeof text, "%.*f", (int)(next() % 5u), v);
        double got = 0.0;
        REQUIRE(Utils::parse_f64(text, &got) == Utils::NUM_OK);
        if ((float)got != (float)std::strtod(text, nullptr)) { bad++; if (bad < 4) MESSAGE(text); }
        checked++;
    }

    // Adversarial: full float32 precision, and the exponent extremes where the
    // power-of-ten scaling has to step rather than clamp.
    for (int i = 0; i < 60000; i++) {
        const uint32_t bits = next();
        float f;
        std::memcpy(&f, &bits, 4);
        if (f != f || f == 0.0f) continue;
        const double a = (double)f < 0 ? -(double)f : (double)f;
        if (a > 3.0e38 || a < 1e-44) continue;
        std::snprintf(text, sizeof text, "%.9g", (double)f);
        double got = 0.0;
        REQUIRE(Utils::parse_f64(text, &got) == Utils::NUM_OK);
        if ((float)got != (float)std::strtod(text, nullptr)) { bad++; if (bad < 4) MESSAGE(text); }
        checked++;
    }

    CHECK(bad == 0);
    CHECK(checked > 90000);
}
