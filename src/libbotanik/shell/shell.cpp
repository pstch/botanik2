// REVIEW_35: TODO
/**
 * @file shell.cpp
 * @brief CommandShell: the built-in table, dispatch, output helpers and command
 *        history. One file per noun group beside it (shell_system.cpp, ...).
 */
#include "libbotanik/shell/shell.h"         // CommandShell
#include "libbotanik/shell/args.h"          // arg_next / arg_eq

#include <algorithm>                        // std::min
#include <cstdarg>                          // va_list / va_start / va_end
#include <cstdio>                           // vsnprintf
#include <cstring>                          // strlen / strncmp / memcpy / memcmp

#include "libbotanik/config/terminal.h"     // Config::SHELL_* - the history ring

#include "libbotanik/config/logging.h"      // Config::LOG_FORMATTED_LEN
#include "libbotanik/config/net.h"          // Config::HOST_STR_LEN

namespace Botanik { namespace Shell {



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

    // The table is grouped the way `help` prints it and the way the manual
    // documents it: what the unit is DOING, what it is REPORTING, then the
    // tables you work on. A subject is one row with its own verbs, never one
    // row per verb - `params set` and `params save` are the same subject, and
    // splitting them put the two halves of one job in different places.
    const CommandShell::Builtin* CommandShell::_builtins(uint8_t& count) {
        // The group each command is filed under; `help <group>` lists one.
        // Same partition as docs/user/console.md, so neither has to be read
        // twice to find a command.
        static const char* const G_SYS = "system";
        static const char* const G_STA = "status";
        static const char* const G_TAB = "tables";
        static const char* const G_CON = "console";

        static const Builtin TABLE[] = {
            { "logs",       "live log output on the shell page",
                            "logs on | off | status",                      G_SYS,
                                                               &CommandShell::_cmd_logs       },
            { "control",    "whether the firmware may drive the field",
                            "control enable | inhibit | disable | status",  G_SYS,
                                                               &CommandShell::_cmd_control    },
            { "ntpsync",    "force an NTP resync now",         nullptr,     G_SYS,
                                                               &CommandShell::_cmd_ntpsync    },
            { "breakpoint", "trap into an attached debugger",  nullptr,     G_SYS,
                                                               &CommandShell::_cmd_breakpoint },
            { "reboot",     "stop kicking the watchdog and let it reset the unit",
                                                               nullptr,     G_SYS,
                                                               &CommandShell::_cmd_reboot     },
            { "reset",      "reset the unit now",              nullptr,     G_SYS,
                                                               &CommandShell::_cmd_reset      },
            { "exit",       "quit (sim) / reboot (hw)",        nullptr,     G_SYS,
                                                               &CommandShell::_cmd_exit       },

            { "stats",      "scheduler / system metrics",      nullptr,     G_STA,
                                                               &CommandShell::_cmd_stats      },
            { "net",        "network / NTP / MQTT status",     nullptr,     G_STA,
                                                               &CommandShell::_cmd_net        },
            { "modbus",     "the field bus: plan, state and devices",
                            "modbus stats | devices",                       G_STA,
                                                               &CommandShell::_cmd_modbus     },
            { "tasks",      "what the beat spends its time on",
                            "tasks stats | list [<prefix>] | get <name>",    G_STA,
                                                               &CommandShell::_cmd_tasks      },
            { "version",    "firmware version",                nullptr,     G_STA,
                                                               &CommandShell::_cmd_version    },
            { "features",   "which subsystems are built in",   nullptr,     G_STA,
                                                               &CommandShell::_cmd_features   },

            // Only the subject here - the verbs are one `<noun> help` away, so
            // this stays one readable screen.
            { "params",     "the editable settings",
                            "params list [<prefix>] | get <name> | set <name> <value>\n"
                            "params clear <name>|all | save <name>|all",     G_TAB,
                                                               &CommandShell::_cmd_params     },
            { "overrides",  "force any variable by hand",
                            "overrides list | get <name> | set <name> [<value>] [valid|invalid]\n"
                            "overrides clear <name>|all | save <name>|all",  G_TAB,
                                                               &CommandShell::_cmd_overrides  },
            { "programs",   "the control programs in the scan",
                            "programs list [<prefix>] | enable <name>|all\n"
                            "programs disable <name>|all | save <name>|all", G_TAB,
                                                               &CommandShell::_cmd_programs   },
            { "alarms",     "the alarm table, and acknowledging",
                            "alarms list [<prefix>] | get <code|name> | ack <code|name|all>",
                                                                            G_TAB,
                                                               &CommandShell::_cmd_alarms     },
            { "vars",       "read any variable, whatever its scope",
                            "vars list [<prefix>] | get <name>",             G_TAB,
                                                               &CommandShell::_cmd_vars       },

            { "help",       "list commands",                   nullptr,     G_CON,
                                                               &CommandShell::_cmd_help       },
            { "clear",      "clear the screen",                nullptr,     G_CON,
                                                               &CommandShell::_cmd_clear      },
        };
        count = (uint8_t)(sizeof(TABLE) / sizeof(TABLE[0]));
        return TABLE;
    }

