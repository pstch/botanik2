/**
 * @file shell.cpp
 * @brief CommandShell implementation - command dispatch and built-in commands.
 */
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/alarms/alarms.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/plcio/sync.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/shell/shell.h"
#include "libbotanik/utils/text.h"

namespace Botanik { namespace Shell {

    // Lifecycle
    // ------------------------------------------------------------------------

    CommandShell::CommandShell(Terminal::AnsiTerminal& terminal,
                               Terminal::TextEditor&   editor,
                               ShellWriter&            output)
        : _terminal(terminal), _editor(editor), _output(output)
    {}

    // Command execution
    // ------------------------------------------------------------------------

    void CommandShell::run(const char* cmd, uint16_t len) {
        if (len == 0) return;
        dispatch(cmd, len);
    }

    // Extensibility
    // ------------------------------------------------------------------------

    void CommandShell::dispatch(const char* cmd, uint16_t len) {
        _builtin(cmd, len);
    }

    // Internals
    // ------------------------------------------------------------------------

    const CommandShell::Builtin* CommandShell::_builtins(uint8_t& count) {
        // Single source for dispatch AND `help`: name, one-line help, handler.
        static const Builtin TABLE[] = {
            { "help",       "list commands",                  &CommandShell::_cmd_help       },
            { "clear",      "clear the screen",               &CommandShell::_cmd_clear      },
            { "logs",       "[on|off]  live log output",      &CommandShell::_cmd_logs       },
            { "stats",      "scheduler / system metrics",     &CommandShell::_cmd_stats      },
            { "uptime",     "time since boot",                &CommandShell::_cmd_uptime     },
            { "plc",        "PLC link status",                &CommandShell::_cmd_plc        },
            { "alarms",     "[list|ack <code|all>] alarms",   &CommandShell::_cmd_alarms     },
            { "loglevel",   "[debug|info|..|critical] filter",&CommandShell::_cmd_loglevel   },
            { "version",    "firmware version",               &CommandShell::_cmd_version    },
            { "breakpoint", "trap into an attached debugger", &CommandShell::_cmd_breakpoint },
            { "reboot",     "reboot (needs 'reboot confirm')",&CommandShell::_cmd_reboot     },
            { "reset",      "reboot the controller now",      &CommandShell::_cmd_reset      },
            { "exit",       "quit (sim) / reboot (hw)",       &CommandShell::_cmd_exit       },
        };
        count = (uint8_t)(sizeof(TABLE) / sizeof(TABLE[0]));
        return TABLE;
    }

    void CommandShell::_builtin(const char* cmd, uint16_t len) {
        // First word = command name (leading spaces skipped); the rest = the arguments.
        const char* args = cmd; uint16_t alen = len, wlen;
        const char* name = Utils::arg_next(args, alen, wlen);
        if (name == nullptr) return;                            // blank / all-spaces line
        while (alen > 0 && *args == ' ') { args++; alen--; }    // trim to the argument slice

        uint8_t bcount; const Builtin* B = _builtins(bcount);
        for (uint8_t i = 0; i < bcount; i++)
            if (Utils::arg_eq(name, wlen, B[i].name)) { (this->*B[i].fn)(args, alen); return; }

        // Site commands, tried after the built-ins so a site can't shadow exit/reset.
        for (size_t i = 0; i < _command_count; i++) {
            const ShellCommand& c = _commands[i];
            if (c.name != nullptr && c.fn != nullptr && Utils::arg_eq(name, wlen, c.name)) {
                c.fn(_output, args, alen, _command_ctx);
                return;
            }
        }
        _unknown(cmd, len);
    }

    void CommandShell::_println(const char* msg) {
        _output.write(msg, strlen(msg));
    }

