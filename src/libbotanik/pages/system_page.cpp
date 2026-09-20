/**
 * @file system_page.cpp
 * @brief SystemPage's draw bodies.
 *
 * Out of the header so the feature guards live in a .cpp, which is what the
 * guard rules ask for, and so the network and field-bus headers the bands read
 * are not pulled into every translation unit that names the page.
 */

#include "libbotanik/pages/system_page.h"

#if BOTANIK_FEATURE_SYSTEM_PAGE

#include <cstdio>

#include "libbotanik/hal/platform.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/utils/text.h"
#include "libbotanik/vars/access.h"
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

    void SystemPage::draw_static() {
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

    void SystemPage::draw_dynamic() {
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

    void SystemPage::_draw_tasks(const Band& b) {
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

    void SystemPage::_draw_network(const Band& b) {
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

    void SystemPage::_draw_buses(const Band& b) {
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

    void SystemPage::draw_hot() {
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

    uint16_t SystemPage::_site_rows() const {
        const uint8_t n = _site_count < Config::SYSTEM_FIELDS_MAX
                        ? _site_count : Config::SYSTEM_FIELDS_MAX;
        return (uint16_t)((n + 1u) / 2u);
    }

    void SystemPage::_draw_site_labels() {
        if (_site_fields == nullptr || _site_count == 0) return;
        const Band b = band();
        const uint16_t rows = _site_rows();
        _header(PAYLOAD_ROW, 2, "SITE", (uint16_t)(_terminal.cols() - 4));
        for (uint8_t i = 0; i < _site_count && i < Config::SYSTEM_FIELDS_MAX; i++) {
            const uint16_t row = (uint16_t)(PAYLOAD_ROW + 1 + (i % rows));
            _label_at(row, (i < rows) ? b.l_lbl : b.r_lbl, _site_fields[i].label);
        }
    }

    void SystemPage::_draw_site_values() {
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

}}  // namespace Botanik::Pages

#endif // BOTANIK_FEATURE_SYSTEM_PAGE
