// REVIEW_35: TODO
/**
 * @file shell.cpp
 * @brief CommandShell implementation - command dispatch and built-in commands.
 */
#include "libbotanik/shell/shell.h"         // CommandShell

#include <algorithm>                        // std::min / std::find
#include <cstdarg>                          // va_list / va_start / va_end
#include <cstdio>                           // snprintf / vsnprintf
#include <cstring>                          // strlen / strncmp / memcpy / memcmp

#include "libbotanik/config/identity.h"   // Config::SITE_NAME / FIRMWARE_VERSION / MQTT_TOPIC_PREFIX
#include "libbotanik/config/logging.h"    // Config::LOG_*
#include "libbotanik/config/net.h"        // Config::NET_* / DNS_* / NTP_* / MQTT_*
#include "libbotanik/config/scheduler.h"  // Config::TASK_PERIOD_MS - the `tasks` command
#include "libbotanik/config/terminal.h"   // Config::SCREEN_* / EDITOR_* / SHELL_* / PAGE_*
#include "libbotanik_features.h"            // BOTANIK_FEATURE_LIST

#include "libbotanik/alarms/alarms.h"       // Alarms::AlarmController
#include "libbotanik/hal/platform.h"        // HAL::System / Debug / Stack
#include "libbotanik/hal/storage.h"         // HAL::Storage::get / set
#include "libbotanik/logs/logger.h"         // Logs::Logger::set_min_level
#include "libbotanik/network/mqtt.h"        // Network::MqttClient
#include "libbotanik/network/network.h"     // Network::NetworkHandler
#include "libbotanik/network/ntp.h"         // Network::NtpClient
#include "libbotanik/config/modbus.h"      // Config::MODBUS_* - the `modbus` command
#include "libbotanik/modbus/rtu/client.h"  // Modbus::RtuClient / Planner
#include "libbotanik/modbus/tcp/slave.h"   // Modbus::TcpSlave / SlaveContext
#include "libbotanik/scheduler/scheduler.h" // Scheduler::SchedulerContext
#include "libbotanik/tasks/tasks.h"         // Tasks::Registry - the `tasks` command
#include "libbotanik/utils/text.h"          // Utils::format_age / format_float / format_size
#include "libbotanik/vars/params.h"         // Vars::Params

// Build-time git short hash: a generated header on the CMake builds, "unknown"
// on the Arduino .ino deploy (no CMake, so the header isn't generated).
#if defined(__has_include) && __has_include("botanik_gitversion.h")
#  include "botanik_gitversion.h"           // BOTANIK_GIT_HASH
#endif
#ifndef BOTANIK_GIT_HASH
#  define BOTANIK_GIT_HASH "unknown"
#endif

namespace Botanik { namespace Shell {

    namespace {
    /** @brief Next whitespace-delimited word from [@p args, @p args+alen),
     *  leading spaces skipped. Returns its start (nullptr if none), sets
     *  @p wlen to its length, and advances @p args / @p alen past it. */
    inline const char* arg_next(const char*& args, uint16_t& alen, uint16_t& wlen) {
        while (alen > 0 && *args == ' ') { args++; alen--; }
        if (alen == 0) { wlen = 0; return nullptr; }
        const char* w = args;
        wlen = 0;
        while (alen > 0 && *args != ' ') { args++; alen--; wlen++; }
        return w;
    }

    /** @brief True if word [@p w, @p w+wlen) equals the NUL-terminated literal
     *  @p lit. */
    inline bool arg_eq(const char* w, uint16_t wlen, const char* lit) {
        for (uint16_t i = 0; i < wlen; i++)
            if (lit[i] == '\0' || w[i] != lit[i]) return false;
        return lit[wlen] == '\0';
    }
    }  // namespace


    // Lifecycle
    // ------------------------------------------------------------------------

    CommandShell::CommandShell(Terminal::AnsiTerminal& terminal,
                               Terminal::TextEditor&   editor,
                               ShellWriter&            output,
                               const SystemContext*    sys)
        : _sys(sys), _terminal(terminal), _editor(editor), _output(output)
    {}

    // Command execution
    // ------------------------------------------------------------------------

    void CommandShell::run(const char* cmd, uint16_t len) {
        if (len == 0) return;
        _history_add(cmd, len);
        _dispatch(cmd, len);
    }

    // Internals
    // ------------------------------------------------------------------------

    const CommandShell::Builtin* CommandShell::_builtins(uint8_t& count) {
        static const Builtin TABLE[] = {
            { "help",       "list commands",                  &CommandShell::_cmd_help       },
            { "clear",      "clear the screen",               &CommandShell::_cmd_clear      },
            { "logs",       "[on|off]  live log output",      &CommandShell::_cmd_logs       },
            { "stats",      "scheduler / system metrics",     &CommandShell::_cmd_stats      },
            { "tasks",      "[<name>] the beat's totals, or one task",&CommandShell::_cmd_tasks },
            { "net",        "network / NTP / MQTT status",     &CommandShell::_cmd_net        },
            { "ntpsync",    "force an NTP resync now",         &CommandShell::_cmd_ntpsync    },
            { "alarms",     "[list|ack <code|all>] alarms",   &CommandShell::_cmd_alarms     },
            { "modbus",     "field bus plan and state; `modbus devices` for the roll-call",
                                                     &CommandShell::_cmd_modbus   },
            { "list",       "[<prefix>] list variables",      &CommandShell::_cmd_list       },
            { "get",        "<name>  read a variable",        &CommandShell::_cmd_get        },
            { "params",     "[set <name> <value>|save]  edit parameters", &CommandShell::_cmd_params },
            { "overrides",  "[set <name> [off|<value>] [valid|invalid]|clear|save]",
                                                     &CommandShell::_cmd_overrides },
            { "control",    "[enable|inhibit|disable] drive the field", &CommandShell::_cmd_control },
            { "save",       "write every pending change to flash", &CommandShell::_cmd_save },
            { "version",    "firmware version",               &CommandShell::_cmd_version    },
            { "features",   "which subsystems are built in",  &CommandShell::_cmd_features   },
            { "breakpoint", "trap into an attached debugger", &CommandShell::_cmd_breakpoint },
            { "reset",      "reboot the controller",          &CommandShell::_cmd_reset      },
            { "exit",       "quit (sim) / reboot (hw)",       &CommandShell::_cmd_exit       },
        };
        count = (uint8_t)(sizeof(TABLE) / sizeof(TABLE[0]));
        return TABLE;
    }