    void CommandShell::_cmd_help(const char*, uint16_t) {
        // One line per command (name + help), generated from the tables so the help can't
        // drift from the command set and a long name just pushes its help right (no
        // fixed-column truncation). %-10s is a minimum width, not a cap.
        char out[Config::LOG_FORMATTED_LEN];
        uint8_t bcount; const Builtin* B = _builtins(bcount);
        for (uint8_t i = 0; i < bcount; i++) {
            int w = snprintf(out, sizeof(out), "  %-10s %s", B[i].name, B[i].help);
            if (w > 0) _output.write(out, std::min((size_t)w, sizeof(out) - 1));
        }
        for (size_t i = 0; i < _command_count; i++) {
            const ShellCommand& c = _commands[i];
            if (c.name == nullptr) continue;
            int w = snprintf(out, sizeof(out), "  %-10s %s", c.name, c.help ? c.help : "");
            if (w > 0) _output.write(out, std::min((size_t)w, sizeof(out) - 1));
        }
    }

    void CommandShell::_cmd_clear(const char*, uint16_t) { _output.clear(); }

    void CommandShell::_cmd_logs(const char* args, uint16_t alen) {
        uint16_t wlen; const char* w = Utils::arg_next(args, alen, wlen);
        if (w == nullptr)
            _println(_output.logs_enabled() ? "logs: output enabled" : "logs: output disabled");
        else if (Utils::arg_eq(w, wlen, "on"))  { _output.set_logs_enabled(true);  _println("logs: output enabled"); }
        else if (Utils::arg_eq(w, wlen, "off")) { _output.set_logs_enabled(false); _println("logs: output disabled"); }
        else _println("usage: logs [on|off]");
    }

    void CommandShell::_cmd_breakpoint(const char*, uint16_t) { HAL::debug_break(); }

    void CommandShell::_cmd_reset(const char*, uint16_t) { HAL::reset(); }    // reboots; does not return

    void CommandShell::_cmd_exit(const char*, uint16_t) { HAL::shutdown(); }  // sim: exit(0); OPTA: reboots

    void CommandShell::_cmd_stats(const char*, uint16_t) {
        // Emits several lines; each is banked and drained at LOG_ROWS_PER_TICK by the
        // scrollback, so the full block appears over a few ticks (bounded, never a flood).
        Scheduler::SchedulerContext* ctx = _terminal.context().scheduler;
        if (ctx == nullptr) { _println("stats: no scheduler context"); return; }

        char out[Config::LOG_FORMATTED_LEN];
        char val[24], detail[64];
        char fa[12], fb[12];
        int w;

        snprintf(val,    sizeof(val),    "%lus", (unsigned long)(HAL::uptime_ms() / 1000));
        snprintf(detail, sizeof(detail), "%lu ticks, %lu overruns",
            (unsigned long)ctx->tick_count, (unsigned long)ctx->overruns);
        w = snprintf(out, sizeof(out), "%-29s : %-8s (%s)",
            "Uptime ......................", val, detail);
        if (w > 0) _output.write(out, (size_t)w);

        snprintf(val,    sizeof(val),    "%s%%",
            Utils::format_float(fa, sizeof(fa), ctx->cpu_load_ema * 100.0f, 1));
        snprintf(detail, sizeof(detail), "recent %sms, peak %sms",
            Utils::format_float(fa, sizeof(fa), ctx->cpu_loop_us_recent / 1000.0, 2),
            Utils::format_float(fb, sizeof(fb), ctx->cpu_loop_us_max / 1000.0, 2));
        w = snprintf(out, sizeof(out), "%-29s : %-8s (%s)",
            "CPU load ....................", val, detail);
        if (w > 0) _output.write(out, (size_t)w);

        snprintf(val,    sizeof(val),    "%s%%",
            Utils::format_float(fa, sizeof(fa), ctx->cpu_jitter_ema * 100.0f, 1));
        snprintf(detail, sizeof(detail), "recent %sms, peak %sms",
            Utils::format_float(fa, sizeof(fa), ctx->cpu_jitter_us_recent / 1000.0, 2),
            Utils::format_float(fb, sizeof(fb), ctx->cpu_jitter_us_max / 1000.0, 2));
        w = snprintf(out, sizeof(out), "%-29s : %-8s (%s)",
            "CPU jitter ..................", val, detail);
        if (w > 0) _output.write(out, (size_t)w);

        snprintf(val,    sizeof(val),    "%s%%",
            Utils::format_float(fa, sizeof(fa), ctx->stack_usage * 100.0f, 1));
        snprintf(detail, sizeof(detail), "%lu bytes free",
            (unsigned long)ctx->stack_bytes_free);
        w = snprintf(out, sizeof(out), "%-29s : %-8s (%s)",
            "Stack usage .................", val, detail);
        if (w > 0) _output.write(out, (size_t)w);

        snprintf(val, sizeof(val), "%lu bytes", (unsigned long)ctx->stack_depth);
        w = snprintf(out, sizeof(out), "%-29s : %s",
            "Stack depth .................", val);
        if (w > 0) _output.write(out, (size_t)w);

        snprintf(detail, sizeof(detail), "%lu TX dropped, %lu RX overruns",
            (unsigned long)_terminal.tx_dropped(), (unsigned long)_terminal.rx_overrun());
        w = snprintf(out, sizeof(out), "%-29s : %s",
            "Console serial ..............", detail);
        if (w > 0) _output.write(out, (size_t)w);
    }

