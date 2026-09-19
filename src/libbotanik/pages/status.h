#ifndef PAGES_STATUS_H
#define PAGES_STATUS_H

/**
 * @file status.h
 * @brief StatusPage - the SYSTEM page, and the base a site builds its own on.
 *
 * Answers the questions no other page does and the status bar cannot fit: is
 * the unit in service, is the watchdog being kicked, what is the task table
 * costing, and are the two field buses talking. Everything the row-0 bar
 * already carries - wall clock, CPU load, stack - is deliberately NOT repeated
 * here; a status page that restates the chrome wastes the one screen an
 * operator opens when something is wrong.
 *
 * A site subclasses it and overrides the three payload hooks; there is no type
 * parameter and no data(), a derived page holds whatever it needs itself.
 * Derived content starts at PAYLOAD_ROW, which is DERIVED from what is compiled
 * in, so a build without the network does not leave its rows blank.
 *
 * Drawing is diffed by MetricPage. The alarm set is on the terminal-owned
 * bottom bar, not here.
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
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/config/vars.h"
#include "libbotanik/vars/access.h"
#include "libbotanik/utils/text.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#include "libbotanik/modbus/rtu/master.h"
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
#include "libbotanik/modbus/tcp/slave.h"
#endif

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

    /** @brief Full-screen status display base (see file header). */
    class StatusPage : public MetricPage {
    protected:
        /** @name State
         *  @{ */

        /** @brief The subsystem handles the system band renders from. Injected,
         *  not read back off the console. */
        const SystemContext* _sys;

        /** @name The site's own half of the band
         *  Borrowed and flash-resident, like the home table: a label and the
         *  row it shows, both generated. @{ */
        const Vars::HomeVar* _site_fields = nullptr;
        const Vars::Bank*    _io          = nullptr;
        uint8_t              _site_count  = 0;
        /** @} */

        /** @} */

        /** @name Band rows
         *  DERIVED, not declared: a build without the network must not leave
         *  its two rows blank and start the payload below them anyway.
         *  @{ */
        static constexpr uint16_t ROW_HEADER = 2;
        static constexpr uint16_t ROW_RUN    = 3;   ///< run state | watchdog
        static constexpr uint16_t ROW_BEAT   = 4;   ///< jitter    | task period
        static constexpr uint16_t ROW_TASKS  = 5;   ///< task table cost
#if BOTANIK_FEATURE_NETWORK
        static constexpr uint16_t ROW_NET    = 6;   ///< link      | NTP
        static constexpr uint16_t ROW_NETIP  = 7;   ///< address   | MQTT
        static constexpr uint16_t _ROW_AFTER_NET = ROW_NETIP;
#else
        static constexpr uint16_t _ROW_AFTER_NET = ROW_TASKS;
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT || BOTANIK_FEATURE_MODBUS_TCP_SERVER
        static constexpr uint16_t ROW_BUS  = _ROW_AFTER_NET + 1;   ///< RTU | TCP
        static constexpr uint16_t _ROW_END = ROW_BUS;
#else
        static constexpr uint16_t _ROW_END = _ROW_AFTER_NET;
#endif
        /** @brief First row a derived page owns; one blank row separates it
         *  from the band above. */
        static constexpr uint16_t PAYLOAD_ROW = _ROW_END + 2;
        /** @} */

        /** @brief The two-column layout at the CURRENT width.
         *
         *  Computed rather than fixed at 80: the console runs 80..132, and a
         *  grid pinned to 80 sits in the left 60% of a wide terminal. Widths
         *  are clamped to what a value cell can hold. */
        struct Band { uint16_t l_lbl, l_val, l_w, r_lbl, r_val, r_w; };
        Band band() const {
            const uint16_t cols = _terminal.cols();
            const uint16_t half = (uint16_t)(cols / 2);
            Band b;
            b.l_lbl = 2;
            b.l_val = 16;
            b.l_w   = (half > b.l_val + 2u) ? (uint16_t)(half - b.l_val - 2u) : 1u;
            b.r_lbl = (uint16_t)(half + 2u);
            b.r_val = (uint16_t)(half + 14u);
            b.r_w   = (cols > b.r_val + 3u) ? (uint16_t)(cols - b.r_val - 3u) : 1u;
            if (b.l_w > FIELD_MAX) b.l_w = FIELD_MAX;
            if (b.r_w > FIELD_MAX) b.r_w = FIELD_MAX;
            return b;
        }

        /** @brief Right-justify @p s into a @p width-wide field. Returns
         *  @p buf.
         *  @note Minimum width, not maximum: an over-long @p s passes through
         *  and _cell() then truncates its TAIL, which for right-justified text
         *  drops the digits that matter. Keep the formats inside the width. */
        static const char* rjust(char* buf, size_t cap, uint16_t width, const char* s) {
            if (width >= cap) width = (uint16_t)(cap - 1);
            snprintf(buf, cap, "%*s", (int)width, s);
            return buf;
        }

        /** @name Draw hooks - override to customize the page.
         *  @{ */

        void draw_static() override {
            const Band b = band();
            _header(ROW_HEADER, 2, "SYSTEM STATUS",
                    (uint16_t)(_terminal.cols() - 4));
            // _label_at, never _label: it takes an explicit column and does not
            // clear to EOL, so the left label cannot wipe the right column.
            _label_at(ROW_RUN,   b.l_lbl, "Run state");
            _label_at(ROW_BEAT,  b.l_lbl, "CPU jitter");
            _label_at(ROW_TASKS, b.l_lbl, "Tasks");
            _label_at(ROW_RUN,   b.r_lbl, "Watchdog");
            _label_at(ROW_BEAT,  b.r_lbl, "Task period");
            _label_at(ROW_TASKS, b.r_lbl, "Task load");
#if BOTANIK_FEATURE_NETWORK
            _label_at(ROW_NET,   b.l_lbl, "Network");
            _label_at(ROW_NETIP, b.l_lbl, "Network IP");
            _label_at(ROW_NET,   b.r_lbl, "NTP sync");
            _label_at(ROW_NETIP, b.r_lbl, "MQTT");
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT || BOTANIK_FEATURE_MODBUS_TCP_SERVER
            _label_at(ROW_BUS,   b.l_lbl, "Field bus");
            _label_at(ROW_BUS,   b.r_lbl, "Modbus TCP");
#endif
            draw_payload_static();
        }

        void draw_dynamic() override {
            if (_sys == nullptr) { draw_payload_dynamic(); return; }
            const SystemContext& ctx = *_sys;
            const Band b = band();
            char val[64], fld[64];

            if (ctx.scheduler) {
                // Nominal loop period and the rate actually achieved.
                snprintf(val, sizeof(val), "%lu ms (%lu TPS)",
                         (unsigned long)Config::TASK_PERIOD_MS,
                         (unsigned long)ctx.scheduler->ticks_per_second);
                _cell(ROW_BEAT, b.r_val, rjust(fld, sizeof(fld), b.r_w, val), b.r_w);
            }

            _draw_tasks(b);

#if BOTANIK_FEATURE_NETWORK
            _draw_network(b);
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT || BOTANIK_FEATURE_MODBUS_TCP_SERVER
            _draw_buses(b);
#endif
            draw_payload_dynamic();
        }

        /** @brief What the task table costs, two ways: the SUM of the per-task
         *  means is an upper bound on a beat (every task due at once), the DUTY
         *  is that spread over each task's own rate, which predicts headroom.
         *  @note Both need TaskCost timed off the high-resolution counter: on
         *  the low-power clock most tasks read 0. @see HAL::Clock::hrt_ticks */
        void _draw_tasks(const Band& b) {
            if (_sys == nullptr || _sys->tasks == nullptr) return;
            const Tasks::Registry& reg = *_sys->tasks;

            uint32_t sum_us = 0, duty_ppm = 0, missed = 0;
            const uint8_t n = reg.count();
            for (uint8_t i = 0; i < n; i++) {
                const Tasks::Task& t = reg.task(i);
                const uint32_t mean = t.cost.us_mean();
                sum_us += mean;
                missed += t.cost.missed;
                // us / ms is already per-mille; x1000 makes it ppm without
                // losing a cheap task to integer division.
                if (t.period_ms != 0)
                    duty_ppm += (uint32_t)((uint64_t)mean * 1000ull / t.period_ms);
            }

            char val[64], fld[64];
            // us, the unit the TASKS page's own MEAN column uses: at ms the
            // whole table rounds to 0.00 and the cell says nothing.
            snprintf(val, sizeof(val), "%u, %lu us/beat", (unsigned)n,
                     (unsigned long)sum_us);
            _cell(ROW_TASKS, b.l_val, rjust(fld, sizeof(fld), b.l_w, val), b.l_w);

            // A refused registration is work that never runs at all, which no
            // other page reports.
            char duty[16];
            if (reg.overflow() != 0)
                snprintf(val, sizeof(val), "%s%%, %u REFUSED",
                         Utils::format_float(duty, sizeof(duty), duty_ppm / 10000.0, 1),
                         (unsigned)reg.overflow());
            else
                snprintf(val, sizeof(val), "%s%%, %lu missed",
                         Utils::format_float(duty, sizeof(duty), duty_ppm / 10000.0, 1),
                         (unsigned long)missed);
            _cell(ROW_TASKS, b.r_val, rjust(fld, sizeof(fld), b.r_w, val), b.r_w);
        }

#if BOTANIK_FEATURE_NETWORK
        /** @brief The network band. Each cell keeps its OWN feature gate: the
         *  pointer is declared whatever the build (@see SystemContext), but
         *  without its header the pointee is incomplete and the accessor below
         *  would not compile. The rows stay either way and read "off", so the
         *  grid does not reflow between builds. */
        void _draw_network(const Band& b) {
            const SystemContext& ctx = *_sys;
            char val[64], fld[64];

            _cell(ROW_NET, b.l_val,
                  rjust(fld, sizeof(fld), b.l_w,
                        ctx.network ? ctx.network->status_str() : "off"), b.l_w);
            HAL::Network::Ip4 ip;
            if (ctx.network && ctx.network->ip(&ip))
                HAL::Network::ip4_to_str(ip, val, sizeof(val));
            else
                snprintf(val, sizeof(val), "no link");
            _cell(ROW_NETIP, b.l_val, rjust(fld, sizeof(fld), b.l_w, val), b.l_w);

#if BOTANIK_FEATURE_NTP
            if (ctx.ntp == nullptr) {
                snprintf(val, sizeof(val), "off");
            } else if (ctx.ntp->synced()) {
                // The offset is worth showing as a DRIFT. The first sync of a
                // unit whose RTC started at zero is a whole epoch and saturates
                // int32, which says nothing about the quality of the sync.
                const int32_t off = ctx.ntp->offset_ms();
                if (off > -60000 && off < 60000)
                    snprintf(val, sizeof(val), "synced, %+ld ms", (long)off);
                else
                    snprintf(val, sizeof(val), "synced");
            } else {
                snprintf(val, sizeof(val), "%s", ctx.ntp->status_str());
            }
#else
            snprintf(val, sizeof(val), "off");
#endif
            _cell(ROW_NET, b.r_val, rjust(fld, sizeof(fld), b.r_w, val), b.r_w);

#if BOTANIK_FEATURE_MQTT
            if (ctx.mqtt) {
                const uint64_t last = ctx.mqtt->last_contact_ms();
                if (last != 0) {
                    char nf[12];
                    const uint32_t age_s =
                        (uint32_t)((HAL::Clock::uptime_ms() - last) / 1000);
                    // floor_min clamps sub-minute to "0 m", so this cold field
                    // changes at most once a minute rather than every second.
                    snprintf(val, sizeof(val), "%s (%s)", ctx.mqtt->status_str(),
                             Utils::format_age(nf, sizeof(nf), age_s,
                                               Utils::AgeSuffix::BARE,
                                               Utils::AgeFloor::MINUTES));
                } else {
                    snprintf(val, sizeof(val), "%s", ctx.mqtt->status_str());
                }
            } else {
                snprintf(val, sizeof(val), "off");
            }
#else
            snprintf(val, sizeof(val), "off");
#endif
            _cell(ROW_NETIP, b.r_val, rjust(fld, sizeof(fld), b.r_w, val), b.r_w);
        }
#endif

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT || BOTANIK_FEATURE_MODBUS_TCP_SERVER
        /** @brief The two field buses. For a greenhouse PLC this is the thing
         *  most worth a permanent readout: a dead RS-485 segment is a plant
         *  running on declared values. */
        void _draw_buses(const Band& b) {
            const SystemContext& ctx = *_sys;
            char val[64], fld[64];

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
            if (ctx.modbus_rtu)
                snprintf(val, sizeof(val), "%s, %u/%u dead",
                         ctx.modbus_rtu->status_str(),
                         (unsigned)ctx.modbus_rtu->dead_count(),
                         (unsigned)ctx.modbus_rtu->peer_count());
            else
                snprintf(val, sizeof(val), "off");
#else
            snprintf(val, sizeof(val), "off");
#endif
            _cell(ROW_BUS, b.l_val, rjust(fld, sizeof(fld), b.l_w, val), b.l_w);

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
            if (ctx.modbus_tcp) {
                const Modbus::SlaveContext sc = ctx.modbus_tcp->context();
                if (sc.listening)
                    snprintf(val, sizeof(val), ":%u, %u client%s", (unsigned)sc.port,
                             (unsigned)sc.clients, sc.clients == 1 ? "" : "s");
                else if (!sc.enabled)
                    snprintf(val, sizeof(val), "off");
                else if (sc.bind_failed)
                    snprintf(val, sizeof(val), ":%u bind failed (%d)",
                             (unsigned)sc.port, sc.bind_error);
                else
                    snprintf(val, sizeof(val), ":%u not listening", (unsigned)sc.port);
            } else {
                snprintf(val, sizeof(val), "off");
            }
#else
            snprintf(val, sizeof(val), "off");
#endif
            _cell(ROW_BUS, b.r_val, rjust(fld, sizeof(fld), b.r_w, val), b.r_w);
        }
#endif

        /** @brief Per-second fields: the two that say whether the unit is
         *  alive and being allowed to stay that way, plus the jitter EMA which
         *  moves every beat. Un-highlighted - a never-settling value would
         *  churn the reverse-video. */
        void draw_hot() override {
            if (_sys == nullptr) { draw_payload_hot(); return; }
            const SystemContext& ctx = *_sys;
            const Band b = band();
            char val[64], fld[64], fa[16], fb[16];

            if (ctx.scheduler) {
                const Scheduler::SchedulerContext& sc = *ctx.scheduler;

                _cell(ROW_RUN, b.l_val,
                      rjust(fld, sizeof(fld), b.l_w,
                            Scheduler::run_state_name(sc.run_state)), b.l_w,
                      Highlight::PLAIN);

                // A withheld kick is the unit choosing to let the hardware
                // reset it, and is otherwise invisible until it reboots.
                if (!sc.watchdog_kicking)
                    snprintf(val, sizeof(val), "%lu ms, WITHHELD %lu",
                             (unsigned long)sc.watchdog_ms,
                             (unsigned long)sc.watchdog_withheld);
                else if (sc.watchdog_ms == 0)
                    snprintf(val, sizeof(val), "not armed");
                else
                    snprintf(val, sizeof(val), "%lu ms, kicking",
                             (unsigned long)sc.watchdog_ms);
                _cell(ROW_RUN, b.r_val, rjust(fld, sizeof(fld), b.r_w, val), b.r_w,
                      Highlight::PLAIN);

                snprintf(val, sizeof(val), "%s%% (peak %sms)",
                         Utils::format_float(fa, sizeof(fa), sc.cpu_jitter_ema * 100.0f, 1),
                         Utils::format_float(fb, sizeof(fb), sc.cpu_jitter_us_max / 1000.0, 2));
                _cell(ROW_BEAT, b.l_val, rjust(fld, sizeof(fld), b.l_w, val), b.l_w,
                      Highlight::PLAIN);
            }

            draw_payload_hot();
        }

        /** @name Payload hooks - one per cadence the base has.
         *
         *  All three, so a subclass never has to override draw_hot() and
         *  remember to chain to it.
         *  @warning The hot and cold sets must be DISJOINT: a field drawn from
         *  both paints twice a cycle and flickers. @{ */

        /** @brief Site labels and headers, drawn once on focus. Base: nothing. */
        /** @name The site's half of the band
         *
         *  Two columns of six under the library's rows, laid out by the same
         *  band() the rows above use, so both halves line up at any width. A
         *  field past SYSTEM_FIELDS_MAX is dropped rather than drawn off the
         *  bottom - the static_assert in the site's generated header is what
         *  catches it before that.
         *  @{ */

        /** @brief Rows the site half occupies, given how many fields it has. */
        uint16_t _site_rows() const {
            const uint8_t n = _site_count < Config::SYSTEM_FIELDS_MAX
                            ? _site_count : Config::SYSTEM_FIELDS_MAX;
            return (uint16_t)((n + 1u) / 2u);
        }

        void _draw_site_labels() {
            if (_site_fields == nullptr || _site_count == 0) return;
            const Band b = band();
            const uint16_t rows = _site_rows();
            _header(PAYLOAD_ROW, 2, "SITE", (uint16_t)(_terminal.cols() - 4));
            for (uint8_t i = 0; i < _site_count && i < Config::SYSTEM_FIELDS_MAX; i++) {
                const uint16_t row = (uint16_t)(PAYLOAD_ROW + 1 + (i % rows));
                _label_at(row, (i < rows) ? b.l_lbl : b.r_lbl, _site_fields[i].label);
            }
        }

        void _draw_site_values() {
            if (_site_fields == nullptr || _io == nullptr) return;
            const Band b = band();
            const uint16_t rows = _site_rows();
            char vb[32], buf[48], fld[64];
            for (uint8_t i = 0; i < _site_count && i < Config::SYSTEM_FIELDS_MAX; i++) {
                const uint16_t row = (uint16_t)(PAYLOAD_ROW + 1 + (i % rows));
                const bool     left = i < rows;
                const uint16_t col  = left ? b.l_val : b.r_val;
                const uint16_t w    = left ? b.l_w   : b.r_w;
                const Vars::Var h = _io->var(_site_fields[i].row);
                // Value THEN unit, as the home screen does it: a declared row
                // carries its unit and a bare 227.0 is not a reading.
                const char* v = Vars::var_format(h, vb, sizeof(vb));
                const char* u = h.meta().unit;
                if (u != nullptr && u[0] != '\0')
                    snprintf(buf, sizeof(buf), "%s %s", v, u);
                else
                    snprintf(buf, sizeof(buf), "%s", v);
                _cell(row, col, rjust(fld, sizeof(fld), w, buf), w);
            }
        }

        /** @} */

        virtual void draw_payload_static() { _draw_site_labels(); }
        /** @brief Site values on the cold cadence. Base: the site's own table. */
        virtual void draw_payload_dynamic() { _draw_site_values(); }
        /** @brief Site values on the per-second cadence. Base: nothing. */
        virtual void draw_payload_hot() {}

        /** @} */

        /** @} */

    public:
        /** @brief Bind to @p console, reading the system band from @p sys.
         *  @p sys may be null - every cell then renders blank rather than
         *  faulting, which is what a rig that has no aggregate wants. */
        /** @brief @p fields (+ @p count) is the site's SYSTEM table and @p io
         *  the bank its rows are in. All borrowed, all optional: a site that
         *  declares none gets the library band alone. */
        StatusPage(Console& console, const SystemContext* sys,
                   const Vars::HomeVar* fields = nullptr, uint8_t count = 0,
                   const Vars::Bank* io = nullptr)
            : MetricPage(console), _sys(sys)
            , _site_fields(fields), _io(io), _site_count(count)
        {
            _hot_ms = Config::PAGE_HOT_MS;   // run state + watchdog + jitter
        }

        const char* name() const override { return "SYSTEM"; }
    };

}}

#endif // PAGES_STATUS_H
