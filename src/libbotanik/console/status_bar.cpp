// REVIEW_35: TODO
/**
 * @file status_bar.cpp
 * @brief StatusBar implementation - row-0 tabs, clock, and metrics field.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

#include "../../libbotanik_config.h"

#include "../hal/platform.h"
#include "status_bar.h"
#include "console.h"
#include "terminal.h"
#include "../utils/time.h"
#include "../scheduler/scheduler.h"
#include "../plcio/sync.h"

namespace Botanik { namespace Terminal {

    // Value-cell columns in "PLC:xxx cpu:nnn% stk:nnn% " - drawn normal-video
    // against the reverse-video bar. One place tied to the build_metrics()
    // format so the layout and the highlight can't drift apart.
    static bool metric_value_col(int c) {
        return (c >= 4 && c <= 6)      // PLC status
            || (c >= 12 && c <= 15)    // cpu value + '%'
            || (c >= 21 && c <= 24);   // stk value + '%'
    }

    StatusBar::StatusBar(Console& console)
        : _c(console), _t(console.terminal()) {
        // Row 0 is the frame around the page: it should land before the page on
        // boot and on a page switch. Its routine churn costs almost nothing - the
        // clock and metrics rebuild once a second, some tens of bytes against a
        // budget of thousands, so ranking it above the page starves nothing.
        _t.set_row_priority(0, PRIO_CHROME);
    }

    // Cooperative paint


    void StatusBar::step(uint64_t now) {
        // Rebuild the clock and metrics on their own cadence and repaint the whole
        // field; the terminal's back-buffer diff emits only the cells that moved.
        if (now - _clock_last_ms >= Config::PAGE_HOT_MS) {
            _clock_last_ms = now;
            update_clock();
        }
        if (now - _last_ms >= Config::STATUS_REFRESH_MS) {
            _last_ms = now;
            update_metrics();
        }
    }

    void StatusBar::draw_row() {
        constexpr uint16_t mlen = METRICS_LEN - 1;   // field width (drop NUL)

        // Save the attribute only, not the cursor: every caller repositions
        // absolutely right after, so a cursor restore here would be wasted.
        bool saved_inv = _t.inverted();

        _t.move_to({0, 0});

        // Assert the reverse-video background (the row-0 bar).
        _t.set_inverted(true);

        // Compact tab strip: "1 2 3:LOGS 4 5" - only the active page shows its
        // name. Records the strip extent for draw_tabs()'s shrink handling.
        _tabs_end_col = draw_tab_strip();
        _t.set_inverted(true);

        // Middle: spaces up to the metrics start column.
        uint16_t metrics_col = (_t.cols() > mlen) ? (uint16_t)(_t.cols() - mlen) : 0;
        _t.fill_spaces(metrics_col);

        // Right + centre: paint the two live fields over the background.
        update_metrics();
        update_clock();

        // Restore only the attribute; the cursor is left where fill_spaces
        // parked it (the caller repositions absolutely).
        _t.set_inverted(saved_inv);
    }

    void StatusBar::draw_tabs() {
        bool saved_inv = _t.inverted();
        _t.move_to({0, 0});
        _t.set_inverted(true);

        uint16_t end = draw_tab_strip();

        // The active tab is wider (N:NAME), so switching to a shorter-named page
        // shrinks the strip; clear the freed columns (still reverse video).
        _t.set_inverted(true);
        if (end < _tabs_end_col) _t.fill_spaces(_tabs_end_col);
        _tabs_end_col = end;

        _t.set_inverted(saved_inv);
    }

    uint16_t StatusBar::draw_tab_strip() {
        // Leading blank column, then one tab per page: just the 1-based number,
        // expanded to "N:NAME" for the active page. Single-space separated. Only
        // the active tab's "N:NAME" is normal video; the bar (and separators)
        // stay reverse video.
        _t.set_inverted(true);
        _t.write_chars(" ", 1);
        uint16_t col = 1;
        for (uint8_t i = 0; i < _c.page_count(); i++) {
            const char* nm = _c.page_name(i);
            if (nm == nullptr) continue;
            char tab[24];
            int n = (i == _c.active_page())
                ? snprintf(tab, sizeof(tab), "%u:%s", (unsigned)(i + 1), nm)
                : snprintf(tab, sizeof(tab), "%u", (unsigned)(i + 1));
            if (n < 0) continue;
            uint16_t len = (uint16_t)std::min(n, (int)sizeof(tab) - 1);
            _t.set_inverted(i != _c.active_page());
            _t.write_chars(tab, len);
            _t.set_inverted(true);
            _t.write_chars(" ", 1);   // separator, reverse video
            col += (uint16_t)(len + 1);
        }
        return col;
    }

    // Field painters

    void StatusBar::submit_field(uint16_t col, const char* field, uint16_t len,
                                 uint32_t hi) {
        // Paint the whole field (all cells); the terminal's diff drops the ones
        // that did not move. hi segments the reverse-video runs.
        _t.paint_field_runs(0, col, field, len, hi);
    }

    void StatusBar::update_metrics() {
        char metrics[METRICS_LEN];
        int mlen = build_metrics(metrics, sizeof(metrics));
        // mlen is constant (fixed-width fields), so a byte index maps to a fixed col.
        uint16_t visible = (uint16_t)std::min((int)_t.cols(), mlen);
        if (visible == 0) return;
        uint16_t metrics_col = (uint16_t)(_t.cols() - visible);
        // The bar is reverse video except the live readings (plc/cpu/stk), drawn
        // normal: highlight mask inverts all but the value cols.
        uint32_t hi = 0;
        for (uint16_t i = 0; i < visible; ++i)
            if (!metric_value_col(i)) hi |= (1u << i);
        submit_field(metrics_col, metrics, visible, hi);
    }

    void StatusBar::update_clock() {
        // Wall-clock HH:MM:SS (same source as the STATUS page's "Wall Clock").
        Utils::CalendarTime wc = Utils::cal_time_ms(HAL::Clock::uptime_ms());
        char clk[16];
        snprintf(clk, sizeof(clk), "%02u:%02u:%02u",
                 (unsigned)wc.hour, (unsigned)wc.minute, (unsigned)wc.second);

        // Centre the field; skip on a terminal too narrow to fit it clear of the
        // right-aligned metrics.
        uint16_t cols = _t.cols();
        if (cols < CLOCK_LEN) return;
        uint16_t clock_col   = (uint16_t)((cols - CLOCK_LEN) / 2);
        uint16_t metrics_col = (cols > (uint16_t)(METRICS_LEN - 1))
                                   ? (uint16_t)(cols - (METRICS_LEN - 1)) : 0;
        if ((uint16_t)(clock_col + CLOCK_LEN) > metrics_col) return;

        // hi = 0: normal video, so the digits stand out against the reverse bar.
        submit_field(clock_col, clk, CLOCK_LEN, 0u);
    }

    int StatusBar::build_metrics(char* buf, size_t cap) const {
        const SystemContext& ctx = _c.context();
        // No PLC context wired: show a "---" placeholder rather than a blank
        // field.
        const char* plc_str = "---";
#if BOTANIK_FEATURE_PLC
        if (ctx.plc != nullptr) switch (ctx.plc->status()) {
            case PlcIO::PLC_UNKNOWN:  plc_str = "UNK"; break;
            case PlcIO::PLC_RUNNING:  plc_str = "RUN"; break;
            case PlcIO::PLC_TIMEOUT:  plc_str = "TMO"; break;
            case PlcIO::PLC_FAULT:    plc_str = "FLT"; break;
            default:                  plc_str = "---"; break;
        }
#endif
        // The scheduler context may be absent (a site can wire a terminal
        // without one); show 0% rather than dereferencing null.
        unsigned cpu = 0, stk = 0;
        if (ctx.scheduler != nullptr) {
            cpu = (unsigned)lroundf(ctx.scheduler->cpu_load_ema * 100.0f);
            stk = (unsigned)lroundf(ctx.scheduler->stack_usage * 100.0f);
        }
        if (cpu > 999) cpu = 999;
        if (stk > 999) stk = 999;
        return snprintf(buf, cap, "PLC:%s cpu:%3u%% stk:%3u%% ", plc_str, cpu, stk);
    }

}}
