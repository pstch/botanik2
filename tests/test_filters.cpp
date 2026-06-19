/**
 * @file test_filters.cpp
 * @brief Tests for the metric filter primitives in utils/filters.h — the
 *        alpha helper @c ema_alpha(), the asymmetric @c ExpMovingAverage, and
 *        the bucketed sliding-window maximum @c WindowedMax.
 */
#include "doctest.h"

#include "libbotanik/utils/filters.h"

using Botanik::Utils::ema_alpha;
using Botanik::Utils::ExpMovingAverage;
using Botanik::Utils::WindowedMax;
using doctest::Approx;

// ema_alpha
// ----------------------------------------------------------------------------

TEST_CASE("ema_alpha: alpha is the sample-period / tau ratio") {
    CHECK(ema_alpha(100, 10) == Approx(0.1f));
    CHECK(ema_alpha(20,  10) == Approx(0.5f));
    CHECK(ema_alpha(40,  20) == Approx(0.5f));
}

TEST_CASE("ema_alpha: tau at or below one sample period collapses to 1.0") {
    CHECK(ema_alpha(10, 10) == Approx(1.0f)); // equal: can't resolve faster
    CHECK(ema_alpha(5,  10) == Approx(1.0f)); // below: clamps, no overshoot
    CHECK(ema_alpha(0,  10) == Approx(1.0f));
}

TEST_CASE("ema_alpha: default period is the tick period") {
    // Config::TASK_PERIOD_MS == 10, so these mirror the explicit-period cases.
    CHECK(ema_alpha(20)  == Approx(0.5f));
    CHECK(ema_alpha(100) == Approx(0.1f));
    CHECK(ema_alpha(10)  == Approx(1.0f));
}

// ExpMovingAverage
// ----------------------------------------------------------------------------

TEST_CASE("ExpMovingAverage: starts at zero") {
    ExpMovingAverage ema;
    CHECK(ema.value() == Approx(0.0f));
}

TEST_CASE("ExpMovingAverage: alpha 1.0 tracks the input exactly") {
    ExpMovingAverage ema;
    ema.configure(1.0f, 1.0f);
    ema.push(42.0f);
    CHECK(ema.value() == Approx(42.0f));
    ema.push(-7.0f);
    CHECK(ema.value() == Approx(-7.0f));
}

TEST_CASE("ExpMovingAverage: symmetric blend converges toward the input") {
    ExpMovingAverage ema;
    ema.configure(0.5f, 0.5f);
    ema.push(10.0f);              // 0.5*10 + 0.5*0
    CHECK(ema.value() == Approx(5.0f));
    ema.push(10.0f);             // 0.5*10 + 0.5*5
    CHECK(ema.value() == Approx(7.5f));
}

TEST_CASE("ExpMovingAverage: asymmetric attack/decay rises fast, holds on falls") {
    ExpMovingAverage ema;
    ema.configure(1.0f, 0.0f);   // snap up instantly, never decay
    ema.push(10.0f);
    CHECK(ema.value() == Approx(10.0f)); // rose: attack 1.0
    ema.push(5.0f);
    CHECK(ema.value() == Approx(10.0f)); // fell: decay 0.0 holds the peak
    ema.push(20.0f);
    CHECK(ema.value() == Approx(20.0f)); // rose again: attack 1.0
}

TEST_CASE("ExpMovingAverage: reset clears back to zero") {
    ExpMovingAverage ema;
    ema.configure(1.0f, 1.0f);
    ema.push(123.0f);
    ema.reset();
    CHECK(ema.value() == Approx(0.0f));
}

// WindowedMax
// ----------------------------------------------------------------------------

TEST_CASE("WindowedMax: reports the max of the samples in the window") {
    WindowedMax<4> w;
    w.configure(8);
    CHECK(w.value() == 0);       // empty
    w.push(3);
    w.push(7);
    w.push(2);
    CHECK(w.value() == 7);
}

TEST_CASE("WindowedMax: a peak ages out as the window fills past it") {
    WindowedMax<3> w;
    w.configure(3);              // window == K, so one sample per bucket
    w.push(9);
    w.push(1);
    w.push(1);
    CHECK(w.value() == 9);       // peak still inside the window
    w.push(1);                   // recycles the bucket holding 9
    CHECK(w.value() == 1);       // peak has aged out
}

TEST_CASE("WindowedMax: a later, higher peak supersedes immediately") {
    WindowedMax<3> w;
    w.configure(3);
    w.push(5);
    CHECK(w.value() == 5);
    w.push(12);
    CHECK(w.value() == 12);
}

TEST_CASE("WindowedMax: reset clears the window") {
    WindowedMax<4> w;
    w.configure(8);
    w.push(99);
    w.reset();
    CHECK(w.value() == 0);
}

TEST_CASE("WindowedMax: a window shorter than K still works (span floored to 1)") {
    WindowedMax<4> w;
    w.configure(2);              // window < K: each bucket spans one sample
    w.push(4);
    w.push(8);
    CHECK(w.value() == 8);
    // Push enough lower samples to rotate through all K buckets; the peak ages out.
    w.push(1);
    w.push(1);
    w.push(1);
    w.push(1);
    CHECK(w.value() == 1);
}
