#ifndef PAGES_STATUS_H
#define PAGES_STATUS_H

/**
 * @file status.h
 * @brief StatusPage - full-screen runtime status display (template base).
 *
 * Templated on the site PLC output payload for type-safe data() access. The
 * base renders system health as a two-column grid (rows 3..6): left PLC link /
 * CPU load / jitter / stack, right wall clock / uptime / task period / console
 * TX. Values are right-aligned so label and value read as separate sub-columns.
 * Rows 7..8 (network / NTP / MQTT) and below come from the payload hooks
 * (site-specific).
 *
 * Drawing is diffed by MetricPage; derived content starts at PAYLOAD_ROW. The
 * alarm set is shown on the terminal-owned bottom bar, not here.
 */

#include <cstdint>
#include <cstdio>

#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/metric_page.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/utils/text.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen status display base (see file header). */
    template<typename OutputData>
    class StatusPage : public MetricPage {
    protected:
        /** @name State
         *  @{ */

        /** @brief Typed PLC output payload (may be null). */
        const OutputData* _data;

        /** @} */

        /** @brief First row below the system band, for the derived page's own
         *         blocks. */
        static constexpr uint16_t PAYLOAD_ROW = 10;

        /** @brief System-band two-column geometry (80-col display): left labels
         *  at col 2 / values right-aligned in L_VAL..L_VAL+L_W; right labels at
         *  R_LBL / values in R_VAL..R_VAL+R_W. */
        // left values: cols 16..39
        static constexpr uint16_t L_LBL = 2, L_VAL = 16, L_W = 24;
        // right: labels 42.., values 54..76
        static constexpr uint16_t R_LBL = 42, R_VAL = 54, R_W = 23;

        /** @brief Diff-cache slots for the base cells, disjoint from the
         *  payload slots a derived page uses; slots 1, 2, 6, 7, 9, 31 are left
         *  free for its network band. */
        static constexpr uint16_t SLOT_PLC = 3, SLOT_CPU = 4, SLOT_JITTER = 5,
                                  SLOT_STACK = 8;
        static constexpr uint16_t SLOT_WALL = 27, SLOT_UPTIME = 28, SLOT_TASK = 29,
                                  SLOT_CONSOLE = 30;

        /** @brief Right-justify @p s into a @p width-wide field. Returns
         *  @p buf. */
        static const char* rjust(char* buf, size_t cap, uint16_t width, const char* s) {
            if (width >= cap) width = (uint16_t)(cap - 1);
            snprintf(buf, cap, "%*s", (int)width, s);
            return buf;
        }

        /** @name Draw hooks - override to customize the page.
         *  @{ */

        void draw_static() override {
            // Row 1 blank separator above the header.
            _header(2, 2, "SYSTEM STATUS", 76);
            // Left column: PLC link + platform health. _label_at (no
            // clear-to-eol) so it never wipes the right column on the same row.
            _label_at(3, L_LBL, "PLC link");
            _label_at(4, L_LBL, "CPU load");
            _label_at(5, L_LBL, "CPU jitter");
            _label_at(6, L_LBL, "Stack");
            // Right column: scheduler/runtime stats.
            _label_at(3, R_LBL, "Wall clock");
            _label_at(4, R_LBL, "Uptime");
            _label_at(5, R_LBL, "Task period");
            _label_at(6, R_LBL, "Console TX");
            // Rows 7..8 (network / NTP / MQTT) are site-specific.
            draw_payload_static();
        }

        void draw_dynamic() override {
            const SystemContext& ctx = _terminal.context();
            char val[40], fld[40];
            char fa[12];

            // Cold fields only - EMAs and clocks are hot (see draw_hot()).
            _cell(SLOT_PLC, 3, L_VAL,
                  rjust(fld, sizeof(fld), L_W,
                        ctx.plc ? plc_str(ctx.plc->status()) : "n/a"), L_W);

            if (ctx.scheduler) {
                // Stack: EMA leads, all-time peak free in parens.
                char fs[12];
                snprintf(val, sizeof(val), "%5s%% (%s free)",
                         Utils::format_float(fa, sizeof(fa), ctx.scheduler->stack_usage * 100.0f, 1),
                         Utils::format_size(fs, sizeof(fs), ctx.scheduler->stack_bytes_free));
                _cell(SLOT_STACK, 6, L_VAL, rjust(fld, sizeof(fld), L_W, val), L_W);
                // Task period: nominal loop period + real tick rate (RTPS =
                // ticks per real second; under sim dilation shows sustain).
                // Count width-padded so " RTPS)" never shifts.
                snprintf(val, sizeof(val), "%lums (%3lu RTPS)",
                         (unsigned long)Config::TASK_PERIOD_MS,
                         (unsigned long)ctx.scheduler->ticks_per_second);
                _cell(SLOT_TASK, 5, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W);
            }

            draw_payload_dynamic();
        }

        /** @brief Hot fields: wall clock + uptime tick once per second, and CPU
         *  load + jitter ride along (their EMAs move every tick). All
         *  un-highlighted (the highlight would churn on never-settling values)
         *  except console TX, which settles between bursts. */
        void draw_hot() override {
            const SystemContext& ctx = _terminal.context();
            char val[40], fld[40];
            char fa[12], fb[12];

            // Wall clock (right): current time to the second.
            Utils::CalendarTime wc = Utils::cal_time_ms(HAL::uptime_ms());
            snprintf(val, sizeof(val), "%02u/%02u/%02u %02u:%02u:%02u",
                     wc.year % 100, wc.month, wc.day, wc.hour, wc.minute, wc.second);
            _cell(SLOT_WALL, 3, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W, false);
            fmt_uptime(val, sizeof(val), HAL::uptime_ms());
            _cell(SLOT_UPTIME, 4, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W, false);

            // CPU load + jitter (left): EMA leads, peak ms in parens.
            if (ctx.scheduler) {
                snprintf(val, sizeof(val), "%5s%% (peak %5sms)",
                         Utils::format_float(fa, sizeof(fa), ctx.scheduler->cpu_load_ema * 100.0f, 1),
                         Utils::format_float(fb, sizeof(fb), ctx.scheduler->cpu_loop_us_max / 1000.0, 2));
                _cell(SLOT_CPU, 4, L_VAL, rjust(fld, sizeof(fld), L_W, val), L_W, false);
                snprintf(val, sizeof(val), "%5s%% (peak %5sms)",
                         Utils::format_float(fa, sizeof(fa), ctx.scheduler->cpu_jitter_ema * 100.0f, 1),
                         Utils::format_float(fb, sizeof(fb), ctx.scheduler->cpu_jitter_us_max / 1000.0, 2));
                _cell(SLOT_JITTER, 5, L_VAL, rjust(fld, sizeof(fld), L_W, val), L_W, false);
            }

            // Console TX (right): ring depth / capacity + cumulative drops.
            // Settles between bursts, so keep the highlight. Drops clamped
            // against field overflow, depth and drops width-padded so the
            // separators hold a fixed column.
            uint32_t drops = _terminal.tx_dropped();
            if (drops > 9999) drops = 9999;
            snprintf(val, sizeof(val), "%4u/%u (drops: %4lu)",
                     _terminal.tx_used(), AnsiTerminal::tx_capacity(), (unsigned long)drops);
            _cell(SLOT_CONSOLE, 6, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W);
        }

        /** @brief Site-specific labels/headers (drawn once). Base: no-op. */
        virtual void draw_payload_static() {}
        /** @brief Site-specific values (refreshed periodically). Base:
         *  no-op. */
        virtual void draw_payload_dynamic() {}

        /** @brief Typed PLC output payload for derived rendering (may be
         *  null). */
        const OutputData* data() const { return _data; }

        /** @} */

    public:
        /** @brief Bind to a terminal and PLC payload. */
        StatusPage(AnsiTerminal& terminal, const OutputData* data)
            : MetricPage(terminal, 16), _data(data) {
            // wall clock + uptime tick per second
            _hot_ms = Config::PAGE_HOT_MS;
        }

        const char* name() const override { return "STATUS"; }
    };

}}

#endif // PAGES_STATUS_H
