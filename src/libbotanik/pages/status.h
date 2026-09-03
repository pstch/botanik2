// REVIEW_35: TODO
#ifndef PAGES_STATUS_H
#define PAGES_STATUS_H

/**
 * @file status.h
 * @brief StatusPage - full-screen runtime status display.
 *
 * A site subclasses it and overrides the two payload hooks; there is no type
 * parameter and no data() - a derived page holds whatever it needs itself. The
 * base renders system health as a
 * two-column grid (rows 3..6): left CPU load / jitter / stack, right wall clock
 * / uptime / task period / console TX. Values are right-aligned so label and
 * value read as separate sub-columns. Rows 7..8 (network / NTP / MQTT) render
 * from the live components (via SystemContext) when BOTANIK_FEATURE_NETWORK is
 * set; everything below comes from the site-specific payload hooks.
 *
 * Drawing is diffed by MetricPage; derived content starts at PAYLOAD_ROW. The
 * alarm set is shown on the terminal-owned bottom bar, not here.
 */

#include <cstdint>
#include <cstdio>

#include "libbotanik/config/scheduler.h"
#include "libbotanik/config/terminal.h"
#include "libbotanik_features.h"

#include "libbotanik/core/context.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/metric_page.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/utils/text.h"
#include "libbotanik/utils/time.h"

#if BOTANIK_FEATURE_NETWORK
#include "libbotanik/network/network.h"
#endif
#if BOTANIK_FEATURE_NTP
#include "libbotanik/network/ntp.h"
#endif
#if BOTANIK_FEATURE_MQTT
#include "libbotanik/network/mqtt.h"
#endif

namespace Botanik { namespace Pages {

    /** @brief Full-screen status display base (see file header).
     *
     *  Not a template. It used to take the site's data struct as a parameter,
     *  but it never dereferenced one: the pointer was stored and handed back
     *  through data(), so the whole template existed to give a subclass a typed
     *  accessor. A subclass can hold its own typed pointer, which is one member
     *  in the one place that knows the type. */
    class StatusPage : public MetricPage {
    protected:
        /** @name State
         *  @{ */

        /** @brief The subsystem handles the system band renders from. Injected,
         *  not read back off the console. */
        const SystemContext* _sys;

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
            // Left column: platform health. _label_at (no clear-to-eol) so it
            // never wipes the right column on the same row.
            _label_at(3, L_LBL, "CPU load");
            _label_at(4, L_LBL, "CPU jitter");
            _label_at(5, L_LBL, "Stack");
            // Right column: scheduler/runtime stats.
            _label_at(3, R_LBL, "Wall clock");
            _label_at(4, R_LBL, "Uptime");
            _label_at(5, R_LBL, "Task period");
            _label_at(6, R_LBL, "Console TX");
#if BOTANIK_FEATURE_NETWORK
            // Rows 7..8: network band, rendered from the live components.
            _label_at(7, L_LBL, "Network");
            _label_at(8, L_LBL, "Network IP");
            _label_at(7, R_LBL, "NTP sync");
            _label_at(8, R_LBL, "MQTT");
#endif
            draw_payload_static();
        }

