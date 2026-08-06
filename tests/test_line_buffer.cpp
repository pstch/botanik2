/**
 * @file test_line_buffer.cpp
 * @brief Tests for LineBuffer<L,N> in pages/line_buffer.h — the fixed-capacity
 *        circular line store: newest-first access, wrap-around eviction,
 *        per-line truncation, and the store_wrapped() newline/width splitter.
 */
#include <cstring>                          // memcmp / strlen

#include "libbotanik/pages/line_buffer.h"   // Pages::LineBuffer

#include "doctest.h"                        // the test framework (vendored single header)

using Botanik::Pages::LineBuffer;

namespace {
    // True if the line @p dist back from newest equals @p expect. Calls line()
    // internally so its out-param is populated before the comparison (sibling
    // call-argument evaluation order would otherwise be unspecified).
    template <class LB>
    bool line_eq(const LB& lb, uint16_t dist, const char* expect) {
        uint16_t len = 0;
        const char* p = lb.line(dist, len);
        return p && len == (uint16_t)std::strlen(expect) && std::memcmp(p, expect, len) == 0;
    }
}

// store / line / count
// ----------------------------------------------------------------------------

TEST_CASE("LineBuffer: stores lines and reads them newest-first") {
    LineBuffer<8, 16> lb;
    CHECK(lb.count() == 0);
    lb.store("one", 3);
    lb.store("two", 3);
    lb.store("three", 5);
    CHECK(lb.count() == 3);

    CHECK(line_eq(lb, 1, "three")); // 1 = most recent
    CHECK(line_eq(lb, 2, "two"));
    CHECK(line_eq(lb, 3, "one"));
}

TEST_CASE("LineBuffer: line() rejects out-of-range distances") {
    LineBuffer<8, 16> lb;
    lb.store("x", 1);
    uint16_t len = 7;
    CHECK(lb.line(0, len) == nullptr);   // 0 is not a valid 1-based distance
    CHECK(lb.line(2, len) == nullptr);   // only one line stored
    CHECK(len == 7);                     // len untouched on failure
}

TEST_CASE("LineBuffer: a line longer than N is truncated to N bytes") {
    LineBuffer<4, 8> lb;
    lb.store("0123456789ABC", 13);       // 13 bytes into 8-wide slots
    uint16_t len = 0;
    const char* p = lb.line(1, len);
    CHECK(len == 8);
    CHECK(std::memcmp(p, "01234567", 8) == 0);
}

TEST_CASE("LineBuffer: wraps once full, evicting the oldest lines") {
    LineBuffer<3, 8> lb;
    for (const char* s : {"1", "2", "3", "4", "5"}) lb.store(s, 1);
    CHECK(lb.count() == 3);              // capped at L

    uint16_t len = 0;
    CHECK(line_eq(lb, 1, "5"));
    CHECK(line_eq(lb, 2, "4"));
    CHECK(line_eq(lb, 3, "3"));
    CHECK(lb.line(4, len) == nullptr);  // "1" and "2" were evicted
}

TEST_CASE("LineBuffer: clear() drops everything") {
    LineBuffer<4, 8> lb;
    lb.store("a", 1);
    lb.store("b", 1);
    lb.clear();
    CHECK(lb.count() == 0);
    uint16_t len = 0;
    CHECK(lb.line(1, len) == nullptr);
}

// store_wrapped
// ----------------------------------------------------------------------------

TEST_CASE("store_wrapped: splits on embedded newlines, one entry per line") {
    LineBuffer<8, 16> lb;
    uint16_t n = lb.store_wrapped("a\nb\nc", 5, /*cols*/0, "", 0);
    CHECK(n == 3);
    CHECK(lb.count() == 3);
    CHECK(line_eq(lb, 1, "c"));  // newline consumed, not printed
    CHECK(line_eq(lb, 2, "b"));
    CHECK(line_eq(lb, 3, "a"));
}

TEST_CASE("store_wrapped: breaks on width and prefixes continuation rows") {
    LineBuffer<8, 16> lb;
    uint16_t n = lb.store_wrapped("abcdef", 6, /*cols*/4, ">", 1);
    CHECK(n == 2);
    CHECK(line_eq(lb, 2, "abcd")); // first row: no prefix, full width
    CHECK(line_eq(lb, 1, ">ef"));  // continuation: prefix + remainder
}

TEST_CASE("store_wrapped: cols == 0 means unbounded width (newlines still split)") {
    LineBuffer<8, 32> lb;
    uint16_t n = lb.store_wrapped("a long single line", 18, /*cols*/0, ">", 1);
    CHECK(n == 1);                                // no width cap -> one entry
    CHECK(line_eq(lb, 1, "a long single line"));
}

TEST_CASE("store_wrapped: always stores at least one (possibly empty) line") {
    LineBuffer<8, 16> lb;
    uint16_t n = lb.store_wrapped("", 0, 0, "", 0);
    CHECK(n == 1);
    CHECK(lb.count() == 1);
    uint16_t len = 7;
    const char* p = lb.line(1, len);
    CHECK(p != nullptr);
    CHECK(len == 0);
}
