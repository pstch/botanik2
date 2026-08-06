// REVIEW_35: TODO
/**
 * @file shell.cpp
 * @brief CommandShell implementation - command dispatch and built-in commands.
 */
#include "libbotanik/shell/shell.h"   // CommandShell - what this file implements

#include <algorithm>                  // std::min / std::find - output clamping, history search
#include <cstdarg>                    // va_list / va_start / va_end - the printf-style reply helper
#include <cstdio>                     // snprintf / vsnprintf - formatting every command's output
#include <cstring>                    // strlen / strncmp / memcpy / memcmp - argument parsing

#include "libbotanik_config.h"        // Config::* - the values `version` and `stats` report
#include "libbotanik_features.h"      // BOTANIK_FEATURE_LIST - the `features` command expands it

#include "libbotanik/alarms/alarms.h" // Alarms::AlarmController - the `alarms` command
#include "libbotanik/hal/platform.h"  // HAL::System / Debug / Stack - `reset`, `breakpoint`, `stats`
#include "libbotanik/hal/storage.h"   // HAL::Storage::get / set - the persisted shell settings
#include "libbotanik/logs/logger.h"   // Logs::Logger::set_min_level - the `loglevel` command
#include "libbotanik/network/mqtt.h"  // Network::MqttClient - the `net` command's broker line
#include "libbotanik/network/network.h" // Network::NetworkHandler - the `net` command's link line
#include "libbotanik/network/ntp.h"   // Network::NtpClient - `net` and `ntpsync`
#include "libbotanik/scheduler/scheduler.h" // Scheduler::SchedulerContext - the `stats` command
#include "libbotanik/utils/text.h"    // Utils::format_age / format_float / format_size - `stats`
#include "libbotanik/vars/params.h"   // Vars::Params - `list`, `get`, `set`, `override`

// Build-time git short hash: a generated header on the CMake builds (see
// CMakeLists), "unknown" on the Arduino .ino deploy (no CMake, so the header
// isn't generated).
#if defined(__has_include) && __has_include("botanik_gitversion.h")
#  include "botanik_gitversion.h"     // BOTANIK_GIT_HASH - what `version` prints; absent in the .ino build
#endif
#ifndef BOTANIK_GIT_HASH
#  define BOTANIK_GIT_HASH "unknown"
#endif

namespace Botanik { namespace Shell {

    namespace {
    /** @brief Extract the next whitespace-delimited word from [@p args, @p
     *  args+alen). Returns the word start (nullptr if none) and sets @p wlen to
     *  its length, advancing @p args / @p alen past the word (leading spaces
     *  are skipped first). */
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

    CommandShell::CommandShell(Terminal::Console&      console,
                               Terminal::TextEditor&   editor,
                               ShellWriter&            output)
        : _console(console), _terminal(console.terminal()), _editor(editor), _output(output)
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
            { "net",        "network / NTP / MQTT status",     &CommandShell::_cmd_net        },
            { "ntpsync",    "force an NTP resync now",         &CommandShell::_cmd_ntpsync    },
            { "alarms",     "[list|ack <code|all>] alarms",   &CommandShell::_cmd_alarms     },
            { "list",       "[<prefix>] list variables",      &CommandShell::_cmd_list       },
            { "get",        "<name>  read a variable",        &CommandShell::_cmd_get        },
            { "set",        "<name> <value>  edit a parameter", &CommandShell::_cmd_set       },
            { "override",   "list|clear|<name> [off|<value>] [valid|invalid]", &CommandShell::_cmd_override },
            { "control",    "[enable|disable] drive the field", &CommandShell::_cmd_control   },
            { "loglevel",   "[debug|info|..|critical] filter",&CommandShell::_cmd_loglevel   },
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

        // Site commands first, so the table can override a built-in (e.g. a
        // site that wants its own `stats`); unmatched names fall through to the
        // built-ins.
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
        // One line per command, generated from the tables so help can't drift
        // from the command set. %-10s is a minimum width, not a cap (a long
        // name pushes help right).
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
        // The system's hold, not the operator's: this is the firmware deciding
        // to stop running, not someone turning control off.
        if (Vars::Bank* io = _bank()) io->disable_control(Vars::Bank::INHIBIT_HEALTH);
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
        Scheduler::SchedulerContext* ctx = _console.context().scheduler;
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

