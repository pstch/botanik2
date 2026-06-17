#ifndef PAGES_STATUS_H
#define PAGES_STATUS_H

/**
 * @file status.h
 * @brief StatusPage - full-screen runtime status display (template base).
 *
 * Templated on the site PLC output payload so derived pages get type-safe access
 * via data(). The base renders payload-independent system health as a two-column
 * grid (rows 3..6): left column PLC link / CPU load / CPU jitter / stack, right
 * column wall clock / uptime / task period / console TX. Values are right-aligned
 * within each column so the label and the value read as separate sub-columns.
 * Rows 7..8 (network / NTP / MQTT) and everything below come from the payload
 * hooks, since that data is site-specific.
 *
 * Drawing is diffed by MetricPage. Row 1 is a blank separator, rows 2..6 belong
 * to the base; derived content starts at PAYLOAD_ROW.
 */

#include <cstdint>
#include <cstdio>

#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/alerts/alerts.h"
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
        /** @brief Alert source for the bottom ribbon (may be null). */
        const Alerts::AlertModel* _alerts;

        /** @name Alert ribbon (bottom row)
         *  The ribbon is a self-contained hot field, repainted only when the
         *  unmasked alert set or the active count changes, and only while the TX
         *  ring is shallow (the shared cold floor) so it interleaves cooperatively
         *  like the top status bar. No change-highlighting (the set is visible as a
         *  whole). _ribbon_dirty survives a deferred paint (floor closed) so a
         *  later tick retries.
         *  @{ */
        /** @brief Unmasked set last painted (bit i = AlertId i shown). */
        uint64_t _ribbon_sig   = 0;
        /** @brief Active count last painted (the header's "(XX)"). */
        uint8_t  _ribbon_count = 0;
        /** @brief A repaint is due (set on change/focus, cleared once painted). */
        bool     _ribbon_dirty = true;

        /** @} */
        /** @} */

        /** @brief First row below the system band, for the derived page's own
         *  content blocks (the band itself, rows 7..8, is also payload-drawn). */
        static constexpr uint16_t PAYLOAD_ROW = 10;

        /** @brief System-band two-column geometry (80-col display). Left labels at
         *  col 2 / values right-aligned within L_VAL..L_VAL+L_W; right labels at
         *  R_LBL / values right-aligned within R_VAL..R_VAL+R_W. */
        static constexpr uint16_t L_LBL = 2, L_VAL = 16, L_W = 24;  // left values:  cols 16..39
        static constexpr uint16_t R_LBL = 42, R_VAL = 54, R_W = 23; // right: labels 42.., values 54..76

        /** @brief Diff-cache slots for the base system cells. Kept disjoint from the
         *  payload slots a derived page uses (see GreenhouseStatusPage); slots 1, 2,
         *  6, 7, 9, 31 are left free for the derived network/NTP/MQTT band. */
        static constexpr uint16_t SLOT_PLC = 3, SLOT_CPU = 4, SLOT_JITTER = 5, SLOT_STACK = 8;
        static constexpr uint16_t SLOT_WALL = 27, SLOT_UPTIME = 28, SLOT_TASK = 29, SLOT_CONSOLE = 30;

        /** @brief Right-justify @p s into a @p width-wide field (system-band values
         *  sit flush right, separated from their left-hand label). Returns @p buf. */
        static const char* rjust(char* buf, size_t cap, uint16_t width, const char* s) {
            if (width >= cap) width = (uint16_t)(cap - 1);
            snprintf(buf, cap, "%*s", (int)width, s);
            return buf;
        }

        /** @name Draw hooks - override to customize the page.
         *  @{ */

        void draw_static() override {
            // Row 1 left blank as a separator above the header.
            _header(2, 2, "SYSTEM STATUS", 76);
            // Left column: PLC link + platform health (rows 3..6). _label_at (no
            // clear-to-eol) so it never wipes the right column on the same row.
            _label_at(3, L_LBL, "PLC link");
            _label_at(4, L_LBL, "CPU load");
            _label_at(5, L_LBL, "CPU jitter");
            _label_at(6, L_LBL, "Stack");
            // Right column: scheduler/runtime stats (rows 3..6).
            _label_at(3, R_LBL, "Wall clock");
            _label_at(4, R_LBL, "Uptime");
            _label_at(5, R_LBL, "Task period");
            _label_at(6, R_LBL, "Console TX");
            // Rows 7..8 (network / NTP / MQTT) are site-specific; drawn by the payload.
            draw_payload_static();
        }

        void draw_dynamic() override {
            const SystemContext& ctx = _terminal.context();
            char val[40], fld[40];
            char fa[12];

            // Cold fields only - the EMAs and clocks are hot (see draw_hot()).
            // PLC link (left).
            _cell(SLOT_PLC, 3, L_VAL,
                  rjust(fld, sizeof(fld), L_W, ctx.plc ? plc_str(ctx.plc->status()) : "n/a"), L_W);

            if (ctx.scheduler) {
                // Stack usage (left): EMA leads, all-time peak bytes in parens.
                snprintf(val, sizeof(val), "%5s%% (peak %5lu B)",
                         Utils::format_float(fa, sizeof(fa), ctx.scheduler->stack_usage_ema * 100.0f, 1),
                         (unsigned long)ctx.scheduler->stack_bytes_max);
                _cell(SLOT_STACK, 6, L_VAL, rjust(fld, sizeof(fld), L_W, val), L_W);
                // Task period (right): nominal loop period + achieved tick rate.
                snprintf(val, sizeof(val), "%lums (%lu tps)",
                         (unsigned long)Config::TASK_PERIOD_MS,
                         (unsigned long)ctx.scheduler->ticks_per_second);
                _cell(SLOT_TASK, 5, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W);
            }

            draw_payload_dynamic();
        }

        /** @brief Hot fields: wall clock and uptime tick once per second (see
         *  MetricPage::tick()), and CPU load + jitter ride along since their EMAs
         *  move every tick. All drawn un-highlighted - the change highlight would
         *  just churn on values that never settle - except console TX, which settles
         *  between bursts. */
        void draw_hot() override {
            const SystemContext& ctx = _terminal.context();
            char val[40], fld[40];
            char fa[12], fb[12];

            // Wall clock (right): current time, down to the second.
            Utils::CalendarTime wc = Utils::cal_time_ms(HAL::uptime_ms());
            snprintf(val, sizeof(val), "%02u/%02u/%02u %02u:%02u:%02u",
                     wc.year % 100, wc.month, wc.day, wc.hour, wc.minute, wc.second);
            _cell(SLOT_WALL, 3, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W, false);
            // Uptime (right).
            fmt_uptime(val, sizeof(val), HAL::uptime_ms());
            _cell(SLOT_UPTIME, 4, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W, false);

            // CPU load + jitter (left): the EMA leads, peak ms in parens.
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

            // Console TX (right): ring depth / capacity + cumulative drops. Settles
            // between bursts, so keep the change highlight. Drops are clamped so a
            // pathological count can't overflow the fixed-width field.
            uint32_t drops = _terminal.tx_dropped();
            if (drops > 9999) drops = 9999;
            snprintf(val, sizeof(val), "%u/%u (drops: %lu)",
                     _terminal.tx_used(), AnsiTerminal::tx_capacity(), (unsigned long)drops);
            _cell(SLOT_CONSOLE, 6, R_VAL, rjust(fld, sizeof(fld), R_W, val), R_W);
        }

        /** @brief Site-specific labels/headers (drawn once). Base: no-op. */
        virtual void draw_payload_static() {}
        /** @brief Site-specific values (refreshed periodically). Base: no-op. */
        virtual void draw_payload_dynamic() {}

        /** @brief Typed PLC output payload for derived rendering (may be null). */
        const OutputData* data() const { return _data; }

        /** @} */

        /** @name Alert ribbon
         *  @{ */

        /** @brief Bitset of currently-unmasked alerts (the ribbon's listed set). */
        uint64_t _unmasked_set() const {
            uint64_t m = 0;
            if (_alerts)
                for (uint8_t i = 0; i < _alerts->count(); i++)
                    if (_alerts->condition(i).unmasked) m |= Alerts::alert_bit(i);
            return m;
        }

        /** @brief Paint the bottom alert ribbon: a reverse-video bar with a header
         *  " /!\ ALERTS (XX) : " (XX = active count) then the unmasked alert codes
         *  in normal video, each followed by an inverted space (so codes read as a
         *  set against the bar, like the active tab). Filled to the row end. */
        void _draw_ribbon() {
            uint16_t rows = _terminal.rows();
            uint16_t cols = _terminal.cols();
            if (rows == 0 || cols == 0) return;
            uint16_t row = (uint16_t)(rows - 1);

            char hdr[40];
            unsigned active = _alerts ? _alerts->active_count() : 0;
            snprintf(hdr, sizeof(hdr), " /!\\ ALERTS (%u) : ", active);
            uint16_t used = (uint16_t)strlen(hdr);

            _terminal.move_to({row, 0});
            _terminal.set_inverted(true);
            _terminal.write_chars(hdr, used);

            if (_alerts) {
                for (uint8_t i = 0; i < _alerts->count(); i++) {
                    const Alerts::AlertCondition& c = _alerts->condition(i);
                    if (!c.unmasked || c.code == nullptr) continue;
                    uint16_t clen = (uint16_t)strlen(c.code);
                    if ((uint16_t)(used + clen + 1) > cols) break;   // out of room on the row
                    _terminal.set_inverted(false);
                    _terminal.write_chars(c.code, clen);
                    _terminal.set_inverted(true);
                    _terminal.write_chars(" ", 1);                   // inverted separator
                    used = (uint16_t)(used + clen + 1);
                }
            }

            // Reverse-video fill to the end of the row, chunked.
            char sp[32];
            memset(sp, ' ', sizeof(sp));
            while (used < cols) {
                uint16_t n = (uint16_t)((cols - used) > (uint16_t)sizeof(sp)
                                            ? sizeof(sp) : (cols - used));
                _terminal.write_chars(sp, n);
                used = (uint16_t)(used + n);
            }
            _terminal.set_inverted(false);
        }

        /** @brief Cooperative ribbon pass: repaint when the unmasked set or active
         *  count changed, but only while the TX ring is shallow; otherwise stay dirty
         *  and retry next tick. */
        void _run_ribbon() {
            if (_alerts == nullptr) return;
            uint64_t sig = _unmasked_set();
            uint8_t  cnt = _alerts->active_count();
            if (sig != _ribbon_sig || cnt != _ribbon_count) _ribbon_dirty = true;
            if (!_ribbon_dirty) return;
            if (!_terminal.tx_below_cold()) return;   // wait for a shallow ring
            _draw_ribbon();
            _ribbon_sig   = sig;
            _ribbon_count = cnt;
            _ribbon_dirty = false;
        }

        /** @} */

    public:
        /** @brief Construct a StatusPage bound to a terminal, PLC payload, and the
         *  alert model that feeds the bottom ribbon (may be null). */
        StatusPage(AnsiTerminal& terminal, const OutputData* data,
                   const Alerts::AlertModel* alerts = nullptr)
            : MetricPage(terminal, 16), _data(data), _alerts(alerts) {
            _hot_ms = Config::PAGE_HOT_MS;   // wall clock + uptime tick every second
        }

        const char* name() const override { return "STATUS"; }

        void focus() override {
            MetricPage::focus();
            _ribbon_dirty = true;   // screen was cleared: repaint the ribbon
        }

        void tick(bool active) override {
            MetricPage::tick(active);
            if (active) _run_ribbon();
        }
    };

}}

#endif // PAGES_STATUS_H