    void CommandShell::_cmd_version(const char*, uint16_t) {
        char out[Config::LOG_FORMATTED_LEN];
        int n = snprintf(out, sizeof(out), "Botanik %s on %s",
                         Config::FIRMWARE_VERSION, HAL::hardware_description());
        if (n > 0) _output.write(out, std::min((size_t)n, sizeof(out) - 1));
    }

    void CommandShell::_cmd_alarms(const char* args, uint16_t alen) {
        uint16_t wlen; const char* sub = Utils::arg_next(args, alen, wlen);
        char out[80];

        // "alarms ack <code|all>" - mutates, so it needs the model wired via set_diagnostics.
        if (sub && Utils::arg_eq(sub, wlen, "ack")) {
            if (_alarm_model == nullptr) { _println("alarms: model unavailable"); return; }
            uint16_t tlen; const char* tgt = Utils::arg_next(args, alen, tlen);
            if (tgt == nullptr) { _println("usage: alarms ack <code|all>"); return; }
            int n;
            if (Utils::arg_eq(tgt, tlen, "all")) {
                n = snprintf(out, sizeof(out), "acknowledged %u active alarm(s)", _alarm_model->ack_all());
            } else {
                char code[24]; uint16_t cl = std::min(tlen, (uint16_t)(sizeof(code) - 1));
                memcpy(code, tgt, cl); code[cl] = '\0';
                n = snprintf(out, sizeof(out), _alarm_model->ack(code)
                             ? "acknowledged %s" : "no active alarm '%s'", code);
            }
            if (n > 0) _output.write(out, std::min((size_t)n, sizeof(out) - 1));
            return;
        }

        const Alarms::AlarmModel* m = _terminal.alarms();
        if (m == nullptr) { _println("alarms: model unavailable"); return; }

        // "alarms list" - one paced line per active condition: code, age, masked/ack flags.
        if (sub && Utils::arg_eq(sub, wlen, "list")) {
            if (m->active_count() == 0) { _println("no active alarms"); return; }
            char age[16];
            uint64_t now = HAL::uptime_ms();
            for (uint8_t i = 0; i < m->count(); i++) {
                const Alarms::AlarmCondition& c = m->condition(i);
                if (!c.active) continue;
                uint32_t secs = (uint32_t)((now - c.started_ms) / 1000);
                int n = snprintf(out, sizeof(out), "  %-10s %s%s%s", c.code ? c.code : "?",
                    Utils::format_age(age, sizeof(age), secs, false),
                    c.unmasked ? "" : " [masked]", c.acknowledged ? " [ack]" : "");
                if (n > 0) _output.write(out, std::min((size_t)n, sizeof(out) - 1));
            }
            return;
        }

        // Default: the count summary.
        int n = snprintf(out, sizeof(out), "alarms: %u active, %u unmasked",
                         m->active_count(), m->unmasked_count());
        if (n > 0) _output.write(out, std::min((size_t)n, sizeof(out) - 1));
    }