    void CommandShell::_cmd_version(const char*, uint16_t) {
        char out[Config::LOG_FORMATTED_LEN];
        _emitf(out, sizeof(out), "Botanik %s (%s) site %s on %s",
               Config::FIRMWARE_VERSION, BOTANIK_GIT_HASH,
               Config::SITE_NAME, HAL::hardware_description());
    }
    void CommandShell::_cmd_features(const char*, uint16_t) {
        // What this binary IS, not anything it can be told to become: the flags
        // are compile-time, so there is no argument form and nothing to set. It
        // answers the question `version` cannot - two units on the same firmware
        // version can differ here, and an absent subsystem is otherwise
        // indistinguishable from a broken one (no `net` output because MQTT was
        // compiled out reads exactly like a broker that will not connect).
        // Expanded from BOTANIK_FEATURE_LIST, so this is not a second roster
        // to keep in step - a flag added to the header is reported here without
        // anyone remembering to come back. Names carry the macro spelling and
        // are lower-cased for display below.
#define BOTANIK_FEAT_ROW(f) { #f, BOTANIK_FEATURE_##f != 0 },
        static const struct { const char* name; bool on; } FEATURES[] = {
            BOTANIK_FEATURE_LIST(BOTANIK_FEAT_ROW)
        };
#undef BOTANIK_FEAT_ROW

        // Partitioned rather than one line per flag: two dozen booleans is a
        // glance, and every line costs the same 4800-baud budget the rest of the
        // UI is fighting over. Wrapped rather than truncated - two dozen names
        // do not fit 80 columns, and a silently clipped list is worse than no
        // list, since the flag you cannot see reads as absent.
        // The shell has no terminal handle, so this is the conventional 80
        // columns minus the label, not a measured width. Wrapping short is
        // harmless; wrapping long would clip.
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
                _println("usage: alarms ack <code|all>"); return;
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

        const Alarms::AlarmController* m = _console.alarms();
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
                _emitf(out, sizeof(out), "  %-10s %s%s",
                    c.name ? c.name : "?",
                    Utils::format_age(age, sizeof(age), secs, false),
                    c.user_ack ? " [ack]" : "");
            }
            return;
        }

        // Default: the count summary.
        _emitf(out, sizeof(out), "alarms: %u active", m->active_count());
    }

    // Render one variable's row, shared by `get` and `list` so a name shown by
    // one is formatted identically by the other.
    void CommandShell::_emit_var(const Vars::Bank& io, Vars::Var h) {
        char out[128], val[48];
        const char* s = Vars::var_format(io, h, val, sizeof(val));
        const char* u = h.meta()->unit;
        // Flag the three things a bare number hides: that a row is a writable
        // parameter, that it is being forced by hand, and that a reading is
        // really its default because nothing has written it (the trap - it looks
        // like a perfectly reasonable measurement).
        _emitf(out, sizeof(out), "  %-20s %-12s %-4s %s%s%s", h.meta()->name, s,
               (u && u[0]) ? u : "",
               h.role() == Vars::ROLE_PARAM ? "(param)" : "",
               h.overridden() ? " OVERRIDDEN" : "",
               h.valid() ? "" : " (default: nothing has written it)");
    }

    // The second line `get` prints for a forced variable: what it is being held
    // at, and what it would read without the override. `list` deliberately does
    // not - one row per variable is the point of it - but a `get` on a forced
    // row is almost always someone deciding whether to release it, and "has the
    // sensor come back?" is the question they have no other way to ask.
    void CommandShell::_emit_override(Vars::Var h) {
        if (!h.overridden()) return;
        const Vars::Variable* d = h.meta();
        char out[128], fb[48], lb[48];
        const char* forced = Vars::format_scalar(d->type, h.raw(),  d->digits,
                                                 d->str, fb, sizeof(fb));
        const char* live   = Vars::format_scalar(d->type, h.live(), d->digits,
                                                 d->str, lb, sizeof(lb));
        _emitf(out, sizeof(out), "    forced %s (%s)   live %s (%s)",
               forced, h.valid()      ? "valid" : "invalid",
               live,   h.live_valid() ? "valid" : "invalid");
    }

