// REVIEW_35: TODO
/**
 * @file shell.cpp
 * @brief CommandShell implementation - command dispatch and built-in commands.
 */
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../../libbotanik_config.h"
#include "../alarms/alarms.h"
#include "../hal/platform.h"
#include "../hal/storage.h"
#include "../logs/logger.h"
#include "libbotanik/vars/params.h"
#include "../network/mqtt.h"
#include "../network/network.h"
#include "../network/ntp.h"
#include "../scheduler/scheduler.h"
#include "shell.h"
#include "../utils/text.h"

// Build-time git short hash: a generated header on the CMake builds (see
// CMakeLists), "unknown" on the Arduino .ino deploy (no CMake, so the header
// isn't generated).
#if defined(__has_include) && __has_include("botanik_gitversion.h")
#  include "botanik_gitversion.h"
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
            { "uptime",     "time since boot",                &CommandShell::_cmd_uptime     },
            { "net",        "network / NTP / MQTT status",     &CommandShell::_cmd_net        },
            { "ntpsync",    "force an NTP resync now",         &CommandShell::_cmd_ntpsync    },
            { "alarms",     "[list|ack <code|all>] alarms",   &CommandShell::_cmd_alarms     },
            { "params",     "[get <n>|set <n> <v>] a parameter", &CommandShell::_cmd_params     },
            { "status",     "[get <name>] status variables",  &CommandShell::_cmd_status     },
            { "vars",       "[<name>] field IO variables",    &CommandShell::_cmd_vars       },
            { "storage",    "get|set|del <key> [<text>]",     &CommandShell::_cmd_storage    },
            { "loglevel",   "[debug|info|..|critical] filter",&CommandShell::_cmd_loglevel   },
            { "version",    "firmware version",               &CommandShell::_cmd_version    },
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
        char out[2 * Config::PLC_HOST_STR_LEN + 32];
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

    // Wipe the screen (flushed straight to the wire) before leaving: the main
    // loop won't run again to drain the TX ring, and neither reset() nor
    // shutdown() returns.
    void CommandShell::_cmd_reset(const char*, uint16_t) {
        _terminal.clear_screen_now();
        HAL::System::reset();      // reboots; does not return
    }

    void CommandShell::_cmd_exit(const char*, uint16_t) {
        _terminal.clear_screen_now();
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

        const Alarms::AlarmModel* m = _console.alarms();
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
                    c.code ? c.code : "?",
                    Utils::format_age(age, sizeof(age), secs, false),
                    c.user_ack ? " [ack]" : "");
            }
            return;
        }

        // Default: the count summary.
        _emitf(out, sizeof(out), "alarms: %u active", m->active_count());
    }

    void CommandShell::_cmd_params(const char* args, uint16_t alen) {
        if (_params == nullptr) { _println("params: unavailable"); return; }
        char out[128];
        uint16_t w0; const char* a0 = arg_next(args, alen, w0);

        // "config" with no args: list every parameter and its value.
        if (a0 == nullptr) {
            char val[40];
            for (uint16_t i = 0; i < _params->count(); i++) {
                const char* v = _params->format(i, val, sizeof(val));
                _emitf(out, sizeof(out), "  %-18s %s",
                       _params->param(i).meta()->name, v);
            }
            return;
        }

        // "config set <name> <value>"
        if (arg_eq(a0, w0, "set")) {
            uint16_t wn; const char* nm = arg_next(args, alen, wn);
            uint16_t wv; const char* v  = arg_next(args, alen, wv);
            if (nm == nullptr || v == nullptr) {
                _println("usage: params set <name> <value>"); return;
            }
            const Vars::VarRef r = _params->find(nm, wn);
            if (r.index == Vars::VAR_NONE) { _println("params: unknown parameter"); return; }
            Vars::Var p = _vars->var(r);
            char val[96];
            uint16_t vc = std::min(wv, (uint16_t)(sizeof(val) - 1));
            memcpy(val, v, vc); val[vc] = '\0';
            switch (_params->set(p, val)) {
                case Vars::SET_OK:
                    _emitf(out, sizeof(out), "%s = %s", p.meta()->name, val);
                    break;
                case Vars::SET_NOT_PERSISTED:
                    // Live, but it dies at the next reboot - say which of the
                    // two failed, since the fixes are entirely different.
                    _emitf(out, sizeof(out), "%s = %s  (NOT SAVED: storage refused it)",
                           p.meta()->name, val);
                    break;
                default:
                    _emitf(out, sizeof(out), "params: invalid value for %s", p.meta()->name);
                    break;
            }
            return;
        }

        // "config get <name>" or "config <name>"
        const char* nm = a0; uint16_t wn = w0;
        if (arg_eq(a0, w0, "get")) nm = arg_next(args, alen, wn);
        if (nm == nullptr) { _println("usage: params get <name>"); return; }
        const Vars::VarRef r = _params->find(nm, wn);
        if (r.index == Vars::VAR_NONE) { _println("params: unknown parameter"); return; }
        char val[40];
        Vars::Var p = _vars->var(r);
        const char* fv = Vars::var_format(*_vars, p, val, sizeof(val));
        _emitf(out, sizeof(out), "%s = %s", p.meta()->name, fv);
    }

    void CommandShell::_cmd_vars(const char* args, uint16_t alen) {
        if (_vars == nullptr) { _println("vars: unavailable"); return; }
        uint16_t wn = 0;
        const char* nm = arg_next(args, alen, wn);

        // One table, so one loop. The old version carried a hand-maintained
        // pair of kind/count arrays and a literal bound of 6, and silently
        // omitted whatever it had not been told about.
        char out[128], val[48];
        bool found = false;
        for (uint16_t i = 0; i < _vars->count(); i++) {
            Vars::Var h = _vars->var(i);
            const char* name = h.meta()->name;
            if (name == nullptr) continue;
            if (nm != nullptr && !arg_eq(nm, wn, name)) continue;
            const char* s = Vars::var_format(*_vars, h, val, sizeof(val));
            const char* u = h.meta()->unit;
            // A variable reading its default is worth marking: the number looks
            // perfectly reasonable, which is exactly the trap.
            _emitf(out, sizeof(out), "  %-20s %-10s %-4s %s", name, s,
                   (u && u[0]) ? u : "",
                   h.valid() ? "" : "(default: nothing has written it)");
            found = true;
        }
        if (!found) _println(nm != nullptr ? "vars: unknown variable"
                                           : "vars: no variables declared");
    }

    void CommandShell::_cmd_storage(const char* args, uint16_t alen) {
        // Raw access to HAL::Storage. Exists so the OPTA's KV backend can be
        // brought up with a set / power-cycle / get round-trip instead of a
        // firmware rebuild per hypothesis - where the Mbed KV region actually
        // lands is a config decision we do not make and cannot check from here.
        uint16_t w = 0;
        const char* op = arg_next(args, alen, w);
        uint16_t kw = 0;
        const char* key = arg_next(args, alen, kw);
        if (op == nullptr || key == nullptr) {
            _println("usage: storage get|set|del <key> [<text>]");
            return;
        }
        // The key must be NUL-terminated for the HAL; arg_next hands back a
        // span into the input line.
        char k[64];
        if (kw >= sizeof(k)) { _println("storage: key too long"); return; }
        memcpy(k, key, kw); k[kw] = 0;

        char out[128];
        if (arg_eq(op, w, "get")) {
            char val[96]; size_t n = 0;
            if (!HAL::Storage::get(k, val, sizeof(val) - 1, &n)) {
                _emitf(out, sizeof(out), "storage: '%s' not found", k);
                return;
            }
            val[n] = 0;
            _emitf(out, sizeof(out), "  %s = %s (%u bytes)", k, val, (unsigned)n);
        } else if (arg_eq(op, w, "set")) {
            uint16_t vw = 0;
            const char* val = arg_next(args, alen, vw);
            if (val == nullptr) { _println("storage: set needs a value"); return; }
            _emitf(out, sizeof(out), "storage: %s",
                   HAL::Storage::set(k, val, vw) ? "ok" : "FAILED");
        } else if (arg_eq(op, w, "del")) {
            _emitf(out, sizeof(out), "storage: %s",
                   HAL::Storage::remove(k) ? "ok" : "FAILED");
        } else {
            _println("usage: storage get|set|del <key> [<text>]");
        }
    }

    void CommandShell::_cmd_status(const char* args, uint16_t alen) {
        if (_status_groups == nullptr || _vars == nullptr) {
            _println("status: unavailable"); return;
        }
        char out[128], val[64];
        uint16_t w0; const char* a0 = arg_next(args, alen, w0);
        const char* nm = a0; uint16_t wn = w0;
        if (a0 != nullptr && arg_eq(a0, w0, "get")) nm = arg_next(args, alen, wn);

        bool found = false;
        for (size_t gi = 0; gi < _status_group_count; ++gi) {
            const Vars::VarGroup& g = _status_groups[gi];
            for (size_t i = 0; i < g.count; ++i) {
                Vars::Var h = _vars->var(g.refs[i]);
                const char* name = h.meta()->name;
                if (name == nullptr) continue;
                if (nm != nullptr && !arg_eq(nm, wn, name)) continue;
                const char* s = Vars::var_format(*_vars, h, val, sizeof(val));
                const char* u = h.meta()->unit;
                // Append the physical unit when the variable carries one, and
                // flag a reading nothing has written - it is showing a default.
                _emitf(out, sizeof(out), "  %-18s %s%s%s%s", name, s,
                       (u && u[0]) ? " " : "", (u && u[0]) ? u : "",
                       h.valid() ? "" : "   (no data)");
                found = true;
            }
        }
        if (nm != nullptr && !found) _println("status: unknown variable");
    }

    void CommandShell::_cmd_loglevel(const char* args, uint16_t alen) {
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
    }

    void CommandShell::_cmd_uptime(const char*, uint16_t) {
        char buf[16], out[48];
        uint32_t secs = (uint32_t)(HAL::Clock::uptime_ms() / 1000);
        _emitf(out, sizeof(out), "uptime: %s",
               Utils::format_age(buf, sizeof(buf), secs, false));
    }

    void CommandShell::_cmd_net(const char*, uint16_t) {
#if !BOTANIK_FEATURE_NETWORK
        _println("net: networking disabled");
    }
#else
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
        // Same dotted-leader "label .... : value" lines as `plc`. Sized to hold
        // a pair of host strings (DNS / NTP servers); the scrollback soft-wraps
        // if a long pair overruns 80 cols.
        char val[2 * Config::PLC_HOST_STR_LEN + 8];

        // Format an address into a small static scratch (one at a time;
        // sequential use is fine).
        char ipbuf[16];
        auto ip4 = [&](const HAL::Network::Ip4& a) -> const char* {
            return HAL::Network::ip4_to_str(a, ipbuf, sizeof(ipbuf));
        };

        // --- Link --------------------------------------------------------
        const char* mode = Vars::get_int(_vars, "net_dhcp") ? "DHCP" : "static";
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
            const char* s1 = Vars::get_str(_vars, "ntp_server1");
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
            const char* brk = Vars::get_str(_vars, "mqtt_broker");
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
#endif  // BOTANIK_FEATURE_NETWORK
    }

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
