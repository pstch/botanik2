/**
 * @file test_scalar.cpp
 * @brief The one value slot: round-trip per type, the write coercion, bounds,
 *        rendering and parsing.
 */
#include <cstdint>                  // int64_t/uint64_t

#include "libbotanik/utils/text.h"  // Utils::compare_str
#include "libbotanik/vars/scalar.h" // Vars::Scalar + format/parse

#include "doctest.h"                // the test framework (vendored single header)

using namespace Botanik;
using namespace Botanik::Vars;

namespace {
    const char* ups_str(uint64_t v) {
        switch (v) { case 1: return "ONLINE"; case 2: return "BATTERY"; }
        return nullptr;          // unmapped: the number should show through
    }

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
