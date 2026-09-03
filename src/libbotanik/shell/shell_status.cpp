/**
 * @file shell_status.cpp
 * @brief What the unit is REPORTING: the beat's metrics and per-task costs, the
 *        build identity, and the network / field-bus state.
 */
#include "libbotanik/shell/shell.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_SHELL_PAGE
#include "libbotanik/vars/bindings.h"
#include "libbotanik/shell/args.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "libbotanik/config/identity.h"
#include "libbotanik/config/modbus.h"
#include "libbotanik/config/net.h"
#include "libbotanik/config/scheduler.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/modbus/rtu/master.h"
#include "libbotanik/modbus/tcp/slave.h"
#include "libbotanik/network/mqtt.h"
#include "libbotanik/network/network.h"
#include "libbotanik/network/ntp.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/utils/text.h"

// Build-time git short hash: a generated header on the CMake builds, "unknown"
// on the Arduino .ino deploy (no CMake, so the header isn't generated).
#if defined(__has_include) && __has_include("botanik_gitversion.h")
#  include "botanik_gitversion.h"           // BOTANIK_GIT_HASH
#endif
#ifndef BOTANIK_GIT_HASH
#  define BOTANIK_GIT_HASH "unknown"
#endif

namespace Botanik { namespace Shell {

    void CommandShell::_cmd_stats(const char*, uint16_t) {
        // Emits several lines; scrollback drains them at LOG_ROWS_PER_TICK.
        Scheduler::SchedulerContext* ctx = _sys != nullptr ? _sys->scheduler : nullptr;
        if (ctx == nullptr) { _println("stats: no scheduler context"); return; }

        char val[24], detail[64];
        char fa[12], fb[12];

        snprintf(val,    sizeof(val),    "%lus",
            (unsigned long)(HAL::Clock::uptime_ms() / 1000));
        snprintf(detail, sizeof(detail), "%lu ticks, %lu overruns",
            (unsigned long)ctx->tick_count, (unsigned long)ctx->overruns);
        _field("Uptime", val, detail);

        snprintf(val,    sizeof(val),    "%s%%",
            Utils::format_float(fa, sizeof(fa), ctx->cpu_load_ema * 100.0f, 1));
        snprintf(detail, sizeof(detail), "recent %sms, peak %sms",
            Utils::format_float(fa, sizeof(fa), ctx->cpu_loop_us_recent / 1000.0, 2),
            Utils::format_float(fb, sizeof(fb), ctx->cpu_loop_us_max / 1000.0, 2));
        _field("CPU load", val, detail);

        snprintf(val,    sizeof(val),    "%s%%",
            Utils::format_float(fa, sizeof(fa), ctx->cpu_jitter_ema * 100.0f, 1));
        snprintf(detail, sizeof(detail), "recent %sms, peak %sms",
            Utils::format_float(fa, sizeof(fa), ctx->cpu_jitter_us_recent / 1000.0, 2),
            Utils::format_float(fb, sizeof(fb), ctx->cpu_jitter_us_max / 1000.0, 2));
        _field("CPU jitter", val, detail);

        snprintf(val,    sizeof(val),    "%s%%",
            Utils::format_float(fa, sizeof(fa), ctx->stack_usage * 100.0f, 1));
        snprintf(detail, sizeof(detail), "%s free, %s total",
            Utils::format_size(fa, sizeof(fa), ctx->stack_bytes_free),
            Utils::format_size(fb, sizeof(fb), ctx->stack_depth));
        _field("Stack usage", val, detail);

        // The reset the loop can choose to take. A withheld kick is otherwise
        // invisible until the unit reboots and cannot say why.
        if (ctx->watchdog_ms == 0) {
            _field("Watchdog", "not armed", nullptr);
        } else {
            snprintf(val, sizeof(val), "%lums", (unsigned long)ctx->watchdog_ms);
            // How long since the kick that went out: the distance to the reset
            // the loop would otherwise take, which is the number to look at
            // when the beat is late.
            const uint64_t now = HAL::Clock::uptime_ms();
            const unsigned long since =
                (ctx->watchdog_kick_ms == 0 || now < ctx->watchdog_kick_ms)
                    ? 0ul : (unsigned long)(now - ctx->watchdog_kick_ms);
            if (ctx->watchdog_kick_ms == 0)
                snprintf(detail, sizeof(detail), "never kicked");
            else if (!ctx->watchdog_kicking)
                snprintf(detail, sizeof(detail),
                         "WITHHELD, last kick %lums ago", since);
            else if (ctx->watchdog_withheld == 0)
                snprintf(detail, sizeof(detail), "last kick %lums ago", since);
            else
                snprintf(detail, sizeof(detail), "last kick %lums ago, %lu withheld",
                         since, (unsigned long)ctx->watchdog_withheld);
            _field("Watchdog", val, detail);
        }

        snprintf(detail, sizeof(detail), "%lu TX dropped, %lu RX overruns",
            (unsigned long)_terminal.serial().tx_dropped(),
            (unsigned long)_terminal.serial().rx_overrun());
        _field("Console serial", detail, nullptr);
    }

