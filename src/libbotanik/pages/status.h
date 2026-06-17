#ifndef PAGES_STATUS_H
#define PAGES_STATUS_H

/**
 * @file status.h
 * @brief StatusPage - full-screen runtime status display (template base).
 *
 * Templated on the site PLC output payload so derived pages get type-safe access
 * via data(). The base renders payload-independent system health: a left column
 * (PLC link, wall clock, CPU, serial buffers) and a right runtime-stats column
 * (uptime, overruns, jitter, stack); derived classes fill the rest via the
 * payload hooks.
 *
 * Drawing is diffed by MetricPage. Row 1 is a blank separator, rows 2..6 belong
 * to the base; derived content starts at PAYLOAD_ROW.
 */

#include <cstdint>
#include <cstdio>

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

        /** @brief First content row available to derived payload rendering. */
        static constexpr uint16_t PAYLOAD_ROW = 8;

        /** @brief System-band two-column geometry (80-col display). Left labels at
         *  col 2 / values at L_VAL; right labels at R_LBL / values at R_VAL. Right
         *  cache slots sit above the payload range so derived _cell() slots can't
         *  collide. */
        static constexpr uint16_t L_VAL = 16, L_W = 24;             // left values:  cols 16..39
        static constexpr uint16_t R_LBL = 42, R_VAL = 54, R_W = 23; // right: labels 42.., values 54..76
        static constexpr uint16_t R_SLOT0 = 28;                     // right-column slots: 28..31

        /** @name Draw hooks - override to customize the page.
         *  @{ */

        void draw_static() override {
            // Row 1 left blank as a separator above the header.
            _header(2, 2, "SYSTEM STATUS", 76);
            // Left column: PLC link + platform health. Rows 6 and 7 are left for the
            // site page to add more system fields (e.g. Network / NTP sync).
            _label(3, "PLC link");
            _label(4, "Wall Clock");
            _label(5, "CPU load");
            // Right column: scheduler/runtime stats, with Serial on the last line.
            _label_at(3, R_LBL, "Uptime");
            _label_at(4, R_LBL, "Overruns");
            _label_at(5, R_LBL, "Jitter");
            _label_at(6, R_LBL, "Stack");
            _label_at(7, R_LBL, "Serial");
            draw_payload_static();
        }

        void draw_dynamic() override {
            const SystemContext& ctx = _terminal.context();
            char buf[40];
            char fa[12], fb[12];

            // Left column --------------------------------------------------
            _cell(3, 3, L_VAL, ctx.plc ? plc_str(ctx.plc->status()) : "n/a", L_W);

            if (ctx.scheduler) {
                snprintf(buf, sizeof(buf), "%s%% (peak %sms)",
                         Utils::format_float(fa, sizeof(fa), ctx.scheduler->cpu_load_ema * 100.0f, 1, 4),
                         Utils::format_float(fb, sizeof(fb), ctx.scheduler->cpu_loop_us_max / 1000.0, 2, 6));
                _cell(5, 5, L_VAL, buf, L_W);
            }

            // Right column: runtime/scheduler stats ------------------------
            if (ctx.scheduler) {
                snprintf(buf, sizeof(buf), "%lu", (unsigned long)ctx.scheduler->overruns);
                _cell(R_SLOT0 + 1, 4, R_VAL, buf, R_W);
                snprintf(buf, sizeof(buf), "%sms (peak %sms)",
                         Utils::format_float(fa, sizeof(fa), ctx.scheduler->cpu_jitter_us_recent / 1000.0, 2, 4),
                         Utils::format_float(fb, sizeof(fb), ctx.scheduler->cpu_jitter_us_max / 1000.0, 2, 5));
                _cell(R_SLOT0 + 2, 5, R_VAL, buf, R_W);
                snprintf(buf, sizeof(buf), "%s%% (%5lu B)",
                         Utils::format_float(fa, sizeof(fa), ctx.scheduler->stack_usage_ema * 100.0f, 1, 4),
                         (unsigned long)ctx.scheduler->stack_bytes_max);
                _cell(R_SLOT0 + 3, 6, R_VAL, buf, R_W);
            }

            draw_payload_dynamic();
        }

        /** @brief Hot fields: wall clock and uptime both tick once per second (see
         *  MetricPage::tick()). Drawn un-highlighted since they change every tick. */
        void draw_hot() override {
            char buf[40];
            // Wall clock: current UTC time, down to the second.
            Utils::CalendarTime wc = Utils::cal_time_ms(HAL::uptime_ms());
            snprintf(buf, sizeof(buf), "%02u/%02u/%02u %02u:%02u:%02u",
                     wc.year % 100, wc.month, wc.day, wc.hour, wc.minute, wc.second);
            _cell(4, 4, L_VAL, buf, L_W, false);

            fmt_uptime(buf, sizeof(buf), HAL::uptime_ms());
            _cell(R_SLOT0, 3, R_VAL, buf, R_W, false);

            // Serial buffer depths (right column, last line): change often enough to
            // want the per-second refresh, but settle between bursts, so keep the
            // change highlight. Single-spaced so it fits the R_W-wide right column.
            // Slot 27 sits just below the R_SLOT0..R_SLOT0+3 runtime-stats slots.
            snprintf(buf, sizeof(buf), "TX %4u/%u RX %3u/%u",
                     _terminal.tx_used(), AnsiTerminal::tx_capacity(),
                     _terminal.rx_used(), AnsiTerminal::rx_capacity());
            _cell(27, 7, R_VAL, buf, R_W);
        }

        /** @brief Site-specific labels/headers (drawn once). Base: no-op. */
        virtual void draw_payload_static() {}
        /** @brief Site-specific values (refreshed periodically). Base: no-op. */
        virtual void draw_payload_dynamic() {}

        /** @brief Typed PLC output payload for derived rendering (may be null). */
        const OutputData* data() const { return _data; }

        /** @} */

    public:
        /** @brief Construct a StatusPage bound to a terminal and PLC payload. */
        StatusPage(AnsiTerminal& terminal, const OutputData* data)
            : MetricPage(terminal, 16), _data(data) {
            _hot_ms = Config::PAGE_HOT_MS;   // wall clock + uptime tick every second
        }

        const char* name() const override { return "STATUS"; }
    };

}}

#endif // PAGES_STATUS_H
