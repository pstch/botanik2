/**
 * @file shell.cpp
 * @brief CommandShell implementation — command dispatch and built-in commands.
 */
#include <algorithm>
#include <cstdio>
#include <cstring>
#ifdef TARGET_SIMULATOR
#include <csignal>
#endif

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/shell/shell.h"

namespace Botanik { namespace Shell {

    CommandShell::CommandShell(Terminal::AnsiTerminal& terminal,
                               Terminal::TextEditor&   editor,
                               ShellWriter&            output)
        : _terminal(terminal), _editor(editor), _output(output)
    {}

    void CommandShell::run(const char* cmd, uint16_t len) {
        if (len == 0) return;
        dispatch(cmd, len);
    }

    void CommandShell::dispatch(const char* cmd, uint16_t len) {
        _builtin(cmd, len);
    }

    void CommandShell::_builtin(const char* cmd, uint16_t len) {
        char out[Config::LOG_FORMATTED_LEN];
        int  n = 0;

        if (len == 4 && strncmp(cmd, "help", 4) == 0) {
            n = snprintf(out, sizeof(out), "Commands: help, clear, stats, breakpoint");
        } else if (len == 5 && strncmp(cmd, "clear", 5) == 0) {
            _output.clear();
            return;
        } else if (len == 10 && strncmp(cmd, "breakpoint", 10) == 0) {
#ifdef TARGET_SIMULATOR
            raise(SIGTRAP);
#else
            __BKPT(0);
#endif
            return;
        } else if (len == 5 && strncmp(cmd, "stats", 5) == 0) {
            Scheduler::SchedulerContext* ctx = _terminal.sched_ctx();
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
                    (double)(ctx->perf.cpu_load_ema * 100.0f));
                snprintf(detail, sizeof(detail), "recent %.2fms, peak %.2fms",
                    (double)ctx->perf.cpu_loop_us_recent / 1000.0,
                    (double)ctx->perf.cpu_loop_us_max / 1000.0);
                w = snprintf(out, sizeof(out), "%-29s : %-8s (%s)",
                    "CPU load ....................", val, detail);
                if (w > 0) _output.write(out, (size_t)w);

                snprintf(val,    sizeof(val),    "%.1f%%",
                    (double)(ctx->perf.jitter_ema * 100.0f));
                snprintf(detail, sizeof(detail), "recent %.2fms, peak %.2fms",
                    (double)ctx->perf.jitter_us_recent / 1000.0,
                    (double)ctx->perf.jitter_us_max / 1000.0);
                w = snprintf(out, sizeof(out), "%-29s : %-8s (%s)",
                    "CPU jitter ..................", val, detail);
                if (w > 0) _output.write(out, (size_t)w);

                snprintf(val,    sizeof(val),    "%.1f%%",
                    (double)(ctx->perf.stack_usage_ema * 100.0f));
                snprintf(detail, sizeof(detail), "recent %lu, peak %lu bytes",
                    (unsigned long)ctx->perf.stack_bytes_recent,
                    (unsigned long)ctx->perf.stack_bytes_max);
                w = snprintf(out, sizeof(out), "%-29s : %-8s (%s)",
                    "Stack usage .................", val, detail);
                if (w > 0) _output.write(out, (size_t)w);

                snprintf(val, sizeof(val), "%lu bytes",
                    (unsigned long)ctx->perf.stack_depth);
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
