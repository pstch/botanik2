/**
 * @file shell.cpp
 * @brief CommandShell implementation — command dispatch and built-in commands.
 */
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/shell/shell.h"

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
        char out[Config::LOG_FORMATTED_LEN];
        int  n = 0;

        if (len == 4 && strncmp(cmd, "help", 4) == 0) {
            n = snprintf(out, sizeof(out), "Commands: help, clear, stats, breakpoint, reset");
        } else if (len == 5 && strncmp(cmd, "clear", 5) == 0) {
            _output.clear();
            return;
        } else if (len == 10 && strncmp(cmd, "breakpoint", 10) == 0) {
            HAL::debug_break();
            return;
        } else if (len == 5 && strncmp(cmd, "reset", 5) == 0) {
            HAL::reset();  // reboots the firmware; does not return
            return;
        } else if (len == 5 && strncmp(cmd, "stats", 5) == 0) {
            Scheduler::SchedulerContext* ctx = _terminal.context().scheduler;
            uint32_t uptime_s = (uint32_t)(HAL::uptime_ms() / 1000);
            if (ctx) {
                char val[24], detail[64];
                int w;
                snprintf(val,    sizeof(val),    "%lus", (unsigned long)uptime_s);
                snprintf(detail, sizeof(detail), "%lu ticks, %lu overruns",
                    (unsigned long)ctx->tick_count, (unsigned long)ctx->overruns);
                w = snprintf(out, sizeof(out), "%-29s : %-8s (%s)",
                    "Uptime ......................", val, detail);
                if (w > 0) _output.write(out, (size_t)w);

                snprintf(val,    sizeof(val),    "%.1f%%",
                    (double)(ctx->cpu_load_ema * 100.0f));
                snprintf(detail, sizeof(detail), "recent %.2fms, peak %.2fms",
                    (double)ctx->cpu_loop_us_recent / 1000.0,
                    (double)ctx->cpu_loop_us_max / 1000.0);
                w = snprintf(out, sizeof(out), "%-29s : %-8s (%s)",
                    "CPU load ....................", val, detail);
                if (w > 0) _output.write(out, (size_t)w);

                snprintf(val,    sizeof(val),    "%.1f%%",
                    (double)(ctx->cpu_jitter_ema * 100.0f));
                snprintf(detail, sizeof(detail), "recent %.2fms, peak %.2fms",
                    (double)ctx->cpu_jitter_us_recent / 1000.0,
                    (double)ctx->cpu_jitter_us_max / 1000.0);
                w = snprintf(out, sizeof(out), "%-29s : %-8s (%s)",
                    "CPU jitter ..................", val, detail);
                if (w > 0) _output.write(out, (size_t)w);

                snprintf(val,    sizeof(val),    "%.1f%%",
                    (double)(ctx->stack_usage_ema * 100.0f));
                snprintf(detail, sizeof(detail), "recent %lu, peak %lu bytes",
                    (unsigned long)ctx->stack_bytes_recent,
                    (unsigned long)ctx->stack_bytes_max);
                w = snprintf(out, sizeof(out), "%-29s : %-8s (%s)",
                    "Stack usage .................", val, detail);
                if (w > 0) _output.write(out, (size_t)w);

                snprintf(val, sizeof(val), "%lu bytes",
                    (unsigned long)ctx->stack_depth);
                w = snprintf(out, sizeof(out), "%-29s : %s",
                    "Stack depth .................", val);
                if (w > 0) _output.write(out, (size_t)w);
                return;
            } else {
                n = snprintf(out, sizeof(out), "stats: no scheduler context");
            }
        } else {
            // "Unknown command: " + the offending input, handed to write_lines()
            // as a single blob: it wraps at the terminal width and splits on any
            // embedded newline, so no \n reaches write_chars and no manual
            // first-line/remainder bookkeeping is needed.
            static const char prefix[] = "Unknown command: ";
            uint16_t plen = (uint16_t)(sizeof(prefix) - 1);
            uint16_t clen = std::min(len, (uint16_t)(sizeof(out) - plen));
            memcpy(out, prefix, plen);
            memcpy(out + plen, cmd, clen);
            _output.write_lines(out, (uint16_t)(plen + clen));
            return;
        }

        if (n > 0)
            _output.write(out, (size_t)n);
    }

}}