    void CommandShell::_cmd_get(const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        if (io == nullptr) { _println("get: no variables"); return; }
        uint16_t wn = 0;
        const char* nm = arg_next(args, alen, wn);
        if (nm == nullptr) { _println("usage: get <name>   (list [<prefix>] to browse)"); return; }

        const Vars::VarRef r = io->find(nm, wn);
        if (r.index == Vars::VAR_NONE) { _println("get: unknown variable"); return; }
        const Vars::Var h = io->var(r);
        _emit_var(*io, h);
        _emit_override(h);
    }

    void CommandShell::_cmd_list(const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        if (io == nullptr) { _println("list: no variables"); return; }
        uint16_t wp = 0;
        const char* prefix = arg_next(args, alen, wp);

        // One table, so one loop. A prefix is how you actually navigate a few
        // hundred variables: they are named by domain (`pwr_`, `env_`), so
        // `list pwr_` is the subsystem view without needing a grouping concept
        // in the command.
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

    void CommandShell::_cmd_set(const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        if (io == nullptr) { _println("set: no variables"); return; }
        uint16_t wn; const char* nm = arg_next(args, alen, wn);
        uint16_t wv; const char* v  = arg_next(args, alen, wv);
        if (nm == nullptr || v == nullptr) {
            _println("usage: set <parameter> <value>"); return;
        }
        char out[128];
        const Vars::VarRef r = io->find(nm, wn);
        if (r.index == Vars::VAR_NONE) { _println("set: unknown variable"); return; }
        Vars::Var p = io->var(r);
        // Point at `override` rather than failing flatly: writing a sensor or an
        // output here is a reasonable thing to WANT, it is just not what `set`
        // does - the next scan would put the value straight back.
        if (p.role() != Vars::ROLE_PARAM) {
            _emitf(out, sizeof(out),
                   "set: %s is not a parameter (override %s <value> to force it)",
                   p.meta()->name, p.meta()->name);
            return;
        }

        char val[96];
        uint16_t vc = std::min(wv, (uint16_t)(sizeof(val) - 1));
        memcpy(val, v, vc); val[vc] = '\0';

        switch (io->set(p, val)) {
            case Vars::SET_OK:
                _emitf(out, sizeof(out), "%s = %s", p.meta()->name, val);
                break;
            case Vars::SET_NOT_PERSISTED:
                // Live, but it dies at the next reboot - say which of the two
                // failed, since the fixes are entirely different.
                _emitf(out, sizeof(out), "%s = %s  (NOT SAVED: storage refused it)",
                       p.meta()->name, val);
                break;
            default:
                _emitf(out, sizeof(out), "set: invalid value for %s",
                       p.meta()->name);
                break;
        }
    }

    void CommandShell::_cmd_override(const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        if (io == nullptr) { _println("override: no variables"); return; }

        char out[128];
        uint16_t wn = 0;
        const char* nm = arg_next(args, alen, wn);

        // `override list` (or a bare `override`): what is forced right now. An
        // override outlives the session that set it and explains behaviour
        // nothing else accounts for, so it must be one command away, not
        // something you have to remember to look for.
        if (nm == nullptr || arg_eq(nm, wn, "list")) {
            char val[48];
            uint16_t n = 0;
            for (uint16_t i = 0; i < io->count(); i++) {
                Vars::Var h = io->var(i);
                if (!h.overridden()) continue;
                const char* s = Vars::var_format(*io, h, val, sizeof(val));
                _emitf(out, sizeof(out), "  %-20s %-12s %s",
                       h.meta()->name, s, h.valid() ? "" : "(reading as invalid)");
                n++;
            }
            if (n == 0) _println("override: none active");
            else _emitf(out, sizeof(out),
                        "%u override(s) active - these SURVIVE a reboot; "
                        "`override clear` clears them", n);
            return;
        }

        // "override clear" releases the lot, so a maintainer can hand the
        // machine back without walking the list. "off" is the older spelling and
        // still works - it is what `override <name> off` says, and there is no
        // sense in the two forms disagreeing.
        if (arg_eq(nm, wn, "clear") || arg_eq(nm, wn, "off")) {
            _emitf(out, sizeof(out), "override: cleared %u",
                   io->clear_overrides());
            return;
        }

        const Vars::VarRef r = io->find(nm, wn);
        if (r.index == Vars::VAR_NONE) { _println("override: unknown variable"); return; }
        Vars::Var h = io->var(r);

        uint16_t wa = 0;
        const char* a = arg_next(args, alen, wa);

        if (a != nullptr && arg_eq(a, wa, "off")) {
            // Through the bank, not the handle: the handle cannot reach the
            // store, and a release that left the stored copy behind would come
            // back at the next boot.
            io->override_clear(h);
            _emitf(out, sizeof(out), "%s: override off", h.meta()->name);
            return;
        }
        // "on" is optional sugar: "override x 20" and "override x on 20" are
        // the same thing, and a bare name freezes.
        if (a != nullptr && arg_eq(a, wa, "on")) a = arg_next(args, alen, wa);

        char val[48];
        // A trailing `valid` / `invalid` says what the forced value READS as.
        // "invalid" is how you stand a variable up as a dead sensor without
        // touching what it reports, which is the only way to exercise the code
        // that watches for one. Accepted either side of the value, since
        // `override x invalid` and `override x 20 invalid` both read naturally.
        bool valid = true;
        auto take_validity = [&](const char* w, uint16_t wl) {
            if (w == nullptr) return false;
            if (!arg_eq(w, wl, "valid") && !arg_eq(w, wl, "invalid")) return false;
            valid = arg_eq(w, wl, "valid");
            return true;
        };

        if (take_validity(a, wa)) a = arg_next(args, alen, wa);   // may be null

        if (a == nullptr) {
            // No value: hold it where it is. The quality is the SAME default as
            // the value form - good unless `invalid` was asked for. Freezing
            // used to carry the current quality through, so freezing a variable
            // nothing had written yet handed back an override reading as dead,
            // which is not what anyone typing a bare `override` means. Through
            // the bank, not the handle: the handle cannot reach the store.
            io->override_set(h, h.raw(), valid);
            const char* s = Vars::var_format(*io, h, val, sizeof(val));
            _emitf(out, sizeof(out), "%s: frozen at %s%s", h.meta()->name, s,
                   h.valid() ? "" : " (reading as invalid)");
            return;
        }

        char text[64];
        uint16_t vc = std::min(wa, (uint16_t)(sizeof(text) - 1));
        memcpy(text, a, vc); text[vc] = '\0';

        // Sequenced deliberately: arg_next() WRITES wt, and passing both to one
        // call left the read of wt unordered against it - which is how the word
        // after the value stopped being seen.
        uint16_t wt = 0;
        const char* t = arg_next(args, alen, wt);
        (void)take_validity(t, wt);

        if (io->override_set(h, text, valid) != Vars::SET_OK) {
            _emitf(out, sizeof(out), "override: invalid value for %s",
                   h.meta()->name);
            return;
        }
        const char* s = Vars::var_format(*io, h, val, sizeof(val));
        _emitf(out, sizeof(out), "%s: forced to %s%s", h.meta()->name, s,
               valid ? "" : " (reading as invalid)");
    }

    void CommandShell::_cmd_control(const char* args, uint16_t alen) {
        Vars::Bank* io = _bank();
        if (io == nullptr) { _println("control: no variables"); return; }
        uint16_t w = 0;
        const char* a = arg_next(args, alen, w);

        if (a == nullptr) { _report_control(io); return; }

        if (arg_eq(a, w, "enable")) {
            io->enable_control();          // releases the operator's hold only
            _report_control(io);           // ...which may not be the only one
            return;
        }
        if (arg_eq(a, w, "disable")) {
            // Says what it actually did, and what is still possible: the
            // programs have stopped deciding, but `override` still reaches an
            // actuator - which is the whole reason to type this before working
            // on the plant.
            io->disable_control();
            _println("control: DISABLED - outputs at their safe state, "
                     "scan inhibited; `override` still drives them by hand");
            return;
        }
        _println("usage: control [enable|disable]");
    }

    // Two independent holds, so "why is it still off?" has to be answerable -
    // otherwise `control enable` that changes nothing looks like a broken
    // command rather than a health fault still in force.
    void CommandShell::_report_control(Vars::Bank* io) {
        if (io->control_enabled()) { _println("control: enabled"); return; }
        const bool op  = io->control_inhibited_by(Vars::Bank::INHIBIT_OPERATOR);
        const bool sys = io->control_inhibited_by(Vars::Bank::INHIBIT_HEALTH);
        char out[128];
        _emitf(out, sizeof(out), "control: DISABLED by %s%s%s",
               op  ? "the operator" : "",
               (op && sys) ? " and " : "",
               sys ? "the system (bring-up, health, or a pending reset)" : "");
    }

    void CommandShell::_cmd_loglevel(const char* args, uint16_t alen) {
#if !BOTANIK_FEATURE_LOGGING
        // The Logger object still exists (an inert shell), so a null check would
        // not catch this and the command would silently appear to work.
        (void)args; (void)alen;
        _println("loglevel: logging is compiled out (see `features`)");
        return;
#else
        if (_logger == nullptr) {
            _println("loglevel: logger unavailable"); return;
        }
        static const struct { const char* name; Logs::LogLevel lvl; } LEVELS[] = {
            { "debug",  Logs::LOG_DEBUG   }, { "info",    Logs::LOG_INFO    },
            { "notice", Logs::LOG_NOTICE  }, { "warning", Logs::LOG_WARNING },
            { "error",  Logs::LOG_ERROR   }, { "critical",Logs::LOG_CRITICAL},
        };
        char out[48];
        uint16_t wlen; const char* w = arg_next(args, alen, wlen);
        if (w == nullptr) {   // report current threshold
            const char* cur = "?";
            for (const auto& L : LEVELS)
                if (L.lvl == _logger->min_level()) cur = L.name;
            _emitf(out, sizeof(out), "loglevel: %s", cur);
            return;
        }
        for (const auto& L : LEVELS)
            if (arg_eq(w, wlen, L.name)) {
                _logger->set_min_level(L.lvl);
                _emitf(out, sizeof(out), "loglevel: %s", L.name);
                return;
            }
        _println("usage: loglevel [debug|info|notice|warning|error|critical]");
#endif
    }


#if !BOTANIK_FEATURE_NETWORK
    void CommandShell::_cmd_net(const char*, uint16_t) {
        _println("net: networking disabled");
    }
#else
    void CommandShell::_cmd_net(const char*, uint16_t) {
        const SystemContext&           ctx = _console.context();
        const Network::NetworkHandler* net = ctx.network;
        // Report and bail if the link never came up (config absent / offline).
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
        // Config values are read by name off the config handler (null-safe).
        // Dotted-leader "label .... : value" lines. Sized to hold
        // a pair of host strings (DNS / NTP servers); the scrollback soft-wraps
        // if a long pair overruns 80 cols.
        char val[2 * Config::HOST_STR_LEN + 8];

        // Format an address into a small static scratch (one at a time;
        // sequential use is fine).
        char ipbuf[16];
        auto ip4 = [&](const HAL::Network::Ip4& a) -> const char* {
            return HAL::Network::ip4_to_str(a, ipbuf, sizeof(ipbuf));
        };

        // --- Link --------------------------------------------------------
        const char* mode = Vars::get_int(_bank(), "net_dhcp") ? "DHCP" : "static";
        snprintf(val, sizeof(val), "%s  (%s)", net->status_str(), mode);
        _line("Network ..............", val);

        // IP / netmask / gateway / DNS are the active link parameters (the DHCP
        // lease or static config, whichever is live), read back from the HAL so
        // a DHCP-assigned value shows too.
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
            // State, then time since the last successful publish in parens.
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

    // History & kill ring
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
        // Replace the editor line: go to the end, delete it, insert recalled
        // text.
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

    void CommandShell::push_kill(const char* buf, uint16_t len) {
        _kill_len = std::min(len, (uint16_t)Config::SHELL_KILL_SIZE);
        memcpy(_kill, buf, _kill_len);
    }

    void CommandShell::kill_to_line_end() {
        uint16_t off  = _editor.cursor_offset();
        uint16_t rest = (uint16_t)(_editor.buffer_len() - off);
        if (rest == 0) return;
        // stores up to SHELL_KILL_SIZE
        push_kill(_editor.buffer() + off, rest);
        _editor.delete_forward(rest);              // remove the whole tail
    }

    void CommandShell::paste_kill() {
        if (_kill_len > 0) _editor.insert_characters(_kill, _kill_len);
    }

}}