    const CommandShell::HelpGroup* CommandShell::_help_groups(uint8_t& count) {
        static const HelpGroup G[] = {
            { "system",  "driving the unit, and the ways out of it" },
            { "status",  "what the unit is doing right now" },
            { "tables",  "settings, overrides, programs, alarms, variables" },
            { "console", "the console itself" },
            { "site",    "commands this installation added" },
        };
        count = (uint8_t)(sizeof(G) / sizeof(G[0]));
        return G;
    }

    void CommandShell::_verb_usage(const char* noun) {
        uint8_t n; const Builtin* B = _builtins(n);
        const char* u = nullptr;
        for (uint8_t i = 0; i < n; i++)
            if (B[i].usage != nullptr && strcmp(B[i].name, noun) == 0) { u = B[i].usage; break; }
        if (u == nullptr) return;

        // Newline-separated in the table, one write() each: a long verb list
        // stacks instead of wrapping into the "> " continuation, which loses the
        // shape that makes it scannable.
        char out[Config::LOG_FORMATTED_LEN];
        bool first = true;
        while (*u != '\0') {
            size_t k = 0;
            while (u[k] != '\0' && u[k] != '\n') k++;
            _emitf(out, sizeof(out), "%s%.*s", first ? "usage: " : "       ", (int)k, u);
            first = false;
            u += k;
            if (*u == '\n') u++;
        }
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
        for (uint8_t i = 0; i < bcount; i++) {
            if (!arg_eq(name, wlen, B[i].name)) continue;
            // A NOUN answers its own usage, so the detail is one word from the
            // command rather than a screenful in `help`. Peeked, not consumed:
            // the handler still gets the whole argument slice.
            if (B[i].usage != nullptr) {
                const char* peek = args; uint16_t plen = alen, vlen;
                const char* verb = arg_next(peek, plen, vlen);
                if (verb == nullptr || arg_eq(verb, vlen, "help")) {
                    _verb_usage(B[i].name); return;
                }
            }
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

    void CommandShell::_cmd_help(const char* args, uint16_t alen) {
        char out[Config::LOG_FORMATTED_LEN];
        uint16_t wlen; const char* key = arg_next(args, alen, wlen);

        if (key != nullptr) {
            if (_help_group(key, wlen)) return;
            _emitf(out, sizeof(out), "help: no group '%.*s'", (int)wlen, key);
        }

        // The summary only. Listing every command here ran past a 25-row screen
        // once the tables grew verbs of their own, and a help that scrolls off
        // is not a help.
        uint8_t gcount; const HelpGroup* G = _help_groups(gcount);
        for (uint8_t i = 0; i < gcount; i++) {
            // The site group is only real when a site registered something.
            if (_command_count == 0 && strcmp(G[i].key, "site") == 0) continue;
            _emitf(out, sizeof(out), "  %-10s %s", G[i].key, G[i].help);
        }
        _println("`help <group>` lists a group; `<command> help` its verbs");
    }

    bool CommandShell::_help_group(const char* key, uint16_t klen) {
        char out[Config::LOG_FORMATTED_LEN];
        char nm[24];

        if (arg_eq(key, klen, "site")) {
            if (_command_count == 0) return false;
            for (size_t i = 0; i < _command_count; i++) {
                const ShellCommand& c = _commands[i];
                if (c.name == nullptr) continue;
                _emitf(out, sizeof(out), "  %-14s %s", c.name, c.help ? c.help : "");
            }
            return true;
        }

        uint8_t gcount; const HelpGroup* G = _help_groups(gcount);
        bool known = false;
        for (uint8_t i = 0; i < gcount; i++)
            if (arg_eq(key, klen, G[i].key)) { known = true; break; }
        if (!known) return false;

        uint8_t bcount; const Builtin* B = _builtins(bcount);
        for (uint8_t i = 0; i < bcount; i++) {
            if (B[i].group == nullptr || !arg_eq(key, klen, B[i].group)) continue;
            // "..." marks a command that wants a verb after it - exactly the
            // ones a bare call answers with usage, so the mark and the
            // behaviour cannot disagree.
            snprintf(nm, sizeof(nm), "%s%s", B[i].name, B[i].usage ? " ..." : "");
            _emitf(out, sizeof(out), "  %-14s %s", nm, B[i].help);
        }
        return true;
    }

    void CommandShell::_cmd_clear(const char*, uint16_t) { _output.clear(); }

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