        void draw_dynamic() override {
            const SystemContext& ctx = *_sys;
            char val[40], fld[40];
            char fa[12];

            // Cold fields only - EMAs and clocks are hot (see draw_hot()).
            if (ctx.scheduler) {
                // Stack: EMA leads, all-time peak free in parens.
                char fs[12];
                snprintf(val, sizeof(val), "%5s%% (%s free)",
                         Utils::format_float(fa, sizeof(fa), ctx.scheduler->stack_usage * 100.0f, 1),
                         Utils::format_size(fs, sizeof(fs), ctx.scheduler->stack_bytes_free));
                _cell(5, L_VAL, rjust(fld, sizeof(fld), L_W, val), L_W);
                // Task period: nominal loop period + real tick rate (RTPS =
                // ticks per real second).
                // Count width-padded so " RTPS)" never shifts.
                snprintf(val, sizeof(val), "%lums (%3lu RTPS)",
                         (unsigned long)Config::TASK_PERIOD_MS,
                         (unsigned long)ctx.scheduler->ticks_per_second);
                _cell(5, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W);
            }

#if BOTANIK_FEATURE_NETWORK
            // Network band (rows 7..8) from the live components. Each cell
            // needs its OWN gate, not NETWORK's: SystemContext declares ntp /
            // mqtt under their own flags, so with NETWORK on and MQTT off the
            // member does not exist to null-check. The rows stay either way and
            // read "off", so the grid does not reflow between builds.
            _cell(7, L_VAL,
                  rjust(fld, sizeof(fld), L_W,
                        ctx.network ? ctx.network->status_str() : "off"), L_W);
            HAL::Network::Ip4 ip;
            if (ctx.network && ctx.network->ip(&ip))
                HAL::Network::ip4_to_str(ip, val, sizeof(val));
            else
                snprintf(val, sizeof(val), "no link");
            _cell(8, L_VAL, rjust(fld, sizeof(fld), L_W, val), L_W);

#if BOTANIK_FEATURE_NTP
            if (ctx.ntp && ctx.ntp->synced()) {
                // Worst case "-2147483.648" (12) -> "synced (.. s)" fills R_W.
                char nf[16];
                snprintf(val, sizeof(val), "synced (%s s)",
                         Utils::format_float(nf, sizeof(nf), ctx.ntp->offset_ms() / 1000.0, 3));
            } else {
                snprintf(val, sizeof(val), "%s", ctx.ntp ? ctx.ntp->status_str() : "off");
            }
#else
            snprintf(val, sizeof(val), "off");
#endif
            _cell(7, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W);
            // State plus the age of the last broker contact (connect or
            // keepalive) - "ready (3s)" while live, growing once it drops.
#if BOTANIK_FEATURE_MQTT
            if (ctx.mqtt) {
                uint64_t last = ctx.mqtt->last_contact_ms();
                if (last != 0) {
                    char nf[12];
                    uint32_t age_s = (uint32_t)((HAL::Clock::uptime_ms() - last) / 1000);
                    // floor_min clamps sub-minute to "0 m", so this cold field
                    // changes at most once a minute rather than every second.
                    snprintf(val, sizeof(val), "%s (%s)", ctx.mqtt->status_str(),
                             Utils::format_age(nf, sizeof(nf), age_s, false, true));
                } else {
                    snprintf(val, sizeof(val), "%s", ctx.mqtt->status_str());
                }
            } else {
                snprintf(val, sizeof(val), "off");
            }
#else
            snprintf(val, sizeof(val), "off");
#endif
            _cell(8, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W);
#endif

            draw_payload_dynamic();
        }

        /** @brief Hot fields: wall clock + uptime tick once per second, and CPU
         *  load + jitter ride along (their EMAs move every tick). All
         *  un-highlighted (the highlight would churn on never-settling values)
         *  except console TX, which settles between bursts. */
        void draw_hot() override {
            const SystemContext& ctx = *_sys;
            char val[40], fld[40];
            char fa[12], fb[12];

            // Wall clock (right): current time to the second.
            Utils::CalendarTime wc = Utils::cal_time_ms(HAL::Clock::uptime_ms());
            snprintf(val, sizeof(val), "%02u/%02u/%02u %02u:%02u:%02u",
                     wc.year % 100, wc.month, wc.day, wc.hour, wc.minute, wc.second);
            _cell(3, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W, false);
            fmt_uptime(val, sizeof(val), HAL::Clock::uptime_ms());
            _cell(4, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W, false);

            // CPU load + jitter (left): EMA leads, peak ms in parens.
            if (ctx.scheduler) {
                snprintf(val, sizeof(val), "%5s%% (peak %5sms)",
                         Utils::format_float(fa, sizeof(fa), ctx.scheduler->cpu_load_ema * 100.0f, 1),
                         Utils::format_float(fb, sizeof(fb), ctx.scheduler->cpu_loop_us_max / 1000.0, 2));
                _cell(3, L_VAL, rjust(fld, sizeof(fld), L_W, val), L_W, false);
                snprintf(val, sizeof(val), "%5s%% (peak %5sms)",
                         Utils::format_float(fa, sizeof(fa), ctx.scheduler->cpu_jitter_ema * 100.0f, 1),
                         Utils::format_float(fb, sizeof(fb), ctx.scheduler->cpu_jitter_us_max / 1000.0, 2));
                _cell(4, L_VAL, rjust(fld, sizeof(fld), L_W, val), L_W, false);
            }

            // Console TX (right): ring depth / capacity + cumulative drops.
            // Settles between bursts, so keep the highlight. Drops clamped
            // against field overflow; both width-padded so the separators hold
            // a fixed column.
            uint32_t drops = _terminal.serial().tx_dropped();
            if (drops > 9999) drops = 9999;
            snprintf(val, sizeof(val), "%4u/%u (drops: %4lu)",
                     _terminal.tx_used(), AnsiTerminal::tx_capacity(), (unsigned long)drops);
            _cell(6, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W);
        }

        /** @brief Site-specific labels/headers (drawn once). Base: no-op. */
        virtual void draw_payload_static() {}
        /** @brief Site-specific values (refreshed periodically). Base:
         *  no-op. */
        virtual void draw_payload_dynamic() {}

        /** @} */

    public:
        /** @brief Bind to a terminal, the subsystem handles the system band
         *  reads, and the site data struct. */
        StatusPage(Console& console, const SystemContext* sys)
            : MetricPage(console), _sys(sys) {
            // wall clock + uptime tick per second
            _hot_ms = Config::PAGE_HOT_MS;
        }

        const char* name() const override { return "STATUS"; }
    };

}}

#endif // PAGES_STATUS_H