    void CommandShell::_cmd_tasks(const char* args, uint16_t alen) {
        const Tasks::Registry* reg = (_sys != nullptr) ? _sys->tasks : nullptr;
        if (reg == nullptr) { _println("tasks: no task registry"); return; }

        uint16_t wlen; const char* verb = arg_next(args, alen, wlen);
        uint16_t wa;   const char* a    = arg_next(args, alen, wa);

        if (arg_eq(verb, wlen, "stats")) { _tasks_stats(*reg); return; }
        if (arg_eq(verb, wlen, "list"))  { _tasks_list(*reg, a, wa); return; }
        if (arg_eq(verb, wlen, "get")) {
            if (a == nullptr) { _verb_usage("tasks", "get"); return; }
            _task_detail(*reg, a, wa);
            return;
        }
        _verb_usage("tasks");
    }

    // The beat as a whole, in `stats`' shape - NOT a listing: 22 rows over a
    // 4800-baud console spend the byte budget on an index of the TASKS page.
    // Only what no per-task row can say: the totals, and which task to look at.
    void CommandShell::_tasks_stats(const Tasks::Registry& reg) {
        uint64_t runs = 0, peaks_us = 0, missed = 0;
        uint8_t  missed_tasks = 0;
        const Tasks::Task* worst   = nullptr;   // most missed activations
        const Tasks::Task* busiest = nullptr;   // dearest mean run
        for (uint8_t i = 0; i < reg.count(); i++) {
            const Tasks::Task& t = reg.task(i);
            runs     += t.cost.runs;
            peaks_us += t.cost.us_max;
            missed   += t.cost.missed;
            if (t.cost.missed != 0) {
                missed_tasks++;
                if (worst == nullptr || t.cost.missed > worst->cost.missed) worst = &t;
            }
            if (busiest == nullptr || t.cost.us_mean() > busiest->cost.us_mean())
                busiest = &t;
        }

        char val[24], detail[80], n1[24], n2[24];

        snprintf(val, sizeof(val), "%u", (unsigned)reg.count());
        snprintf(detail, sizeof(detail), "%u refused, %u slots",
                 (unsigned)reg.overflow(), (unsigned)Config::MAX_TASKS);
        _field("Registered", val, detail);

        // The rate classes, since a task's period is its group's. Names then
        // periods, both slash-joined: the pairwise form ("fast 10, main 50") is
        // wider still, and _field() cuts whatever does not fit.
        snprintf(val, sizeof(val), "%lums", (unsigned long)Config::TASK_PERIOD_MS);
        // The DISTINCT periods in the table, smallest first. There is no fixed
        // set of rate classes to name any more: a task states its own, so what
        // an operator wants is which ones are actually in use.
        uint32_t seen[8];
        uint8_t  n = 0;
        for (uint8_t i = 0; i < reg.count() && n < 8; i++) {
            const uint32_t p = reg.task(i).period_ms;
            bool have = false;
            for (uint8_t j = 0; j < n; j++) if (seen[j] == p) have = true;
            if (have) continue;
            uint8_t at = n++;
            while (at > 0 && seen[at - 1] > p) { seen[at] = seen[at - 1]; at--; }
            seen[at] = p;
        }
        int w = 0;
        for (uint8_t i = 0; i < n && w >= 0 && w < (int)sizeof(detail); i++)
            w += snprintf(detail + w, sizeof(detail) - (size_t)w, "%s%lu",
                          (i == 0) ? "" : "/", (unsigned long)seen[i]);
        _field("Beat", val, detail);

        // Missed is the headline: a dropped activation is never caught up
        // (Tasks::TaskTimer), so this is the only record of a rate not kept.
        snprintf(detail, sizeof(detail), "across %u task(s)", (unsigned)reg.count());
        _field("Activations",
               Utils::format_int(n1, sizeof(n1), (long long)runs), detail);

        if (missed == 0)
            snprintf(detail, sizeof(detail), "every task kept its rate");
        else
            snprintf(detail, sizeof(detail), "%u task(s), worst %s with %s",
                     (unsigned)missed_tasks,
                     (worst != nullptr && worst->name != nullptr) ? worst->name : "?",
                     Utils::format_int(n2, sizeof(n2),
                         (long long)(worst != nullptr ? worst->cost.missed : 0)));
        _field("Missed",
               Utils::format_int(n1, sizeof(n1), (long long)missed), detail);

        // Every task at its own worst, all due together: the beat that never
        // happened. Exceeding the beat period is normal, not an overrun.
        snprintf(val, sizeof(val), "%sus",
                 Utils::format_int(n1, sizeof(n1), (long long)peaks_us));
        _field("Peaks add to", val, "every task at its worst, one beat");

        // One name to go and look at, since the table is the TASKS page's.
        if (busiest != nullptr && busiest->cost.runs != 0) {
            const uint32_t period = busiest->period_ms;
            char fa[12];
            snprintf(val, sizeof(val), "%luus",
                     (unsigned long)busiest->cost.us_mean());
            if (period != 0)
                snprintf(detail, sizeof(detail), "%s mean, %s %% of period",
                         (busiest->name != nullptr) ? busiest->name : "?",
                         Utils::format_float(fa, sizeof(fa),
                             (double)busiest->cost.us_mean() / (double)(period * 10u), 1));
            else
                snprintf(detail, sizeof(detail), "%s mean, every beat",
                         (busiest->name != nullptr) ? busiest->name : "?");
            _field("Costliest task", val, detail);
        }

        _println("per-task figures: the TASKS page, or `tasks get <name>`");
    }

