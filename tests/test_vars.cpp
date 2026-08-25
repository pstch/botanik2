/**
 * @file test_vars.cpp
 * @brief The variable model on its own: no bank, no site, no scan cycle.
 */

#include "doctest.h"

#include "libbotanik.h"

using namespace Botanik;

namespace {

// Two rows to read through. G() is the computed-value factory; a Value carries
// its descriptor so the conversions know the arm the union is holding.
const Vars::Descriptor D_I32 =
    Vars::G("t_i32", Vars::T_I32, Vars::SI(0), 0, "", "an i32 row", "");
const Vars::Descriptor D_U32 =
    Vars::G("t_u32", Vars::T_U32, Vars::SU(0), 0, "", "a u32 row", "");
const Vars::Descriptor D_F32 =
    Vars::G("t_f32", Vars::T_F32, Vars::SF(0.0f), 2, "", "an f32 row", "");

Vars::Value of_i(int64_t x)  { return Vars::Value{ Vars::Scalar::I(x), &D_I32, Vars::T_I32, Vars::Q_GOOD }; }
Vars::Value of_u(uint64_t x) { return Vars::Value{ Vars::Scalar::U(x), &D_U32, Vars::T_U32, Vars::Q_GOOD }; }
Vars::Value of_f(float x)    { return Vars::Value{ Vars::Scalar::F(x), &D_F32, Vars::T_F32, Vars::Q_GOOD }; }

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
    // G/SV take `retained` the way P does. What the flag means is split by
    // ROLE: a computed row goes to the retained store, a parameter already
    // persists through its own key and only becomes reset-proof.
    const Vars::Descriptor plain =
        Vars::G("t_plain", Vars::T_F32, Vars::SF(0.0f), 1, "L", "not retained", "");
    const Vars::Descriptor kept =
        Vars::G("t_kept", Vars::T_F32, Vars::SF(0.0f), 1, "L", "retained", "", true);

    CHECK_FALSE(Vars::Bank::retainable(plain));
    CHECK(Vars::Bank::retainable(kept));
    // ...and not as a parameter, which is the other half of the same flag.
    CHECK_FALSE(Vars::Bank::param_retained(kept));

    const Vars::Descriptor pk =
        Vars::P("t_pk", Vars::T_U16, 1, 0, 10, 0, "", 900, "a retained param", "",
                true, true);
    CHECK(Vars::Bank::param_retained(pk));
    CHECK_FALSE(Vars::Bank::retainable(pk));

    // The published factory carries it too - that is what SVR used to be.
    const Vars::Descriptor sv =
        Vars::SV("t_sv", Vars::T_F32, 0, 1, "L", 901, "a totalizer", "", true);
    CHECK(Vars::Bank::retainable(sv));
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
