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

    // Coalescing keys for the two chrome fields (owner 0xF001, not a page).
    static const uint32_t KEY_CLOCK   = 0xF0010001u;
    static const uint32_t KEY_METRICS = 0xF0010002u;

    void StatusBar::step(uint64_t now) {
        // Refresh the clock and metrics on their own cadence; each submits a
        // coalescing transaction that the scheduler paces. No per-tick budget or
        // round-robin here - the TX queue owns priority and pacing.
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

        // Force the reverse-video background to emit before the tabs: a prior
        // tx_discard can leave the inverted shadow stale, which would make the
        // first tab's set_inverted() a wrong no-op.
        _t.force_inverted(true);

        // Compact tab strip: "1 2 3:LOGS 4 5" - only the active page shows its
        // name. Records the strip extent for draw_tabs()'s shrink handling.
        _tabs_end_col = draw_tab_strip();
        _t.set_inverted(true);

        // Middle: spaces up to the metrics start column.
        uint16_t metrics_col = (_t.cols() > mlen) ? (uint16_t)(_t.cols() - mlen) : 0;
        _t.fill_spaces(metrics_col);

        // Right + centre: invalidate the diff caches, then submit the two fields
        // as transactions (they drain over this direct background next tick).
        invalidate();
        update_metrics();
        update_clock();

        // Restore only the attribute; the cursor is left where fill_spaces
        // parked it (the caller repositions absolutely).
        _t.set_inverted(saved_inv);
    }

    void StatusBar::draw_tabs() {
        bool saved_inv = _t.inverted();
        _t.move_to({0, 0});
        // Force the reverse-video background to emit before the tabs (same
        // stale-shadow hazard draw_row() guards).
        _t.force_inverted(true);

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

    void StatusBar::invalidate() {
        // A full redraw repaints the background under both fields, so any
        // in-flight submitted-but-not-drawn transaction is now moot: clear the
        // drawn caches AND the pending flags so the next refresh diffs from
        // scratch (drop_owner on the switch already dropped the queued tx).
        memset(_metrics_cache, 0, sizeof(_metrics_cache));
        memset(_clock_cache,   0, sizeof(_clock_cache));
        _metrics_sub_v = false;
        _clock_sub_v   = false;
    }

    // Field painters

    void StatusBar::submit_field(uint16_t col, const char* field, uint16_t len,
                                 uint32_t hi, uint32_t key, uint32_t emit) {
        // Only the changed cells (per-cell diff). The callers diff against the
        // *drawn* cache, so `emit` covers every cell pending since the last draw -
        // a coalesced-away predecessor leaves nothing stale.
        _t.tx_begin(TX_VALUE, key);
        _t.paint_field_runs(0, col, field, len, emit, hi);
        _t.tx_commit();
    }

    bool StatusBar::update_metrics() {
        // Promote the last submitted field to "drawn" once its transaction drained.
        if (_metrics_sub_v && !_t.tx_has_key(TX_VALUE, KEY_METRICS)) {
            memcpy(_metrics_cache, _metrics_sub, METRICS_LEN);
            _metrics_sub_v = false;
        }
        char metrics[METRICS_LEN];
        int mlen = build_metrics(metrics, sizeof(metrics));

        // mlen is constant (fixed-width fields), so a byte index maps to a
        // fixed col.
        uint16_t visible = (uint16_t)std::min((int)_t.cols(), mlen);
        if (visible == 0) return true;   // nothing to draw == nothing to sync
        uint16_t metrics_col = (uint16_t)(_t.cols() - visible);

        // The bar is reverse video except the live readings (plc/cpu/stk), drawn
        // normal. Diff against the drawn cache; highlight mask inverts all but the
        // value cols.
        uint32_t emit = Field::changed(metrics, _metrics_cache, visible, false);
        if (emit == 0) return true;   // already in sync
        uint32_t hi = 0;
        for (uint16_t i = 0; i < visible; ++i)
            if (!metric_value_col(i)) hi |= (1u << i);

        submit_field(metrics_col, metrics, visible, hi, KEY_METRICS, emit);
        memcpy(_metrics_sub, metrics, (size_t)visible);   // pending until it drains
        _metrics_sub_v = true;
        return true;
    }

    bool StatusBar::update_clock() {
        // Promote the last submitted clock to "drawn" once its transaction drained.
        if (_clock_sub_v && !_t.tx_has_key(TX_VALUE, KEY_CLOCK)) {
            memcpy(_clock_cache, _clock_sub, CLOCK_LEN);
            _clock_sub_v = false;
        }
        // Wall-clock HH:MM:SS (same source as the STATUS page's "Wall Clock").
        Utils::CalendarTime wc = Utils::cal_time_ms(HAL::Clock::uptime_ms());
        // Oversized scratch so snprintf can't truncation-warn; only the first
        // CLOCK_LEN cells ("HH:MM:SS") are diffed/emitted below.
        char clk[16];
        snprintf(clk, sizeof(clk), "%02u:%02u:%02u",
                 (unsigned)wc.hour, (unsigned)wc.minute, (unsigned)wc.second);

        // Centre the field; the compact numeric tab strip no longer needs the
        // old rightward nudge. Skip on a terminal too narrow to fit it clear of
        // the right-aligned metrics.
        uint16_t cols = _t.cols();
        if (cols < CLOCK_LEN) return false;
        uint16_t clock_col   = (uint16_t)((cols - CLOCK_LEN) / 2);
        uint16_t metrics_col = (cols > (uint16_t)(METRICS_LEN - 1))
                                   ? (uint16_t)(cols - (METRICS_LEN - 1)) : 0;
        if ((uint16_t)(clock_col + CLOCK_LEN) > metrics_col) return false;

        // Diff vs the cache, then paint the whole field in one pass (hi = 0:
        // normal video, so the digits stand out against the reverse-video bar).
        uint32_t emit = Field::changed(clk, _clock_cache, CLOCK_LEN, false);
        if (emit == 0) return false;

        submit_field(clock_col, clk, CLOCK_LEN, 0u, KEY_CLOCK, emit);
        memcpy(_clock_sub, clk, CLOCK_LEN);   // pending until it drains
        _clock_sub_v = true;
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