    void CommandShell::_tasks_list(const Tasks::Registry& reg, const char* prefix,
                                   uint16_t plen) {
        char out[Config::LOG_FORMATTED_LEN];
        uint16_t n = 0;
        for (uint8_t i = 0; i < reg.count(); i++) {
            const Tasks::Task& t = reg.task(i);
            if (t.name == nullptr) continue;
            if (!Utils::glob_bounded(t.name, prefix, plen)) continue;
            _emitf(out, sizeof(out), "  %-22s %-8s %5lums %6luus %5lu",
                   t.name, Tasks::phase_name(t.phase),
                   (unsigned long)t.period_ms,
                   (unsigned long)t.cost.us_mean(),
                   (unsigned long)t.cost.missed);
            n++;
        }
        if (n == 0) { _println("tasks: nothing matches"); return; }
        _emitf(out, sizeof(out), "  %u task(s)  (name group phase every mean missed)", n);
    }

    void CommandShell::_task_detail(const Tasks::Registry& reg,
                                    const char* name, uint16_t nlen) {
        for (uint8_t i = 0; i < reg.count(); i++) {
            const Tasks::Task& t = reg.task(i);
            if (t.name == nullptr || !arg_eq(name, nlen, t.name)) continue;

            char val[64], fa[12];
            const uint32_t period = t.period_ms;

            snprintf(val, sizeof(val), "%s, every %lu ms",
                     Tasks::phase_name(t.phase), (unsigned long)period);
            _line("Runs in", val);

            // last_run_ms() is meaningless until the first activation, so a task
            // that has never run says so rather than reporting the uptime.
            // Missed rides here because it only means anything against the run
            // count.
            if (t.timer.runs() == 0) snprintf(val, sizeof(val), "none yet");
            else snprintf(val, sizeof(val), "%lu run, %lu missed, last %lu ms ago",
                          (unsigned long)t.timer.runs(),
                          (unsigned long)t.cost.missed,
                          (unsigned long)(HAL::Clock::uptime_ms() - t.timer.last_run_ms()));
            _line("Activations", val);

            snprintf(val, sizeof(val), "last %lu us, mean %lu us, peak %lu us",
                     (unsigned long)t.cost.us_last,
                     (unsigned long)t.cost.us_mean(),
                     (unsigned long)t.cost.us_max);
            _line("Cost", val);

            // Against its OWN period, not the beat: a 500 ms task costing 200 us
            // is idle, and measuring it against 10 ms would say otherwise.
            if (t.cost.runs != 0 && period != 0) {
                // us_mean/period_ms is already per-mille - the us/ms ratio does
                // the scaling - so this stays a 32-bit divide.
                snprintf(val, sizeof(val), "%s %% of its own period",
                         Utils::format_float(fa, sizeof(fa),
                             (double)t.cost.us_mean() / (double)(period * 10u), 2));
                _line("Duty", val);
            }
            return;
        }
        char out[Config::LOG_FORMATTED_LEN];
        _emitf(out, sizeof(out), "tasks: no task named %.*s", (int)nlen, name);
    }