    void CommandShell::_dispatch(const char* cmd, uint16_t len) {
        // First word = command name (leading spaces skipped); the rest = args.
        const char* args = cmd; uint16_t alen = len, wlen;
        const char* name = arg_next(args, alen, wlen);
        if (name == nullptr) return;   // blank / all-spaces line
        // trim to arg slice
        while (alen > 0 && *args == ' ') { args++; alen--; }

        // Site commands first, so the table can override a built-in; unmatched
        // names fall through.
        for (size_t i = 0; i < _command_count; i++) {
            const ShellCommand& c = _commands[i];
            if (c.name != nullptr && c.fn != nullptr &&
                arg_eq(name, wlen, c.name)) {
                c.fn(_output, args, alen, _command_ctx);
                return;
            }
        }
        uint8_t bcount; const Builtin* B = _builtins(bcount);
        for (uint8_t i = 0; i < bcount; i++)
            if (arg_eq(name, wlen, B[i].name)) {
                (this->*B[i].fn)(args, alen); return;
            }

        _unknown(cmd, len);
    }

    void CommandShell::_println(const char* msg) {
        _output.write(msg, strlen(msg));
    }

    void CommandShell::_emitf(char* buf, size_t cap, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt);
        int w = vsnprintf(buf, cap, fmt, ap);
        va_end(ap);
        if (w > 0) _output.write(buf, std::min((size_t)w, cap - 1));
    }

    void CommandShell::_line(const char* label, const char* val) {
        char out[2 * Config::HOST_STR_LEN + 32];
        _emitf(out, sizeof(out), "%-22s : %s", label, val);
    }

    void CommandShell::_cmd_help(const char*, uint16_t) {
        // Generated from the tables, so help cannot drift from the command set.
        // %-10s is a minimum width, not a cap (a long name pushes help right).
        char out[Config::LOG_FORMATTED_LEN];
        uint8_t bcount; const Builtin* B = _builtins(bcount);
        for (uint8_t i = 0; i < bcount; i++)
            _emitf(out, sizeof(out), "  %-10s %s", B[i].name, B[i].help);
        for (size_t i = 0; i < _command_count; i++) {
            const ShellCommand& c = _commands[i];
            if (c.name == nullptr) continue;
            _emitf(out, sizeof(out), "  %-10s %s", c.name, c.help ? c.help : "");
        }
    }

    void CommandShell::_cmd_clear(const char*, uint16_t) { _output.clear(); }

    void CommandShell::_cmd_logs(const char* args, uint16_t alen) {
        uint16_t wlen; const char* w = arg_next(args, alen, wlen);
        if (w == nullptr)
            _println(_output.logs_enabled() ? "logs: output enabled"
                                            : "logs: output disabled");
        else if (arg_eq(w, wlen, "on")) {
            _output.set_logs_enabled(true);  _println("logs: output enabled");
        }
        else if (arg_eq(w, wlen, "off")) {
            _output.set_logs_enabled(false); _println("logs: output disabled");
        }
        else _println("usage: logs [on|off]");
    }

    void CommandShell::_cmd_breakpoint(const char*, uint16_t) { HAL::Debug::breakpoint(); }

    // Drop the field to its safe state before a deliberate restart: neither
    // reset() nor shutdown() promises anything about the outputs, and the gap
    // across a reboot is exactly when an actuator must not be left energised.
    void CommandShell::_leave_running() {
        // The system's hold, not the operator's: the firmware is deciding to
        // stop running, not someone turning control off.
        if (Vars::Bank* io = _bank()) {
            io->set_health_hold(true);
            // A deliberate stop loses no counting: the retain cadence bounds
            // what a power cut costs, a planned stop should cost nothing.
            (void)io->save_retained();
        }
        // Wipe the screen (flushed straight to the wire) before leaving: the
        // main loop won't run again to drain the TX ring.
        _terminal.clear_screen_now();
    }

    void CommandShell::_cmd_reset(const char*, uint16_t) {
        _leave_running();
        HAL::System::reset();      // reboots; does not return
    }

    void CommandShell::_cmd_exit(const char*, uint16_t) {
        _leave_running();
        HAL::System::shutdown();   // sim: exit(0); OPTA: reboots
    }

    void CommandShell::_cmd_stats(const char*, uint16_t) {
        // Emits several lines; scrollback drains them at LOG_ROWS_PER_TICK.
        Scheduler::SchedulerContext* ctx = _sys != nullptr ? _sys->scheduler : nullptr;
        if (ctx == nullptr) { _println("stats: no scheduler context"); return; }

        char out[Config::LOG_FORMATTED_LEN];
        char val[24], detail[64];
        char fa[12], fb[12];

        snprintf(val,    sizeof(val),    "%lus",
            (unsigned long)(HAL::Clock::uptime_ms() / 1000));
        snprintf(detail, sizeof(detail), "%lu ticks, %lu overruns",
            (unsigned long)ctx->tick_count, (unsigned long)ctx->overruns);
        _emitf(out, sizeof(out), "%-29s : %-8s (%s)",
            "Uptime ......................", val, detail);

        snprintf(val,    sizeof(val),    "%s%%",
            Utils::format_float(fa, sizeof(fa), ctx->cpu_load_ema * 100.0f, 1));
        snprintf(detail, sizeof(detail), "recent %sms, peak %sms",
            Utils::format_float(fa, sizeof(fa), ctx->cpu_loop_us_recent / 1000.0, 2),
            Utils::format_float(fb, sizeof(fb), ctx->cpu_loop_us_max / 1000.0, 2));
        _emitf(out, sizeof(out), "%-29s : %-8s (%s)",
            "CPU load ....................", val, detail);

        snprintf(val,    sizeof(val),    "%s%%",
            Utils::format_float(fa, sizeof(fa), ctx->cpu_jitter_ema * 100.0f, 1));
        snprintf(detail, sizeof(detail), "recent %sms, peak %sms",
            Utils::format_float(fa, sizeof(fa), ctx->cpu_jitter_us_recent / 1000.0, 2),
            Utils::format_float(fb, sizeof(fb), ctx->cpu_jitter_us_max / 1000.0, 2));
        _emitf(out, sizeof(out), "%-29s : %-8s (%s)",
            "CPU jitter ..................", val, detail);

        snprintf(val,    sizeof(val),    "%s%%",
            Utils::format_float(fa, sizeof(fa), ctx->stack_usage * 100.0f, 1));
        snprintf(detail, sizeof(detail), "%s free, %s total",
            Utils::format_size(fa, sizeof(fa), ctx->stack_bytes_free),
            Utils::format_size(fb, sizeof(fb), ctx->stack_depth));
        _emitf(out, sizeof(out), "%-29s : %-8s (%s)",
            "Stack usage .................", val, detail);

        snprintf(detail, sizeof(detail), "%lu TX dropped, %lu RX overruns",
            (unsigned long)_terminal.serial().tx_dropped(),
            (unsigned long)_terminal.serial().rx_overrun());
        _emitf(out, sizeof(out), "%-29s : %s",
            "Console serial ..............", detail);
    }

