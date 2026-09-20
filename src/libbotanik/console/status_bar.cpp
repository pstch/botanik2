/**
 * @file status_bar.cpp
 * @brief StatusBar implementation - row-0 tabs, clock, and metrics field.
 */
#include "libbotanik/utils/minmax.h"
#include "libbotanik/console/status_bar.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "libbotanik/config/terminal.gen.h"

#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Terminal {

    // Value-cell columns in "run:STATE cpu:nnn% stk:nnn% " - drawn normal-video
    // against the reverse-video bar. One place tied to the build_metrics()
    // format so the layout and the highlight can't drift apart.
    static bool metric_value_col(int c) {
        return (c >= 4  && c <= 8)     // lifecycle word
            || (c >= 14 && c <= 17)    // cpu value + '%'
            || (c >= 23 && c <= 26);   // stk value + '%'
    }

    StatusBar::StatusBar(Console& console, const Scheduler::SchedulerContext* sched)
        : _console(console), _terminal(console.terminal()), _sched(sched) {
        claim_row();
    }

    void StatusBar::claim_row() {
        // Row 0 frames the page, so it lands before it. Ranking it above the page
        // starves nothing: the clock and metrics rebuild once a second, tens of
        // bytes against a budget of thousands.
        _terminal.set_row_priority(0, PRIO_CHROME);
    }

    // Cooperative paint


    void StatusBar::step(uint64_t now) {
        // Rebuild the clock and metrics on their own cadence and repaint the whole
        // field; the terminal's back-buffer diff emits only the cells that moved.
        if (now - _last_ms >= Config::PAGE_HOT_MS) {
            _last_ms = now;
            update_clock();
            update_metrics();
        }
    }

    void StatusBar::draw_row() {
        constexpr uint16_t mlen = METRICS_LEN - 1;   // field width (drop NUL)

        // Save the attribute only: every caller repositions absolutely right
        // after, so a cursor restore would be wasted.
        bool saved_inv = _terminal.inverted();

        _terminal.move_to({0, 0});
        // Assert the reverse-video background (the row-0 bar).
        _terminal.set_inverted(true);

        // Compact tab strip: "1 2 3:LOGS 4 5" - only the active page shows its
        // name. Records the strip extent for draw_tabs()'s shrink handling.
        _tabs_end_col = draw_tab_strip();
        _terminal.set_inverted(true);

        // Middle: spaces up to the metrics start column.
        uint16_t metrics_col = (_terminal.cols() > mlen) ? (uint16_t)(_terminal.cols() - mlen) : 0;
        _terminal.fill_spaces(metrics_col);

        // Right + centre: paint the two live fields over the background.
        update_metrics();
        update_clock();

        _terminal.set_inverted(saved_inv);
    }

    void StatusBar::draw_tabs() {
        bool saved_inv = _terminal.inverted();
        _terminal.move_to({0, 0});
        _terminal.set_inverted(true);

        uint16_t end = draw_tab_strip();

        // The active tab is wider (N:NAME), so switching to a shorter-named page
        // shrinks the strip; clear the freed columns (still reverse video).
        _terminal.set_inverted(true);
        if (end < _tabs_end_col) _terminal.fill_spaces(_tabs_end_col);
        _tabs_end_col = end;

        _terminal.set_inverted(saved_inv);
    }

    uint16_t StatusBar::draw_tab_strip() {
        // Leading blank column, then one tab per page: just the 1-based number,
        // expanded to "N:NAME" for the active page. Single-space separated. Only
        // the active tab's "N:NAME" is normal video; the bar (and separators)
        // stay reverse video.
        _terminal.set_inverted(true);
        _terminal.write_chars(" ", 1);
        uint16_t col = 1;
        for (uint8_t i = 0; i < _console.page_count(); i++) {
            const char* nm = _console.page_name(i);
            if (nm == nullptr) continue;
            char tab[24];
            int n = (i == _console.active_page())
                ? snprintf(tab, sizeof(tab), "%u:%s", (unsigned)(i + 1), nm)
                : snprintf(tab, sizeof(tab), "%u", (unsigned)(i + 1));
            if (n < 0) continue;
            uint16_t len = (uint16_t)Utils::min(n, (int)sizeof(tab) - 1);
            _terminal.set_inverted(i != _console.active_page());
            _terminal.write_chars(tab, len);
            _terminal.set_inverted(true);
            _terminal.write_chars(" ", 1);   // separator, reverse video
            col += (uint16_t)(len + 1);
        }
        return col;
    }

    // Field painters

    void StatusBar::update_metrics() {
        char metrics[METRICS_LEN];
        int mlen = build_metrics(metrics, sizeof(metrics));
        // mlen is constant (fixed-width fields), so a byte index maps to a fixed col.
        uint16_t visible = (uint16_t)Utils::min((int)_terminal.cols(), mlen);
        if (visible == 0) return;
        uint16_t metrics_col = (uint16_t)(_terminal.cols() - visible);
        // The bar is reverse video except the live readings (cpu/stk), so the
        // mask inverts everything but the value columns. Paint the whole field;
        // the diff drops the cells that did not move.
        uint32_t hi = 0;
        for (uint16_t i = 0; i < visible; ++i)
            if (!metric_value_col(i)) hi |= (1u << i);
        _terminal.paint_field_runs(0, metrics_col, metrics, visible, hi);
    }

    void StatusBar::update_clock() {
        // Wall-clock HH:MM:SS. The bar is the only place it appears: it is on
        // every page, so no page repeats it.
        Utils::CalendarTime wc = Utils::cal_time_ms(HAL::Clock::uptime_ms());
        char clk[16];
        snprintf(clk, sizeof(clk), "%02u:%02u:%02u",
                 (unsigned)wc.hour, (unsigned)wc.minute, (unsigned)wc.second);

        // Centre the field; skip on a terminal too narrow to fit it clear of the
        // right-aligned metrics.
        uint16_t cols = _terminal.cols();
        if (cols < CLOCK_LEN) return;
        uint16_t clock_col   = (uint16_t)((cols - CLOCK_LEN) / 2);
        uint16_t metrics_col = (cols > (uint16_t)(METRICS_LEN - 1))
                                   ? (uint16_t)(cols - (METRICS_LEN - 1)) : 0;
        if ((uint16_t)(clock_col + CLOCK_LEN) > metrics_col) return;

        // hi = 0: normal video, so the digits stand out against the reverse bar.
        _terminal.paint_field_runs(0, clock_col, clk, CLOCK_LEN, 0u);
    }

    int StatusBar::build_metrics(char* buf, size_t cap) const {
        // The scheduler context may be absent (a site can wire a terminal
        // without one); show 0% rather than dereferencing null.
        unsigned cpu = 0, stk = 0;
        const char* run = "-";
        if (_sched != nullptr) {
            cpu = (unsigned)lroundf(_sched->cpu_load_ema * 100.0f);
            stk = (unsigned)lroundf(_sched->stack_usage * 100.0f);
            run = Scheduler::run_state_name(_sched->run_state);
        }
        if (cpu > 999) cpu = 999;
        if (stk > 999) stk = 999;
        return snprintf(buf, cap, "run:%5s cpu:%3u%% stk:%3u%% ", run, cpu, stk);
    }

}}

#endif // BOTANIK_FEATURE_CONSOLE
