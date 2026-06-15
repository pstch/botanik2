/**
 * @file test_text.cpp
 * @brief Tests for the text helpers in utils/text.cpp — the fixed-point
 *        @c format_float() that stands in for the missing @c %f, and the
 *        "N u ago" duration formatter @c format_age().
 */
#include <cstring>
#include <limits>

#include "doctest.h"

#include "libbotanik/utils/text.h"

using Botanik::Utils::format_float;
using Botanik::Utils::format_age;

// format_float
// ----------------------------------------------------------------------------

TEST_CASE("format_float: basic fixed-point") {
    char b[32];
    CHECK(std::strcmp(format_float(b, sizeof(b), 3.14159, 2), "3.14") == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b), 3.14159, 0), "3")    == 0);  // no point
    CHECK(std::strcmp(format_float(b, sizeof(b), 1.0,     1), "1.0")  == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b), 0.0,     1), "0.0")  == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b), 12.345,  3), "12.345") == 0);
}

TEST_CASE("format_float: rounds half away from zero") {
    char b[32];
    CHECK(std::strcmp(format_float(b, sizeof(b),  2.5,  0),  "3") == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b), -2.5,  0), "-3") == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b),  1.25, 1), "1.3") == 0);  // tie rounds up
    CHECK(std::strcmp(format_float(b, sizeof(b),  9.99, 1), "10.0") == 0); // carry into int part
}

TEST_CASE("format_float: negative values") {
    char b[32];
    CHECK(std::strcmp(format_float(b, sizeof(b), -3.14159, 2), "-3.14") == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b), -0.5,     1), "-0.5")  == 0);
}

TEST_CASE("format_float: zero-padding to width (after sign)") {
    char b[32];
    CHECK(std::strcmp(format_float(b, sizeof(b),  5.0, 1, 6), "0005.0") == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b), -5.0, 1, 6), "-005.0") == 0); // sign counts toward width
    CHECK(std::strcmp(format_float(b, sizeof(b),  5.0, 1, 0), "5.0")    == 0); // width 0 = no padding
    // Already at/over width: no padding added.
    CHECK(std::strcmp(format_float(b, sizeof(b), 123.0, 1, 3), "123.0") == 0);
}

TEST_CASE("format_float: decimals capped at 9") {
    char b[32];
    // 12 requested decimals clamps to 9.
    const char* r = format_float(b, sizeof(b), 1.0, 12);
    CHECK(std::strlen(r) == std::strlen("1.000000000")); // "1." + 9 zeros
}

TEST_CASE("format_float: output is truncated, not overrun, when cap is tight") {
    char b[8];
    // "3.14" needs 5 bytes incl. NUL; cap 4 keeps 3 chars + NUL, dropping the
    // least-significant tail.
    CHECK(std::strcmp(format_float(b, 4, 3.14159, 2), "3.1") == 0);
    // cap 1 leaves room only for the terminator.
    CHECK(std::strcmp(format_float(b, 1, 3.14159, 2), "") == 0);
}

TEST_CASE("format_float: cap 0 leaves the buffer untouched") {
    char b[8] = "guard";
    CHECK(format_float(b, 0, 3.14, 2) == b); // returns buf
    CHECK(std::strcmp(b, "guard") == 0);     // nothing written
}

TEST_CASE("format_float: non-finite inputs render as tags") {
    char b[32];
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(std::strcmp(format_float(b, sizeof(b),  nan, 2),  "nan") == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b),  inf, 2),  "inf") == 0);
    CHECK(std::strcmp(format_float(b, sizeof(b), -inf, 2), "-inf") == 0);
}

// format_age
// ----------------------------------------------------------------------------

TEST_CASE("format_age: picks the largest sensible unit") {
    char b[16];
    CHECK(std::strcmp(format_age(b, sizeof(b),     0), "  0 s ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b),    45), " 45 s ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b),    59), " 59 s ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b),    60), "  1 m ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b),  3599), " 59 m ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b),  3600), "  1 h ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b), 86399), " 23 h ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b), 86400), "  1 d ago") == 0);
    CHECK(std::strcmp(format_age(b, sizeof(b), 172800), "  2 d ago") == 0);
}

TEST_CASE("format_age: field is constant width (9 chars + NUL)") {
    char b[16];
    CHECK(std::strlen(format_age(b, sizeof(b),     0)) == 9);
    CHECK(std::strlen(format_age(b, sizeof(b), 86400)) == 9);
    // Width holds up to 3 digits; >= 1000 d would widen it.
}
