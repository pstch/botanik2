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

#include "libbotanik_config.h"

#include "libbotanik/hal/platform.h"
#include "libbotanik/console/status_bar.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/console/field.h"
#include "libbotanik/utils/time.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/plcio/sync.h"

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
        : _c(console), _t(console.terminal()) {}

    // Cooperative paint

    void StatusBar::step(uint64_t now) {
        // The clock and metrics share TX_STATUS_FIELDS_PER_TICK and the depth
        // floor; the round-robin cursor rotates which repaints first so a tight
        // budget never favours the clock. A field repaints only on its cadence;
        // an unchanged field spends no budget.
        uint16_t budget = Config::TX_STATUS_FIELDS_PER_TICK;
        for (uint8_t v = 0; v < 2 && budget > 0 && _t.tx_below_cold(); v++) {
            if (_field == 0) {
                if (now - _clock_last_ms >= Config::PAGE_HOT_MS) {
                    _clock_last_ms = now;
                    if (update_clock()) budget--;
                }
            } else {
                if (now - _last_ms >= Config::STATUS_REFRESH_MS) {
                    // Advance the refresh clock only once the pass fully synced
                    // the on-screen metrics; a partial run leaves it for the
                    // next tick instead of waiting out another STATUS_REFRESH_MS.
                    if (update_metrics()) _last_ms = now;
                    budget--;
                }
            }
            _field ^= 1;
        }
    }

    void StatusBar::draw_row() {
        constexpr uint16_t mlen = METRICS_LEN - 1;   // field width (drop NUL)

        // Save the attribute only, not the cursor: every caller repositions
        // absolutely right after, so a cursor restore here would be wasted.
        bool saved_inv = _t.inverted();

        _t.move_to({0, 0});

        // Force the reverse-video background to emit before the tabs: a prior
        // tx_discard can leave the inverted shadow stale, which would make the
        // first tab's set_inverted() a wrong no-op.
        _t.force_inverted(true);

        // One blank column, then one " NAME " tab per page. The bar is reverse
        // video; the active tab is drawn normal so it stands out.
        _t.write_chars(" ", 1);
        for (uint8_t i = 0; i < _c.page_count(); i++) {
            const char* nm = _c.page_name(i);
            if (nm == nullptr) continue;
            _t.set_inverted(i != _c.active_page());
            _t.write_chars(" ", 1);
            _t.write_chars(nm, (uint16_t)strlen(nm));
            _t.write_chars(" ", 1);
        }
        _t.set_inverted(true);

        // Middle: spaces up to the metrics start column.
        uint16_t metrics_col = (_t.cols() > mlen) ? (uint16_t)(_t.cols() - mlen) : 0;
        _t.fill_spaces(metrics_col);

        // Right + centre: invalidate the diff caches, then let the field
        // painters draw - they know the per-field video and truncate narrow.
        invalidate();
        update_metrics(/*restore_pos=*/false);
        update_clock(/*restore_pos=*/false);

        // Restore only the attribute; the cursor is left where the metrics
        // parked it (the caller repositions absolutely).
        _t.set_inverted(saved_inv);
    }

    void StatusBar::draw_tab(uint8_t index) {
        const char* nm = _c.page_name(index);
        if (nm == nullptr) return;

        // Tab columns mirror draw_row(): one leading blank, then one " NAME "
        // (name length + 2) per page in order.
        uint16_t col = 1;
        for (uint8_t i = 0; i < index; i++) {
            const char* pn = _c.page_name(i);
            if (pn != nullptr) col += (uint16_t)(strlen(pn) + 2);
        }

        bool saved_inv = _t.inverted();
        _t.move_to({0, col});
        // Active tab normal video, others reverse; force the SGR to emit (same
        // stale-shadow hazard draw_row() guards).
        _t.force_inverted(index != _c.active_page());
        _t.write_chars(" ", 1);
        _t.write_chars(nm, (uint16_t)strlen(nm));
        _t.write_chars(" ", 1);
        _t.set_inverted(saved_inv);
    }

    void StatusBar::invalidate() {
        memset(_metrics_cache, 0, sizeof(_metrics_cache));
        memset(_clock_cache,   0, sizeof(_clock_cache));
    }

    // Field painters

    void StatusBar::paint_field(uint16_t col, const char* field, uint16_t len,
                                uint32_t emit, uint32_t hi, char* cache,
                                bool restore_pos) {
        AnsiTerminal::CursorSnapshot snap = _t.save_cursor();
        _t.paint_field_runs(0, col, field, len, emit, hi);
        memcpy(cache, field, (size_t)len);
        // Periodic refreshes restore the cursor to where the page left it;
        // draw_row passes false (its caller repositions absolutely).
        _t.restore_cursor(snap, restore_pos);
    }

    bool StatusBar::update_metrics(bool restore_pos) {
        char metrics[METRICS_LEN];
        int mlen = build_metrics(metrics, sizeof(metrics));

        // mlen is constant (fixed-width fields), so a byte index maps to a
        // fixed col.
        uint16_t visible = (uint16_t)std::min((int)_t.cols(), mlen);
        if (visible == 0) return true;   // nothing to draw == nothing to sync
        uint16_t metrics_col = (uint16_t)(_t.cols() - visible);

        // The bar is reverse video except the live readings (plc/cpu/stk), drawn
        // normal. Changed-column mask + highlight mask (inverted everywhere but
        // the value cols) over the visible field.
        uint32_t emit = Field::changed(metrics, _metrics_cache, visible, false);
        if (emit == 0) return true;   // already in sync
        uint32_t hi = 0;
        for (uint16_t i = 0; i < visible; ++i)
            if (!metric_value_col(i)) hi |= (1u << i);

        // Hide the cursor before painting so it can't blink across row 0 while
        // the bar updates (no-op if already hidden). The guaranteed floor fits a
        // whole field, so paint and cache it in one pass.
        bool saved_cur = _t.cursor_visible();
        _t.set_cursor_visible(false);
        paint_field(metrics_col, metrics, visible, emit, hi, _metrics_cache, restore_pos);
        _t.set_cursor_visible(saved_cur);   // back at the page position
        return true;
    }

    bool StatusBar::update_clock(bool restore_pos) {
        // Wall-clock HH:MM:SS (same source as the STATUS page's "Wall Clock").
        Utils::CalendarTime wc = Utils::cal_time_ms(HAL::Clock::uptime_ms());
        // Oversized scratch so snprintf can't truncation-warn; only the first
        // CLOCK_LEN cells ("HH:MM:SS") are diffed/emitted below.
        char clk[16];
        snprintf(clk, sizeof(clk), "%02u:%02u:%02u",
                 (unsigned)wc.hour, (unsigned)wc.minute, (unsigned)wc.second);

        // Centre the field, nudged 5 columns right; skip on a terminal too
        // narrow to fit it clear of the right-aligned metrics.
        uint16_t cols = _t.cols();
        if (cols < CLOCK_LEN) return false;
        uint16_t clock_col   = (uint16_t)((cols - CLOCK_LEN) / 2 + 5);
        uint16_t metrics_col = (cols > (uint16_t)(METRICS_LEN - 1))
                                   ? (uint16_t)(cols - (METRICS_LEN - 1)) : 0;
        if ((uint16_t)(clock_col + CLOCK_LEN) > metrics_col) return false;

        // Diff vs the cache, then paint the whole field in one pass (hi = 0:
        // normal video, so the digits stand out against the reverse-video bar).
        uint32_t emit = Field::changed(clk, _clock_cache, CLOCK_LEN, false);
        if (emit == 0) return false;

        paint_field(clock_col, clk, CLOCK_LEN, emit, 0u, _clock_cache, restore_pos);
        return true;
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