    void CommandShell::_cmd_tasks(const char* args, uint16_t alen) {
        const Tasks::Registry* reg = (_sys != nullptr) ? _sys->tasks : nullptr;
        if (reg == nullptr) { _println("tasks: no task registry"); return; }

        char out[Config::LOG_FORMATTED_LEN];
        uint16_t wlen = 0;
        const char* want = arg_next(args, alen, wlen);

        if (want != nullptr) { _task_detail(*reg, want, wlen); return; }

        // The beat as a whole, in `stats`' shape - NOT a listing: 22 rows over
        // a 4800-baud console spend the byte budget on an index of the TASKS
        // page. Only what no per-task row can say: the totals, and which task
        // to go and look at.
        uint64_t runs = 0, peaks_us = 0, missed = 0;
        uint8_t  missed_tasks = 0;
        const Tasks::Task* worst   = nullptr;   // most missed activations
        const Tasks::Task* busiest = nullptr;   // dearest mean run
        for (uint8_t i = 0; i < reg->count(); i++) {
            const Tasks::Task& t = reg->task(i);
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

        snprintf(val, sizeof(val), "%u", (unsigned)reg->count());
        snprintf(detail, sizeof(detail), "%u refused, %u slots",
                 (unsigned)reg->overflow(), (unsigned)Config::MAX_TASKS);
        _emitf(out, sizeof(out), "%-29s : %-8s (%s)",
               "Registered ..................", val, detail);

        // The rate classes, since a task's period is its group's. Names then
        // periods, both slash-joined: the pairwise form ("fast 10, main 50")
        // is three characters too wide for an 80-column line.
        snprintf(val, sizeof(val), "%lums", (unsigned long)Config::TASK_PERIOD_MS);
        int w = 0;
        for (uint8_t g = 0; g < Tasks::GROUP_COUNT && w >= 0 && w < (int)sizeof(detail); g++)
            w += snprintf(detail + w, sizeof(detail) - (size_t)w, "%s%s",
                          (g == 0) ? "" : "/", Tasks::group_name((Tasks::GroupId)g));
        for (uint8_t g = 0; g < Tasks::GROUP_COUNT && w >= 0 && w < (int)sizeof(detail); g++)
            w += snprintf(detail + w, sizeof(detail) - (size_t)w, "%s%lu",
                          (g == 0) ? " " : "/",
                          (unsigned long)Tasks::group_period_ms((Tasks::GroupId)g));
        _emitf(out, sizeof(out), "%-29s : %-8s (%s ms)",
               "Beat ........................", val, detail);

        // Missed is the headline: a dropped activation is never caught up
        // (Tasks::TaskTimer), so this is the only record of a rate not kept.
        _emitf(out, sizeof(out), "%-29s : %-8s (across %u task(s))",
               "Activations .................",
               Utils::format_int(n1, sizeof(n1), (long long)runs),
               (unsigned)reg->count());

        if (missed == 0)
            snprintf(detail, sizeof(detail), "every task kept its rate");
        else
            snprintf(detail, sizeof(detail), "%u task(s), worst %s with %s",
                     (unsigned)missed_tasks,
                     (worst != nullptr && worst->name != nullptr) ? worst->name : "?",
                     Utils::format_int(n2, sizeof(n2),
                         (long long)(worst != nullptr ? worst->cost.missed : 0)));
        _emitf(out, sizeof(out), "%-29s : %-8s (%s)",
               "Missed ......................",
               Utils::format_int(n1, sizeof(n1), (long long)missed), detail);

        // Every task at its own worst, all due together: the beat that never
        // happened. Exceeding the beat period is normal, not an overrun.
        snprintf(val, sizeof(val), "%sus",
                 Utils::format_int(n1, sizeof(n1), (long long)peaks_us));
        _emitf(out, sizeof(out), "%-29s : %-8s (%s)",
               "Peaks add to ................", val,
               "every task at its worst, one beat");

        // One name to go and look at, since the table is the TASKS page's.
        if (busiest != nullptr && busiest->cost.runs != 0) {
            const uint32_t period = Tasks::group_period_ms(busiest->group);
            char fa[12];
            snprintf(val, sizeof(val), "%luus",
                     (unsigned long)busiest->cost.us_mean());
            if (period != 0)
                snprintf(detail, sizeof(detail), "%s mean, %s %% of its own period",
                         (busiest->name != nullptr) ? busiest->name : "?",
                         Utils::format_float(fa, sizeof(fa),
                             (double)busiest->cost.us_mean() / (double)(period * 10u), 1));
            else
                snprintf(detail, sizeof(detail), "%s mean, every beat",
                         (busiest->name != nullptr) ? busiest->name : "?");
            _emitf(out, sizeof(out), "%-29s : %-8s (%s)",
                   "Costliest task ..............", val, detail);
        }

        _println("per-task figures: the TASKS page, or `tasks <name>`");
    }

    void CommandShell::_task_detail(const Tasks::Registry& reg,
                                    const char* name, uint16_t nlen) {
        for (uint8_t i = 0; i < reg.count(); i++) {
            const Tasks::Task& t = reg.task(i);
            if (t.name == nullptr || !arg_eq(name, nlen, t.name)) continue;

            char val[64], fa[12];
            const uint32_t period = Tasks::group_period_ms(t.group);

            snprintf(val, sizeof(val), "%s (%s, every %lu ms)",
                     Tasks::phase_name(t.phase), Tasks::group_name(t.group),
                     (unsigned long)period);
            _line("Runs in ..............", val);

            // last_run_ms() is meaningless until the first activation, so a task
            // that has never run says so rather than reporting the uptime.
            // Missed rides here because it only means anything against the run
            // count.
            if (t.timer.runs() == 0) snprintf(val, sizeof(val), "none yet");
            else snprintf(val, sizeof(val), "%lu run, %lu missed, last %lu ms ago",
                          (unsigned long)t.timer.runs(),
                          (unsigned long)t.cost.missed,
                          (unsigned long)(HAL::Clock::uptime_ms() - t.timer.last_run_ms()));
            _line("Activations ..........", val);

            snprintf(val, sizeof(val), "last %lu us, mean %lu us, peak %lu us",
                     (unsigned long)t.cost.us_last,
                     (unsigned long)t.cost.us_mean(),
                     (unsigned long)t.cost.us_max);
            _line("Cost .................", val);

            // Against its OWN period, not the beat: a 500 ms task costing 200 us
            // is idle, and measuring it against 10 ms would say otherwise.
            if (t.cost.runs != 0 && period != 0) {
                // us_mean/period_ms is already per-mille - the us/ms ratio does
                // the scaling - so this stays a 32-bit divide.
                snprintf(val, sizeof(val), "%s %% of its own period",
                         Utils::format_float(fa, sizeof(fa),
                             (double)t.cost.us_mean() / (double)(period * 10u), 2));
                _line("Duty .................", val);
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

    void CommandShell::_cmd_alarms(const char* args, uint16_t alen) {
        uint16_t wlen; const char* sub = arg_next(args, alen, wlen);
        char out[80];

        // "alarms ack <code|all>" - mutates, so it needs the model wired via
        // set_diagnostics.
        if (sub && arg_eq(sub, wlen, "ack")) {
            if (_alarm_model == nullptr) {
                _println("alarms: model unavailable"); return;
            }
            uint16_t tlen; const char* tgt = arg_next(args, alen, tlen);
            if (tgt == nullptr) {
                _println("usage: alarms ack <code|name|all>"); return;
            }
            if (arg_eq(tgt, tlen, "all")) {
                _emitf(out, sizeof(out), "acknowledged %u active alarm(s)",
                       _alarm_model->ack_all());
            } else {
                char code[24];
                uint16_t cl = std::min(tlen, (uint16_t)(sizeof(code) - 1));
                memcpy(code, tgt, cl); code[cl] = '\0';
                _emitf(out, sizeof(out), _alarm_model->ack(code)
                       ? "acknowledged %s" : "no active alarm '%s'", code);
            }
            return;
        }

        const Alarms::AlarmController* m = _alarm_model;
        if (m == nullptr) { _println("alarms: model unavailable"); return; }

        // "alarms list" - one paced line per active condition: code, age,
        // flags.
        if (sub && arg_eq(sub, wlen, "list")) {
            if (m->active_count() == 0) { _println("no active alarms"); return; }
            char age[16];
            uint64_t now = HAL::Clock::uptime_ms();
            for (uint8_t i = 0; i < m->count(); i++) {
                const Alarms::AlarmCondition& c = m->condition(i);
                if (!c.active) continue;
                uint32_t secs = (uint32_t)((now - c.started_ms) / 1000);
                // Code first (what the alarm bar shows and what `alarms ack`
                // takes), then the variable name (what `get` takes).
                _emitf(out, sizeof(out), "  %-9s %-22s %s%s",
                    c.code() ? c.code() : "?",
                    c.name() ? c.name() : "?",
                    Utils::format_age(age, sizeof(age), secs, false),
                    c.user_ack ? " [ack]" : "");
            }
            return;
        }

        // Default: the count summary.
        _emitf(out, sizeof(out), "alarms: %u active", m->active_count());
    }

    void CommandShell::_emit_var(const Vars::Bank& io, Vars::Var h) {
        char out[128], val[48];
        const char* s = Vars::var_format(io, h, val, sizeof(val));
        const char* u = h.meta()->unit;
        // Flag the three things a bare number hides: the scope (on every row,
        // since it decides whether `set` or `override` applies), that it is
        // forced by hand, and that a reading is really its default because
        // nothing has written it.
        _emitf(out, sizeof(out), "  %-20s %-12s %-4s (%s)%s%s", h.meta()->name, s,
               (u && u[0]) ? u : "",
               Vars::var_scope(*h.meta()),
               h.overridden() ? " OVERRIDDEN" : "",
               h.valid() ? "" : " (default: nothing has written it)");
    }

    // The detail view: what kind of row, whether you may write it, what it
    // reads underneath any override. Laid out like `stats` and `net` (dotted
    // leader to a fixed column, value, detail in parentheses).
    void CommandShell::_cmd_get(const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        if (io == nullptr) { _println("get: no variables"); return; }
        uint16_t wn = 0;
        const char* nm = arg_next(args, alen, wn);
        if (nm == nullptr) { _println("usage: get <name>   (list [<prefix>] to browse)"); return; }

        const Vars::VarRef r = io->find(nm, wn);
        if (r.index == Vars::VAR_NONE) { _println("get: unknown variable"); return; }
        const Vars::Var h = io->var(r);
        const Vars::Variable* d = h.meta();

        char out[Config::LOG_FORMATTED_LEN], val[64], det[80];
        const char* u = (d->unit && d->unit[0]) ? d->unit : "";

        // The name and what every other reader sees for it: the override
        // applied. The rows below take that apart.
        _emitf(out, sizeof(out), "%s = %s %s", d->name,
               Vars::var_format(*io, h, val, sizeof(val)), u);

        // Both derived (scope from the factory that declared it, group from the
        // name), so neither can disagree with the table.
        _field("Scope", Vars::var_scope(*d), _scope_phrase(d->scope));
        _field("Group", Vars::var_group(*d), nullptr);
        // Whether `set` works; everything is forceable, so that is the only
        // question.
        _field("Access", _writable(h) ? "read-write" : "read-only", nullptr);

        // The two values, kept apart: an override SHADOWS (the driver keeps
        // publishing underneath), so "held at what" and "would read what if
        // released" are different questions. The raw row says WHY it is
        // invalid, not just that it is.
        _value_field("Raw value", *io, h, h.live(), Vars::quality_str(h.live_quality()), u);
        // The override's quality is a person's choice, not a diagnosis: a yes
        // or a no.
        if (h.overridden())
            _value_field("Override value", *io, h, h.raw(),
                         h.valid() ? "valid" : "invalid", u);
        else
            _field("Override value", "-", nullptr);

        // The declared band, live: an analog row's comes from its calibration
        // parameters, so var_band() rather than the flash constants.
        if ((d->flags & Vars::F_BOUNDED) != 0 && d->type != Vars::T_STR) {
            char lo[24], hi[24];
            const Vars::NumBand b = Vars::var_band(io, *d);
            snprintf(det, sizeof(det), "%s .. %s %s",
                     Vars::format_scalar(d->type, b.min, d->digits, nullptr, lo, sizeof(lo)),
                     Vars::format_scalar(d->type, b.max, d->digits, nullptr, hi, sizeof(hi)), u);
            _field(d->role == Vars::ROLE_PARAM ? "Range" : "Plausible", det, nullptr);
        }
        // A labelled enum's bound is its label set, so that is what its "Range"
        // is - and it belongs here rather than in the notes, which are free text
        // and drift from the converter the moment one gains a label.
        else if (d->type == Vars::T_ENUM && d->str != nullptr) {
            size_t n = 0;
            for (uint32_t code = 0; code <= 255u && n + 1 < sizeof(det); code++) {
                const char* label = d->str(code);
                if (label == nullptr) continue;
                if (n != 0 && n + 1 < sizeof(det)) det[n++] = '|';
                while (*label != 0 && n + 1 < sizeof(det)) det[n++] = *label++;
            }
            det[n] = 0;
            if (n != 0) _field("Range", det, nullptr);
        }

        // Where the value physically comes from.
        _field("Source", _binding_phrase(d->bind, det, sizeof(det)), nullptr);
        if (d->desc  && d->desc[0])  _field("Description", d->desc,  nullptr);
        if (d->notes && d->notes[0]) _field("Notes",       d->notes, nullptr);
    }

    // The leader is built rather than written into each label, so the column
    // cannot drift one row at a time.
    void CommandShell::_field(const char* label, const char* value,
                              const char* detail) {
        char lead[LEADER_W + 1];
        size_t n = 0;
        while (label[n] != 0 && n + 1 < sizeof(lead)) { lead[n] = label[n]; n++; }
        if (n + 1 < sizeof(lead)) lead[n++] = ' ';
        while (n + 1 < sizeof(lead)) lead[n++] = '.';
        lead[n] = 0;

        char out[Config::LOG_FORMATTED_LEN];
        if (detail != nullptr && detail[0] != 0)
            _emitf(out, sizeof(out), "%s : %-8s (%s)", lead, value, detail);
        else
            _emitf(out, sizeof(out), "%s : %s", lead, value);
    }

    // Quality rides on the value row rather than one of its own: a value and
    // whether it is good are one fact, and there are two values.
    void CommandShell::_value_field(const char* label, const Vars::Bank& io,
                                    Vars::Var h, Vars::Scalar v,
                                    const char* quality, const char* unit) {
        const Vars::Variable* d = h.meta();
        char num[48], cell[64];
        // A T_STR keeps its live text in the pool, not the slot, so the Scalar
        // is not where its value is; var_format() is the only reader that knows.
        const char* text = (d->type == Vars::T_STR)
            ? Vars::var_format(io, h, num, sizeof(num))
            : Vars::format_scalar(d->type, v, d->digits, d->str, num, sizeof(num));
        snprintf(cell, sizeof(cell), "%s%s%s", text, unit[0] ? " " : "", unit);
        _field(label, cell, quality);
    }

    // What a scope abbreviation means, for the detail column: the table shows
    // the two-character code because a column cannot spare eight.
    const char* CommandShell::_scope_phrase(uint8_t scope) {
        switch ((Vars::VarScope)scope) {
            case Vars::SCOPE_A:  return "alarm condition";
            case Vars::SCOPE_P:  return "parameter";
            case Vars::SCOPE_G:  return "computed";
            case Vars::SCOPE_SV: return "status value, published";
            case Vars::SCOPE_MI: return "input, direct";
            case Vars::SCOPE_AI: return "input, calibrated";
            case Vars::SCOPE_DI: return "input, digital";
            case Vars::SCOPE_MO: return "output, direct";
            case Vars::SCOPE_AO: return "output, scaled";
            case Vars::SCOPE_DO: return "output, digital";
            case Vars::SCOPE_RO: return "relay coil";
            default:             return "?";
        }
    }

    // A parameter, unless the site marked it read-only; everything else is
    // driven by something and would be overwritten on the next scan.
    bool CommandShell::_writable(Vars::Var h) {
        const Vars::Variable* d = h.meta();
        return d->role == Vars::ROLE_PARAM && (d->flags & Vars::F_RDONLY) == 0;
    }

    // Mirrors the MONITOR page's Source field, so a row reads the same whichever
    // showed it.
    const char* CommandShell::_binding_phrase(const Vars::Binding& b,
                                              char* buf, size_t cap) {
        switch (b.transport) {
            case Vars::XP_MODBUS:
                snprintf(buf, cap, "Modbus dev %u fc %u reg %u",
                         (unsigned)b.device, (unsigned)b.fc, (unsigned)b.reg);
                break;
            case Vars::XP_LOCAL:
                // The wire type is what makes a local channel analog or digital;
                // the transport only says it is not Modbus.
                snprintf(buf, cap, "%s channel %u",
                         (b.wire == Vars::T_BOOL) ? "digital" : "analog",
                         (unsigned)b.reg);
                break;
            default:
                snprintf(buf, cap, "computed (nothing drives it)");
                break;
        }
        return buf;
    }

    void CommandShell::_cmd_list(const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        if (io == nullptr) { _println("list: no variables"); return; }
        uint16_t wp = 0;
        const char* prefix = arg_next(args, alen, wp);

        // Variables are named by domain (`pwr_`, `env_`), so a prefix is the
        // subsystem view without a grouping concept in the command.
        uint16_t n = 0;
        for (uint16_t i = 0; i < io->count(); i++) {
            Vars::Var h = io->var(i);
            const char* name = h.meta()->name;
            if (name == nullptr) continue;
            if (prefix != nullptr && strncmp(name, prefix, wp) != 0) continue;
            _emit_var(*io, h);
            n++;
        }
        char out[96];
        if (n == 0) _println("list: nothing matches");
        else        _emitf(out, sizeof(out), "  %u variable(s)", n);
    }

    void CommandShell::_cmd_params(const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        if (io == nullptr) { _println("params: no variables"); return; }
        char out[128];

        uint16_t ws = 0;
        const char* sub = arg_next(args, alen, ws);

        // Bare `params`: what is edited and not yet written. This is the ONLY
        // thing that says so, and an edit costs nothing until it is saved, so a
        // silent pending state is how an afternoon's tuning gets lost to a power
        // blip. Names them, because "3 unsaved" is not a diagnosis.
        if (sub == nullptr) {
            const uint16_t n = io->unsaved_params();
            if (n == 0) { _println("params: nothing unsaved"); return; }
            for (uint16_t i = 0; i < io->count(); i++) {
                Vars::Var h = io->var(i);
                if (!h.unsaved()) continue;
                char val[48];
                _emitf(out, sizeof(out), "  %-24s %s", h.meta()->name,
                       Vars::var_format(*io, h, val, sizeof(val)));
            }
            _emitf(out, sizeof(out), "%u unsaved (`params save` to write)", n);
            return;
        }

        if (arg_eq(sub, ws, "save")) {
            const uint16_t n = io->save_params();
            if (n == 0) _println("params: nothing to save");
            else _emitf(out, sizeof(out), "params: %u written", (unsigned)n);
            return;
        }

        if (!arg_eq(sub, ws, "set")) {
            _println("usage: params [set <parameter> <value>|save]"); return;
        }

        uint16_t wn; const char* nm = arg_next(args, alen, wn);
        uint16_t wv; const char* v  = arg_next(args, alen, wv);
        if (nm == nullptr || v == nullptr) {
            _println("usage: params set <parameter> <value>"); return;
        }
        const Vars::VarRef r = io->find(nm, wn);
        if (r.index == Vars::VAR_NONE) { _println("params: unknown variable"); return; }
        Vars::Var p = io->var(r);
        // Point at `overrides set` rather than failing flatly: the next scan
        // would put a value written here straight back.
        if (p.role() != Vars::ROLE_PARAM) {
            _emitf(out, sizeof(out),
                   "params: %s is not a parameter (overrides set %s <value> forces it)",
                   p.meta()->name, p.meta()->name);
            return;
        }

        char val[96];
        uint16_t vc = std::min(wv, (uint16_t)(sizeof(val) - 1));
        memcpy(val, v, vc); val[vc] = '\0';

        if (io->set(p, val) != Vars::SET_OK) {
            _emitf(out, sizeof(out), "params: invalid value for %s", p.meta()->name);
            return;
        }
        // "unsaved" on the confirmation of every edit, not just once a session:
        // the value is LIVE now and gone at the next reboot, and telling the two
        // apart is the operator's whole job here.
        _emitf(out, sizeof(out), "%s = %s  (unsaved)", p.meta()->name, val);
    }

    void CommandShell::_cmd_overrides(const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        if (io == nullptr) { _println("overrides: no variables"); return; }

        char out[128];
        uint16_t ws = 0;
        const char* sub = arg_next(args, alen, ws);

        // Bare `overrides` (or `overrides list`): what is forced right now, and
        // what of it would not survive a reboot. An override drives the plant by
        // hand, so both halves have to be one command away.
        if (sub == nullptr || arg_eq(sub, ws, "list")) {
            char val[48];
            uint16_t n = 0;
            for (uint16_t i = 0; i < io->count(); i++) {
                Vars::Var h = io->var(i);
                if (!h.overridden()) continue;
                const char* s = Vars::var_format(*io, h, val, sizeof(val));
                _emitf(out, sizeof(out), "  %-20s %-12s %s%s",
                       h.meta()->name, s, h.valid() ? "" : "(reading as invalid) ",
                       h.override_unsaved() ? "(unsaved)" : "");
                n++;
            }
            if (n == 0) { _println("overrides: none active"); return; }
            const uint16_t u = io->unsaved_overrides();
            _emitf(out, sizeof(out),
                   "%u active, %u unsaved (`overrides clear` / `overrides save`)",
                   n, u);
            return;
        }

        // Releases the lot. Unlike setting one, this reaches the store at once:
        // an override that survived its own release to come back at the next
        // boot is the hazard the safe state exists to prevent.
        if (arg_eq(sub, ws, "clear")) {
            _emitf(out, sizeof(out), "overrides: cleared %u", io->clear_overrides());
            return;
        }

        if (arg_eq(sub, ws, "save")) {
            const uint16_t n = io->save_overrides();
            if (n == 0) { _println("overrides: nothing to save"); return; }
            // CRITICAL, not a reply: this is the moment a hand-driven plant
            // becomes something that outlives the reboot, and the boot that
            // restores it says so just as loudly.
            _emitf(out, sizeof(out), "overrides: %u written - they now survive a reboot",
                   (unsigned)n);
            return;
        }

        if (!arg_eq(sub, ws, "set")) {
            _println("usage: overrides [set <name> [off|<value>] [valid|invalid]|clear|save]");
            return;
        }

        uint16_t wn = 0;
        const char* nm = arg_next(args, alen, wn);
        if (nm == nullptr) { _println("usage: overrides set <name> [<value>]"); return; }
        const Vars::VarRef r = io->find(nm, wn);
        if (r.index == Vars::VAR_NONE) { _println("overrides: unknown variable"); return; }
        Vars::Var h = io->var(r);

        uint16_t wa = 0;
        const char* a = arg_next(args, alen, wa);

        if (a != nullptr && arg_eq(a, wa, "off")) {
            // Through the bank, not the handle: the handle cannot reach the
            // store, and a release leaving the stored copy behind would come
            // back at the next boot.
            io->override_clear(h);
            _emitf(out, sizeof(out), "%s: override off", h.meta()->name);
            return;
        }
        // "on" is optional sugar: `overrides set x 20` and `... x on 20` are
        // the same thing, and a bare name freezes.
        if (a != nullptr && arg_eq(a, wa, "on")) a = arg_next(args, alen, wa);

        char val[48];
        // A trailing `valid` / `invalid` says what the forced value READS as:
        // "invalid" stands a variable up as a dead sensor without touching what
        // it reports. Accepted either side of the value.
        bool valid = true;
        auto take_validity = [&](const char* w, uint16_t wl) {
            if (w == nullptr) return false;
            if (!arg_eq(w, wl, "valid") && !arg_eq(w, wl, "invalid")) return false;
            valid = arg_eq(w, wl, "valid");
            return true;
        };

        if (take_validity(a, wa)) a = arg_next(args, alen, wa);   // may be null

        if (a == nullptr) {
            // No value: hold it where it is, with the same quality default as
            // the value form (good unless `invalid` was asked for), never the
            // variable's current quality. Through the bank, not the handle.
            io->override_set(h, h.raw(), valid);
            const char* s = Vars::var_format(*io, h, val, sizeof(val));
            _emitf(out, sizeof(out), "%s: frozen at %s%s  (unsaved)", h.meta()->name, s,
                   h.valid() ? "" : " (reading as invalid)");
            return;
        }

        char text[64];
        uint16_t vc = std::min(wa, (uint16_t)(sizeof(text) - 1));
        memcpy(text, a, vc); text[vc] = '\0';

        // Sequenced deliberately: arg_next() WRITES wt, so passing both to one
        // call leaves the read of wt unordered against it.
        uint16_t wt = 0;
        const char* t = arg_next(args, alen, wt);
        (void)take_validity(t, wt);

        if (io->override_set(h, text, valid) != Vars::SET_OK) {
            _emitf(out, sizeof(out), "overrides: invalid value for %s",
                   h.meta()->name);
            return;
        }
        const char* s = Vars::var_format(*io, h, val, sizeof(val));
        _emitf(out, sizeof(out), "%s: forced to %s%s  (unsaved)", h.meta()->name, s,
               valid ? "" : " (reading as invalid)");
    }

    void CommandShell::_cmd_control(const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        if (io == nullptr) { _println("control: no variables"); return; }
        uint16_t w = 0;
        const char* a = arg_next(args, alen, w);

        if (a == nullptr) { _report_control(io); return; }

        if (arg_eq(a, w, "enable")) {
            io->set_control_mode(Vars::CONTROL_ENABLED);
            _report_control(io);           // ...which the system may still veto
            return;
        }
        if (arg_eq(a, w, "inhibit")) {
            // The programs stop deciding, but `override` still reaches an
            // actuator - the whole reason to type this before working on the
            // plant.
            io->set_control_mode(Vars::CONTROL_INHIBITED);
            _println("control: INHIBITED - outputs at their safe state, scan "
                     "stopped; `override` still drives them by hand");
            return;
        }
        if (arg_eq(a, w, "disable")) {
            io->set_control_mode(Vars::CONTROL_DISABLED);
            _println("control: DISABLED - outputs at their safe state, scan "
                     "stopped, output overrides ignored (kept, not cleared)");
            return;
        }
        _println("usage: control [enable|inhibit|disable]");
    }

    // The operator's choice and the system's hold are reported separately: a
    // `control enable` that changes nothing must read as a health fault still in
    // force rather than a broken command.
    void CommandShell::_report_control(Vars::Bank* io) {
        const Vars::ControlMode eff = io->control_mode();
        const Vars::ControlMode op  = io->operator_mode();
        char out[160];
        if (!io->health_hold()) {
            _emitf(out, sizeof(out), "control: %s",
                   Vars::Bank::control_mode_name(eff));
            return;
        }
        // Held by the system: say so, and say what the operator asked for, so
        // releasing the fault has a predictable result.
        _emitf(out, sizeof(out),
               "control: %s - held by the system (bring-up, health, or a "
               "pending reset); operator asked for %s",
               Vars::Bank::control_mode_name(eff),
               Vars::Bank::control_mode_name(op));
    }

    void CommandShell::_cmd_save(const char* args, uint16_t alen) {
        (void)args; (void)alen;
        Vars::Bank*   io = _bank();
        Vars::Params* p  = _params();
        if (io == nullptr || p == nullptr) { _println("save: no variables"); return; }
        // Everything pending, in one word. Three different kinds of state -
        // edited settings, forced values, and counters that ARE their own
        // history - but one moment when you want them all on flash, which is
        // just before the power goes. `params save` / `overrides save` stay for
        // when you mean only one of them.
        char out[128];
        const uint16_t np = io->save_params();
        const uint16_t no = io->save_overrides();
        const uint16_t nr = p->save_retained(HAL::Clock::uptime_ms());
        if (np == 0 && no == 0 && nr == 0) { _println("save: nothing pending"); return; }
        _emitf(out, sizeof(out),
               "save: %u parameter(s), %u override(s), %u retained value(s)",
               (unsigned)np, (unsigned)no, (unsigned)nr);
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
        _line("Network ..............", val);

        // The active link parameters (DHCP lease or static config, whichever is
        // live), read back from the HAL so a DHCP-assigned value shows too.
        HAL::Network::Ip4 a4;
        _line("IP address ...........", net->ip(&a4) ? ip4(a4) : "-");
        _line("Netmask ..............",
             HAL::Network::link_netmask(&a4) ? ip4(a4) : "-");
        _line("Gateway ..............",
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
            _line("DNS ..................", val);
        }

        // --- NTP ---------------------------------------------------------
        if (ntp != nullptr) {
            if (ntp->synced())
                snprintf(val, sizeof(val), "%s  (offset %+ld ms)",
                         ntp->status_str(), (long)ntp->offset_ms());
            else
                snprintf(val, sizeof(val), "%s", ntp->status_str());
            _line("NTP ..................", val);
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
                _line("NTP server ...........", val);
            }
        } else {
            _line("NTP ..................", "disabled");
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
            _line("MQTT .................", val);
            const char* brk = Vars::get_str(_bank(), "mqtt_host");
            if (brk[0]) {
                // Configured hostname, then the resolved IP:port once connected.
                if (mqt->connected()) {
                    Network::ResolvedAddr pa = mqt->peer_addr();
                    snprintf(val, sizeof(val), "%s  %s:%u", brk,
                             ip4(pa), (unsigned)mqt->peer_port());
                } else {
                    snprintf(val, sizeof(val), "%s", brk);
                }
                _line("MQTT broker ..........", val);
            }
        } else {
            _line("MQTT .................", "disabled");
        }
    }
#endif  // BOTANIK_FEATURE_NETWORK

// modbus - the field bus and the SCADA-facing server
// ---------------------------------------------------------------------------

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    /** @brief The roll-call: one line per unit. The plant's tag first (an
     *  operator knows the box by its designation, not its unit id), then how
     *  much of the scan it accounts for, then the fastest period any of its
     *  blocks sits on, which bounds how quickly a fault on it is noticed. */
    void CommandShell::_modbus_devices(const Modbus::RtuClient& rtu) {
        char val[96];
        const Modbus::Planner& plan = rtu.plan();
        for (uint8_t i = 0; i < rtu.peer_count(); ++i) {
            const uint8_t unit = rtu.peer_unit(i);

            uint16_t blocks = 0, rows = 0;
            uint32_t fastest = 0;
            for (uint16_t b = 0; b < plan.block_count(); ++b) {
                const Modbus::Block& blk = plan.block(b);
                if (blk.device != unit) continue;
                blocks++;
                rows = (uint16_t)(rows + blk.row_count);
                if (blk.period_ms != 0 && (fastest == 0 || blk.period_ms < fastest))
                    fastest = blk.period_ms;
            }

            const char* tag = Modbus::device_tag(rtu.devices(), unit);
            char rate[16];
            if (fastest == 0) snprintf(rate, sizeof(rate), "%s", "on demand");
            else if (fastest >= 1000) snprintf(rate, sizeof(rate), "%us", (unsigned)(fastest / 1000u));
            else snprintf(rate, sizeof(rate), "%ums", (unsigned)fastest);

            // The state last: the column an eye runs down for the one that is
            // not "ok".
            char state[16];
            if (!rtu.peer_dead(i)) snprintf(state, sizeof(state), "%s", "ok");
            else snprintf(state, sizeof(state), "DEAD x%u", (unsigned)rtu.peer_misses(i));

            snprintf(val, sizeof(val), "  %-18s dev %-3u %2u blk %3u row %-9s %s",
                     (tag != nullptr && tag[0] != '\0') ? tag : "-",
                     (unsigned)unit, (unsigned)blocks, (unsigned)rows, rate, state);
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

        // `modbus devices` is the roll-call; a bare `modbus` is the plan and
        // the state. Matched on its first letter, like the other sub-commands.
        uint16_t wn = 0;
        const char* sub = arg_next(args, alen, wn);
        const bool want_devices = (sub != nullptr && wn > 0 && sub[0] == 'd');

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        const Modbus::RtuClient* rtu = _sys->modbus_rtu;
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
            _line("  Plan ...............", val);

            snprintf(val, sizeof(val), "%s, %u baud, %u.%u%% of the bus",
                     rtu->status_str(), (unsigned)Config::MODBUS_BAUD,
                     (unsigned)(plan.duty_ppm() / 10000u),
                     (unsigned)((plan.duty_ppm() / 1000u) % 10u));
            _line("  State ..............", val);

            snprintf(val, sizeof(val), "%lu sent, %lu faults, %lu refused, %lu short tx, %lu late B",
                     (unsigned long)rtu->txns(), (unsigned long)rtu->faults(),
                     (unsigned long)rtu->refusals(), (unsigned long)rtu->tx_short(),
                     (unsigned long)rtu->stale_rx());
            _line("  Transactions .......", val);

            // The framer has always kept these apart because they point at
            // different physical faults, and until now nothing read them: a bus
            // failing on CRC is a cable, a terminator or the wrong baud; one
            // failing on clean timeouts is power or addressing; and bytes out of
            // turn are a second master or a duplicate address.
            const Modbus::RtuFramer& fr = rtu->framer();
            snprintf(val, sizeof(val), "%lu crc, %lu bad frame, %lu timeout, %lu stray B",
                     (unsigned long)fr.crc_errors(), (unsigned long)fr.bad_frames(),
                     (unsigned long)fr.timeouts(), (unsigned long)fr.stray_bytes());
            _line("  Line ...............", val);

            // Unplanned rows are signals that will never be scanned: a critical
            // at bring-up, repeated here rather than only in a log.
            if (plan.rows_dropped() != 0 || plan.rows_unscanned() != 0) {
                snprintf(val, sizeof(val), "%u unplanned, %u never scanned",
                         (unsigned)plan.rows_dropped(),
                         (unsigned)plan.rows_unscanned());
                _line("  Rows dropped .......", val);
            }

            if (rtu->safe_state_pending())
                _println("  (safe state owed to the field)");

            // Twenty devices do not fail at once. Said here as well as in the
            // alarm, because this is the screen an engineer is already looking
            // at when they ask which cabinet to walk to.
            if (rtu->peer_count() != 0 && rtu->dead_count() == rtu->peer_count())
                _println("  (the whole segment is quiet: trunk, termination, "
                         "this unit's transceiver, or a second master)");

            // Counts only: twenty units is a screenful that would push the plan
            // and the state off the top. WHICH ones is `modbus devices`.
            if (!want_devices) {
                snprintf(val, sizeof(val), "  %u device(s), %u not answering"
                                           "%s",
                         (unsigned)rtu->peer_count(), (unsigned)rtu->dead_count(),
                         rtu->dead_count() != 0 ? "  (modbus devices)" : "");
                _println(val);
            } else {
                _modbus_devices(*rtu);
            }
        }
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
            _line("  State ..............", val);

            snprintf(val, sizeof(val), "%lu served, %lu refused, %lu writes, %lu rejects",
                     (unsigned long)c.requests, (unsigned long)c.exceptions,
                     (unsigned long)c.writes, (unsigned long)c.rejects);
            _line("  Requests ...........", val);
        }
#endif
    }
#endif



    void CommandShell::_cmd_ntpsync(const char*, uint16_t) {
        if (_ntp_client == nullptr) { _println("ntpsync: NTP unavailable"); return; }
        _ntp_client->resync();
        _println("ntpsync: NTP resync requested");
    }

    void CommandShell::_unknown(const char* cmd, uint16_t len) {
        // Handed to write_lines() as one blob: it wraps at terminal width and
        // splits on any embedded newline, so no manual bookkeeping is needed.
        char out[Config::LOG_FORMATTED_LEN];
        static const char prefix[] = "Unknown command: ";
        uint16_t plen = (uint16_t)(sizeof(prefix) - 1);
        uint16_t clen = std::min(len, (uint16_t)(sizeof(out) - plen));
        memcpy(out, prefix, plen);
        memcpy(out + plen, cmd, clen);
        _output.write_lines(out, (uint16_t)(plen + clen));
    }

    // History
    // ------------------------------------------------------------------------

    void CommandShell::_history_add(const char* cmd, uint16_t len) {
        if (len > Config::SHELL_HISTORY_ENTRY) len = Config::SHELL_HISTORY_ENTRY;
        // Skip a consecutive duplicate of the most-recent entry.
        if (_hist_count > 0) {
            uint8_t last = (uint8_t)((_hist_head + Config::SHELL_HISTORY_SIZE - 1)
                                     % Config::SHELL_HISTORY_SIZE);
            if (_hist_len[last] == len && memcmp(_history[last], cmd, len) == 0) {
                _hist_browse = -1; return;
            }
        }
        memcpy(_history[_hist_head], cmd, len);
        _hist_len[_hist_head] = len;
        _hist_head = (uint8_t)((_hist_head + 1) % Config::SHELL_HISTORY_SIZE);
        if (_hist_count < Config::SHELL_HISTORY_SIZE) _hist_count++;
        _hist_browse = -1;   // a submit ends browsing
    }

    void CommandShell::_load_browse() {
        const char* text; uint16_t len;
        if (_hist_browse < 0) { text = _draft; len = _draft_len; }
        else {
            // browse 0 = newest (head-1); browse k = head-1-k, modulo the ring.
            uint8_t slot = (uint8_t)((_hist_head + Config::SHELL_HISTORY_SIZE - 1
                                      - (uint8_t)_hist_browse)
                                     % Config::SHELL_HISTORY_SIZE);
            text = _history[slot]; len = _hist_len[slot];
        }
        // Replace the editor line with the recalled text.
        _editor.move_to_line_end();
        _editor.backspace(_editor.buffer_len());
        _editor.insert_characters(text, len);
    }

    void CommandShell::history_prev() {
        if (_hist_count == 0) return;
        if (_hist_browse < 0) {
            // Starting to browse: stash the in-progress line as the draft.
            _draft_len = std::min(_editor.buffer_len(), (uint16_t)sizeof(_draft));
            memcpy(_draft, _editor.buffer(), _draft_len);
            _hist_browse = 0;
        } else if (_hist_browse < (int16_t)(_hist_count - 1)) {
            _hist_browse++;
        } else {
            return;   // already at the oldest entry
        }
        _load_browse();
    }

    void CommandShell::history_next() {
        if (_hist_browse < 0) return;   // not browsing
        _hist_browse--;                 // 0 -> -1 restores the draft
        _load_browse();
    }

}}