    void CommandShell::_cmd_loglevel(const char* args, uint16_t alen) {
        if (_logger == nullptr) { _println("loglevel: logger unavailable"); return; }
        static const struct { const char* name; PlcIO::LogLevel lvl; } LEVELS[] = {
            { "debug",  PlcIO::LOG_DEBUG   }, { "info",    PlcIO::LOG_INFO    },
            { "notice", PlcIO::LOG_NOTICE  }, { "warning", PlcIO::LOG_WARNING },
            { "error",  PlcIO::LOG_ERROR   }, { "critical",PlcIO::LOG_CRITICAL},
        };
        char out[48];
        uint16_t wlen; const char* w = Utils::arg_next(args, alen, wlen);
        if (w == nullptr) {                                   // report the current threshold
            const char* cur = "?";
            for (const auto& L : LEVELS) if (L.lvl == _logger->min_level()) cur = L.name;
            int n = snprintf(out, sizeof(out), "loglevel: %s", cur);
            if (n > 0) _output.write(out, std::min((size_t)n, sizeof(out) - 1));
            return;
        }
        for (const auto& L : LEVELS)
            if (Utils::arg_eq(w, wlen, L.name)) {
                _logger->set_min_level(L.lvl);
                int n = snprintf(out, sizeof(out), "loglevel: %s", L.name);
                if (n > 0) _output.write(out, std::min((size_t)n, sizeof(out) - 1));
                return;
            }
        _println("usage: loglevel [debug|info|notice|warning|error|critical]");
    }

    void CommandShell::_cmd_reboot(const char* args, uint16_t alen) {
        uint16_t wlen; const char* w = Utils::arg_next(args, alen, wlen);
        if (w != nullptr && Utils::arg_eq(w, wlen, "confirm")) {
            _println("rebooting...");
            HAL::reset();                                     // does not return
        }
        _println("this reboots the controller; run 'reboot confirm' to proceed");
    }

    void CommandShell::_cmd_uptime(const char*, uint16_t) {
        char buf[16], out[48];
        uint32_t secs = (uint32_t)(HAL::uptime_ms() / 1000);
        int n = snprintf(out, sizeof(out), "uptime: %s",
                         Utils::format_age(buf, sizeof(buf), secs, false));
        if (n > 0) _output.write(out, std::min((size_t)n, sizeof(out) - 1));
    }

    void CommandShell::_cmd_plc(const char*, uint16_t) {
        const PlcIO::PlcSyncContext* p = _terminal.context().plc;
        if (p == nullptr) { _println("plc: no PLC context"); return; }
        const char* s;
        switch (p->status()) {
            case PlcIO::PLC_UNKNOWN: s = "no contact yet"; break;
            case PlcIO::PLC_RUNNING: s = "running";        break;
            case PlcIO::PLC_TIMEOUT: s = "TIMEOUT";        break;
            case PlcIO::PLC_FAULT:   s = "FAULT";          break;
            default:                 s = "?";              break;
        }
        char out[64];
        int n = snprintf(out, sizeof(out), "plc link: %s%s", s, p->faulted() ? " (faulted)" : "");
        if (n > 0) _output.write(out, std::min((size_t)n, sizeof(out) - 1));
    }

    void CommandShell::_unknown(const char* cmd, uint16_t len) {
        // "Unknown command: " + the offending input, handed to write_lines() as a
        // single blob: it wraps at the terminal width and splits on any embedded
        // newline, so no \n reaches write_chars and no manual bookkeeping is needed.
        char out[Config::LOG_FORMATTED_LEN];
        static const char prefix[] = "Unknown command: ";
        uint16_t plen = (uint16_t)(sizeof(prefix) - 1);
        uint16_t clen = std::min(len, (uint16_t)(sizeof(out) - plen));
        memcpy(out, prefix, plen);
        memcpy(out + plen, cmd, clen);
        _output.write_lines(out, (uint16_t)(plen + clen));
    }

}}