    void CommandShell::_cmd_version(const char*, uint16_t) {
        char out[Config::LOG_FORMATTED_LEN];
        _emitf(out, sizeof(out), "Botanik %s (%s) site %s on %s",
               Config::FIRMWARE_VERSION, BOTANIK_GIT_HASH,
               Config::SITE_NAME, HAL::hardware_description());
    }
    void CommandShell::_cmd_features(const char*, uint16_t) {
        // Compile-time flags, so there is no argument form and nothing to set.
        // An absent subsystem is otherwise indistinguishable from a broken one.
        // Expanded from BOTANIK_FEATURE_LIST rather than a second roster, so a
        // flag added to the header is reported here. Names carry the macro
        // spelling and are lower-cased for display below.
#define BOTANIK_FEAT_ROW(f) { #f, BOTANIK_FEATURE_##f != 0 },
        static const struct { const char* name; bool on; } FEATURES[] = {
            BOTANIK_FEATURE_LIST(BOTANIK_FEAT_ROW)
        };
#undef BOTANIK_FEAT_ROW

        // Partitioned rather than one line per flag: every line costs the same
        // 4800-baud budget. Wrapped rather than truncated, since two dozen
        // names do not fit 80 columns and a clipped flag reads as absent.
        // 72 = the conventional 80 columns minus the label; the shell has no
        // terminal handle to measure. Wrapping short is harmless, long clips.
        static const size_t WRAP_W = 72;
        char out[Config::LOG_FORMATTED_LEN];
        for (uint8_t pass = 0; pass < 2; pass++) {
            const bool want = (pass == 0);
            char list[96];
            size_t n = 0;
            bool first_line = true, any = false;
            list[0] = '\0';

            for (const auto& f : FEATURES) {
                if (f.on != want) continue;
                any = true;
                const size_t len = strlen(f.name);
                // Flush before overflowing, so a name is never split.
                if (n != 0 && n + 1 + len > WRAP_W) {
                    _emitf(out, sizeof(out), "%-3s %s %s",
                           first_line ? (want ? "on" : "off") : "",
                           first_line ? ":" : " ", list);
                    first_line = false;
                    n = 0; list[0] = '\0';
                }
                if (n != 0) list[n++] = ' ';
                for (size_t i = 0; i < len && n < sizeof(list) - 1; i++) {
                    const char c = f.name[i];
                    list[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
                }
                list[n] = '\0';
            }
            if (n != 0 || !any)
                _emitf(out, sizeof(out), "%-3s %s %s",
                       first_line ? (want ? "on" : "off") : "",
                       first_line ? ":" : " ", any ? list : "(none)");
        }
    }

#if !BOTANIK_FEATURE_NETWORK

    void CommandShell::_cmd_net(const char*, uint16_t) {
        _println("net: networking disabled");
    }
#else
    void CommandShell::_cmd_net(const char*, uint16_t) {
        if (_sys == nullptr) { _println("net: networking disabled"); return; }
        const SystemContext&           ctx = *_sys;
        const Network::NetworkHandler* net = ctx.network;
        // Link never came up (config absent / offline).
        if (net == nullptr) { _println("net: networking disabled"); return; }
#if BOTANIK_FEATURE_NTP
        const Network::NtpClient*       ntp = ctx.ntp;
#else
        const Network::NtpClient*       ntp = nullptr;    // _NTP off: not built
#endif
#if BOTANIK_FEATURE_MQTT
        const Network::MqttClient*      mqt = ctx.mqtt;
#else
        const Network::MqttClient*      mqt = nullptr;    // _MQTT off: not built
#endif
        // Dotted-leader "label .... : value" lines. Sized for a pair of host
        // strings (DNS / NTP servers); the scrollback soft-wraps if a long pair
        // overruns 80 cols.
        char val[2 * Config::HOST_STR_LEN + 8];

        // One address at a time into a shared scratch: sequential use only.
        char ipbuf[16];
        auto ip4 = [&](const HAL::Network::Ip4& a) -> const char* {
            return HAL::Network::ip4_to_str(a, ipbuf, sizeof(ipbuf));
        };

        // --- Link --------------------------------------------------------
        const char* mode = Vars::get_int(_bank(), "net_dhcp") ? "DHCP" : "static";
        snprintf(val, sizeof(val), "%s  (%s)", net->status_str(), mode);
        _line("Network", val);

        // The active link parameters (DHCP lease or static config, whichever is
        // live), read back from the HAL so a DHCP-assigned value shows too.
        HAL::Network::Ip4 a4;
        _line("IP address", net->ip(&a4) ? ip4(a4) : "-");
        _line("Netmask",
             HAL::Network::link_netmask(&a4) ? ip4(a4) : "-");
        _line("Gateway",
             HAL::Network::link_gateway(&a4) ? ip4(a4) : "-");
        {
            HAL::Network::Ip4 d0, d1;
            bool h0 = HAL::Network::link_dns(0, &d0);
            bool h1 = HAL::Network::link_dns(1, &d1);
            if (h0 && h1) {
                char first[16];
                snprintf(val, sizeof(val), "%s  %s",
                         HAL::Network::ip4_to_str(d0, first, sizeof(first)), ip4(d1));
            } else {
                snprintf(val, sizeof(val), "%s", h0 ? ip4(d0) : "-");
            }
            _line("DNS", val);
        }

        // --- NTP ---------------------------------------------------------
        if (ntp != nullptr) {
            if (ntp->synced())
                snprintf(val, sizeof(val), "%s  (offset %+ld ms)",
                         ntp->status_str(), (long)ntp->offset_ms());
            else
                snprintf(val, sizeof(val), "%s", ntp->status_str());
            _line("NTP", val);
            const char* s1 = Vars::get_str(_bank(), "ntp_srv1");
            if (s1[0]) {
                // Configured hostname, then the resolved IP:port once known.
                Network::ResolvedAddr sa = ntp->server_addr();
                bool resolved = (sa.b[0] | sa.b[1] | sa.b[2] | sa.b[3]) != 0;
                if (resolved)
                    snprintf(val, sizeof(val), "%s  %s:%u", s1,
                             ip4(sa), (unsigned)ntp->server_port());
                else
                    snprintf(val, sizeof(val), "%s", s1);
                _line("NTP server", val);
            }
        } else {
            _line("NTP", "disabled");
        }

        // --- MQTT --------------------------------------------------------
        if (mqt != nullptr) {
            uint64_t lp = mqt->last_publish_ms();
            if (lp != 0) {
                char ageb[12];
                uint32_t age_s = (uint32_t)((HAL::Clock::uptime_ms() - lp) / 1000);
                snprintf(val, sizeof(val), "%s  (pub %s)", mqt->status_str(),
                         Utils::format_age(ageb, sizeof(ageb), age_s, true));
            } else {
                snprintf(val, sizeof(val), "%s  (no pub yet)", mqt->status_str());
            }
            _line("MQTT", val);
            // The EFFECTIVE broker, which is what the client is actually
            // given: an empty row falls back to the gateway, and a report
            // showing nothing while the client talks to the router would be
            // the report lying. Marked, so an operator can tell which it is.
            const char* set = Vars::get_str(_bank(), "mqtt_host");
            const char* brk = (_bank() != nullptr)
                                  ? Vars::host_or_gateway(*_bank(), "mqtt_host")
                                  : set;
            if (brk != nullptr && brk[0]) {
                const char* via = set[0] ? "" : " (net_gw)";
                // Configured hostname, then the resolved IP:port once connected.
                if (mqt->connected()) {
                    Network::ResolvedAddr pa = mqt->peer_addr();
                    snprintf(val, sizeof(val), "%s%s  %s:%u", brk, via,
                             ip4(pa), (unsigned)mqt->peer_port());
                } else {
                    snprintf(val, sizeof(val), "%s%s", brk, via);
                }
                _line("MQTT broker", val);
            }
        } else {
            _line("MQTT", "disabled");
        }
    }
#endif  // BOTANIK_FEATURE_NETWORK

// modbus - the field bus and the SCADA-facing server

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    /** @brief The roll-call: one line per unit, the plant's tag first (an
     *  operator knows the box by its designation, not its unit id), then the
     *  address, the model, and whether it is answering. No counters. */
    void CommandShell::_modbus_devices(const Modbus::RtuMaster& rtu) {
        char val[96];
        _println("RTU devices (the field bus)");
        if (rtu.peer_count() == 0) { _println("  none planned"); return; }

        for (uint8_t i = 0; i < rtu.peer_count(); ++i) {
            const uint8_t unit = rtu.peer_unit(i);
            const char* tag = Modbus::device_tag(rtu.devices(), unit);
            const char* mdl = Modbus::model_of(rtu.devices(), unit).name;

            // The miss count is how far into the backoff this unit is, not
            // traffic: it says how dead "dead" is, so it stays out of stats.
            char state[16];
            if (!rtu.peer_dead(i)) snprintf(state, sizeof(state), "%s", "ok");
            else snprintf(state, sizeof(state), "DEAD x%u", (unsigned)rtu.peer_misses(i));

            snprintf(val, sizeof(val), "  %-18s dev %-3u %-19s %s",
                     (tag != nullptr && tag[0] != '\0') ? tag : "-",
                     (unsigned)unit, (mdl != nullptr) ? mdl : "-", state);
            _println(val);
        }
    }
#endif

#if !BOTANIK_FEATURE_MODBUS_RTU_CLIENT && !BOTANIK_FEATURE_MODBUS_TCP_SERVER
    void CommandShell::_cmd_modbus(const char*, uint16_t) {
        _println("modbus: not built in");
    }
#else
    void CommandShell::_cmd_modbus(const char* args, uint16_t alen) {
        if (_sys == nullptr) { _println("modbus: no context"); return; }
        char val[96];

        // `modbus devices` is the roll-call and nothing else, `modbus stats`
        // the plan, the line and the counters. A bare `modbus` is answered
        // upstream, in _dispatch().
        uint16_t wn = 0;
        const char* sub = arg_next(args, alen, wn);
        const bool want_devices = arg_eq(sub, wn, "devices");
        if (!want_devices && !arg_eq(sub, wn, "stats")) { _verb_usage("modbus"); return; }

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        const Modbus::RtuMaster* rtu = _sys->modbus_rtu;
        if (want_devices) {
            if (rtu == nullptr) _println("RTU master (the field bus) .. not wired");
            else                _modbus_devices(*rtu);
            return;
        }

        if (rtu == nullptr) {
            _println("RTU master (the field bus) .. not wired");
        } else {
            const Modbus::Planner& plan = rtu->plan();

            // Duty is the scheduled figure, not the wire one: it answers "is
            // there room for another sensor".
            _println("RTU master (the field bus)");

            snprintf(val, sizeof(val), "%u rows -> %u blocks on %u device(s)",
                     (unsigned)plan.row_count(), (unsigned)plan.block_count(),
                     (unsigned)plan.device_count());
            _line("  Plan", val);

            snprintf(val, sizeof(val), "%s, %u baud, %u.%u%% of the bus",
                     rtu->status_str(), (unsigned)Config::MODBUS_BAUD,
                     (unsigned)(plan.duty_ppm() / 10000u),
                     (unsigned)((plan.duty_ppm() / 1000u) % 10u));
            _line("  State", val);

            snprintf(val, sizeof(val), "%lu sent, %lu faults, %lu refused, %lu short tx, %lu late B",
                     (unsigned long)rtu->txns(), (unsigned long)rtu->faults(),
                     (unsigned long)rtu->refusals(), (unsigned long)rtu->tx_short(),
                     (unsigned long)rtu->stale_rx());
            _line("  Transactions", val);

            // The framer has always kept these apart because they point at
            // different physical faults, and until now nothing read them: a bus
            // failing on CRC is a cable, a terminator or the wrong baud; one
            // failing on clean timeouts is power or addressing; and bytes out of
            // turn are a second master or a duplicate address.
            const Modbus::RtuFramer& fr = rtu->framer();
            snprintf(val, sizeof(val), "%lu crc, %lu bad frame, %lu timeout, %lu stray B",
                     (unsigned long)fr.crc_errors(), (unsigned long)fr.bad_frames(),
                     (unsigned long)fr.timeouts(), (unsigned long)fr.stray_bytes());
            _line("  Line", val);

            // Unplanned rows are signals that will never be scanned: a critical
            // at bring-up, repeated here rather than only in a log.
            if (plan.rows_dropped() != 0 || plan.rows_unscanned() != 0) {
                snprintf(val, sizeof(val), "%u unplanned, %u never scanned",
                         (unsigned)plan.rows_dropped(),
                         (unsigned)plan.rows_unscanned());
                _line("  Rows dropped", val);
            }

            // Twenty devices do not fail at once. Said here as well as in the
            // alarm, because this is the screen an engineer is already looking
            // at when they ask which cabinet to walk to.
            if (rtu->peer_count() != 0 && rtu->dead_count() == rtu->peer_count())
                _println("  (the whole segment is quiet: trunk, termination, "
                         "this unit's transceiver, or a second master)");

            // Counts only: twenty units is a screenful that would push the plan
            // and the state off the top. WHICH ones is `modbus devices`.
            snprintf(val, sizeof(val), "  %u device(s), %u not answering%s",
                     (unsigned)rtu->peer_count(), (unsigned)rtu->dead_count(),
                     rtu->dead_count() != 0 ? "  (modbus devices)" : "");
            _println(val);
        }
#else
        // The roll-call is the RS-485 segment's; the TCP side has no units.
        if (want_devices) { _println("modbus devices: no field bus in this build"); return; }
#endif

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        const Modbus::TcpSlave* tcp = _sys->modbus_tcp;
        if (tcp == nullptr) {
            _println("TCP slave (the SCADA side) . not wired");
        } else {
            const Modbus::SlaveContext c = tcp->context();
            _println("TCP slave (the SCADA side)");

            if (c.listening)
                snprintf(val, sizeof(val), "listening on %u, %u row(s), %u client(s)",
                         (unsigned)c.port, (unsigned)c.rows, (unsigned)c.clients);
            else
                snprintf(val, sizeof(val), "NOT LISTENING (port %u)", (unsigned)c.port);
            _line("  State", val);

            snprintf(val, sizeof(val), "%lu served, %lu refused, %lu writes, %lu rejects",
                     (unsigned long)c.requests, (unsigned long)c.exceptions,
                     (unsigned long)c.writes, (unsigned long)c.rejects);
            _line("  Requests", val);
        }
#endif
    }
#endif

}}  // namespace Botanik::Shell

#endif // BOTANIK_FEATURE_SHELL_PAGE
