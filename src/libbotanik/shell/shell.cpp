/**
 * @file shell.cpp
 * @brief CommandShell implementation - command dispatch and built-in commands.
 */
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
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

    void CommandShell::_builtin(const char* cmd, uint16_t len) {
        // name -> handler table: keeps the command list (and its widths, via
        // strlen) in one place, with no magic length literals to drift.
        struct Builtin { const char* name; void (CommandShell::*fn)(); };
        static const Builtin BUILTINS[] = {
            { "help",       &CommandShell::_cmd_help       },
            { "clear",      &CommandShell::_cmd_clear      },
            { "logs",       &CommandShell::_cmd_logs       },
            { "logs on",    &CommandShell::_cmd_logs_on    },
            { "logs off",   &CommandShell::_cmd_logs_off   },
            { "stats",      &CommandShell::_cmd_stats      },
            { "breakpoint", &CommandShell::_cmd_breakpoint },
            { "reset",      &CommandShell::_cmd_reset      },
            { "exit",       &CommandShell::_cmd_exit       },
        };
        for (const Builtin& b : BUILTINS) {
            uint16_t blen = (uint16_t)strlen(b.name);
            if (len == blen && strncmp(cmd, b.name, blen) == 0) { (this->*b.fn)(); return; }
        }
        _unknown(cmd, len);
    }

    void CommandShell::_println(const char* msg) {
        _output.write(msg, strlen(msg));
    }

    void CommandShell::_cmd_help() {
        _println("Commands: help, clear, logs [on/off], stats, breakpoint, reset, exit");
    }

    void CommandShell::_cmd_clear() { _output.clear(); }

    void CommandShell::_cmd_logs() {
        _println(_output.logs_enabled() ? "logs: output enabled"
                                        : "logs: output disabled");
    }

    void CommandShell::_cmd_logs_on() {
        _output.set_logs_enabled(true);
        _println("logs: output enabled");
    }

    void CommandShell::_cmd_logs_off() {
        _output.set_logs_enabled(false);
        _println("logs: output disabled");
    }

    void CommandShell::_cmd_breakpoint() { HAL::debug_break(); }

    void CommandShell::_cmd_reset() { HAL::reset(); }        // reboots; does not return

    void CommandShell::_cmd_exit() { HAL::shutdown(); }      // sim: exit(0); OPTA: reboots

    void CommandShell::_cmd_stats() {
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
            Utils::format_float(fa, sizeof(fa), ctx->stack_usage_ema * 100.0f, 1));
        snprintf(detail, sizeof(detail), "recent %lu, peak %lu bytes",
            (unsigned long)ctx->stack_bytes_recent,
            (unsigned long)ctx->stack_bytes_max);
        w = snprintf(out, sizeof(out), "%-29s : %-8s (%s)",
            "Stack usage .................", val, detail);
        if (w > 0) _output.write(out, (size_t)w);

        snprintf(val, sizeof(val), "%lu bytes", (unsigned long)ctx->stack_depth);
        w = snprintf(out, sizeof(out), "%-29s : %s",
            "Stack depth .................", val);
        if (w > 0) _output.write(out, (size_t)w);
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
