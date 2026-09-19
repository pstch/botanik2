/**
 * @file shell.cpp
 * @brief CommandShell: the built-in table, dispatch, output helpers and command
 *        history. One file per noun group beside it (shell_system.cpp, ...).
 */
#include "libbotanik/utils/minmax.h"
#include "libbotanik/shell/shell.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_SHELL_PAGE
#include "libbotanik/shell/args.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "libbotanik/config/terminal.h"

#include "libbotanik/config/logging.h"
#include "libbotanik/config/net.h"

namespace Botanik { namespace Shell {



    // Lifecycle

    CommandShell::CommandShell(Terminal::AnsiTerminal& terminal,
                               Terminal::TextEditor&   editor,
                               ShellWriter&            output,
                               const SystemContext*    sys)
        : _sys(sys), _terminal(terminal), _editor(editor), _output(output)
    {}

    // Command execution

    void CommandShell::run(const char* cmd, uint16_t len) {
        if (len == 0) return;
        _history_add(cmd, len);
        _dispatch(cmd, len);
    }

    // Internals

    // The table is grouped the way `help` prints it and the way the manual
    // documents it: what the unit is DOING, what it is REPORTING, then the
    // tables you work on. A subject is one row with its own verbs, never one
    // row per verb - `params set` and `params save` are the same subject, and
    // splitting them put the two halves of one job in different places.
    const CommandShell::Builtin* CommandShell::_builtins(uint8_t& count) {
        // The topic each command is filed under; `help <topic>` lists one. Same
        // partition as docs/user/console.md. `main` is the shortcut list the
        // help page opens with - a command may be in both.
        static const char* const G_SYS = "system";
        static const char* const G_STA = "status";
        static const char* const G_TAB = "tables";

        static const Builtin TABLE[] = {
            // name        args                     help
            //             verbs (one spelling per line)          topic  main
            { "help",      "[<topic>]", "this help page, or a topic's help page",
                           nullptr,                               G_SYS, true,
                                                               &CommandShell::_cmd_help       },
            { "breakpoint", nullptr,    "break into an attached debugger",
                           nullptr,                               G_SYS, true,
                                                               &CommandShell::_cmd_breakpoint },
            { "version",   nullptr,     "show code version",
                           nullptr,                               G_STA, true,
                                                               &CommandShell::_cmd_version    },
            { "reboot",    "confirm",   "soft reboot (watchdog resets us)",
                           nullptr,                               G_SYS, true,
                                                               &CommandShell::_cmd_reboot     },
            { "halt",      "confirm",   "park the plant, then stop the cycle",
                           nullptr,                               G_SYS, true,
                                                               &CommandShell::_cmd_halt       },
            { "reset",     "confirm",   "immediate reset",
                           nullptr,                               G_SYS, true,
                                                               &CommandShell::_cmd_reset      },
            { "clear",     nullptr,     "clear the screen",
                           nullptr,                               G_SYS, true,
                                                               &CommandShell::_cmd_clear      },
            { "exit",      "confirm",   "exit the simulator (reboot on hardware)",
                           nullptr,                               G_SYS, true,
                                                               &CommandShell::_cmd_exit       },
            { "logs",      "on|off",    "show logs in the shell",
                           &CommandShell::_logs_verbs,
                                                                  G_SYS, true,
                                                               &CommandShell::_cmd_logs       },
            { "log",       "<level> <text>", "write an entry of your own to the log",
                           &CommandShell::_log_verbs,
                                                                  G_SYS, false,
                                                               &CommandShell::_cmd_log        },
#if BOTANIK_FEATURE_NTP
            { "ntpsync",   nullptr,     "force an NTP resync now",
                           nullptr,                               G_SYS, false,
                                                               &CommandShell::_cmd_ntpsync    },
#endif
            { "stats",     nullptr,     "scheduler, stack and watchdog metrics",
                           nullptr,                               G_STA, false,
                                                               &CommandShell::_cmd_stats      },
            { "net",       nullptr,     "network / NTP / MQTT status",
                           nullptr,                               G_STA, false,
                                                               &CommandShell::_cmd_net        },
            { "features",  nullptr,     "which subsystems are built in",
                           nullptr,                               G_STA, false,
                                                               &CommandShell::_cmd_features   },
            { "modbus",    "...",       "the field bus: plan, state and devices",
                           &CommandShell::_modbus_verbs,
                                                                  G_STA, false,
                                                               &CommandShell::_cmd_modbus     },
            { "tasks",     "...",       "what the beat spends its time on",
                           &CommandShell::_tasks_verbs,
                                                                  G_STA, false,
                                                               &CommandShell::_cmd_tasks      },

            { "params",    "...",       "the editable settings",
                           &CommandShell::_params_verbs,
                                                                  G_TAB, false,
                                                               &CommandShell::_cmd_params     },
            { "overrides", "...",       "force any variable by hand",
                           &CommandShell::_overrides_verbs,
                                                                  G_TAB, false,
                                                               &CommandShell::_cmd_overrides  },
            { "control",   "...",       "whether the control programs drive at all",
                           &CommandShell::_control_verbs,
                                                                  G_SYS, true,
                                                               &CommandShell::_cmd_control    },
#if BOTANIK_FEATURE_PLC
            { "programs",  "...",       "the control programs in the scan",
                           &CommandShell::_programs_verbs,
                                                                  G_TAB, false,
                                                               &CommandShell::_cmd_programs   },
#endif
            { "alarms",    "...",       "the alarm table, and acknowledging",
                           &CommandShell::_alarms_verbs,
                                                                  G_TAB, false,
                                                               &CommandShell::_cmd_alarms     },
            { "vars",      "...",       "read any variable, whatever its scope",
                           &CommandShell::_vars_verbs,
                                                                  G_TAB, false,
                                                               &CommandShell::_cmd_vars       },
        };
        count = (uint8_t)(sizeof(TABLE) / sizeof(TABLE[0]));
        return TABLE;
    }

    const CommandShell::Builtin* CommandShell::_noun(const char* name) {
        uint8_t n; const Builtin* B = _builtins(n);
        for (uint8_t i = 0; i < n; i++)
            if (strcmp(B[i].name, name) == 0) return &B[i];
        return nullptr;
    }

    /** @brief Whether @p usage's first word is exactly [@p w, @p w+wlen).
     *  Case-sensitive, like arg_eq(): a verb is a keyword, not a name. */
    static bool _verb_is(const char* usage, const char* w, uint16_t wlen) {
        uint16_t k = 0;
        while (k < wlen && usage[k] != '\0' && usage[k] == w[k]) k++;
        return k == wlen && (usage[k] == '\0' || usage[k] == ' ');
    }

    bool CommandShell::_run_verb(const Verb* verbs, uint8_t count,
                                 const char*& args, uint16_t& alen) {
        const char* held = args; const uint16_t held_len = alen;
        uint16_t wlen; const char* w = arg_next(args, alen, wlen);
        for (uint8_t i = 0; i < count; i++) {
            if (verbs[i].fn == nullptr) continue;   // still parsed by hand
            if (w != nullptr && _verb_is(verbs[i].usage, w, wlen)) {
                (this->*verbs[i].fn)(args, alen);
                return true;
            }
        }
        args = held; alen = held_len;
        return false;
    }

    void CommandShell::_verb_usage(const char* noun) {
        const Builtin* b = _noun(noun);
        if (b == nullptr || b->verbs == nullptr) return;
        uint8_t n; const Verb* V = b->verbs(n);

        // One verb per line, each spelled out in full with the command in front:
        // a reader copies a line rather than assembling one from a bar-separated
        // run, and nothing wraps however many verbs a command grows.
        char out[Config::LOG_FORMATTED_LEN];
        for (uint8_t i = 0; i < n; i++)
            _emitf(out, sizeof(out), "%s%s %s",
                   i == 0 ? "usage: " : "       ", b->name, V[i].usage);
    }

    void CommandShell::_verb_usage(const char* noun, const char* verb) {
        const Builtin* b = _noun(noun);
        if (b == nullptr || b->verbs == nullptr) { _verb_usage(noun); return; }
        uint8_t n; const Verb* V = b->verbs(n);

        // The row whose FIRST WORD is @p verb, printed with the noun in front
        // and its trailing description column trimmed: a usage line is what to
        // type, and the help page is where the prose belongs.
        const size_t vlen = strlen(verb);
        for (uint8_t i = 0; i < n; i++) {
            const char* v = V[i].usage;
            if (strncmp(v, verb, vlen) != 0 || (v[vlen] != ' ' && v[vlen] != '\0')) continue;
            size_t end = strlen(v);
            while (end > vlen && v[end - 1] == ' ') end--;
            // Two spaces or more is the description column, not the syntax.
            for (size_t k = vlen; k + 1 < end; k++)
                if (v[k] == ' ' && v[k + 1] == ' ') { end = k; break; }
            char out[Config::LOG_FORMATTED_LEN];
            _emitf(out, sizeof(out), "usage: %s %.*s", b->name, (int)end, v);
            return;
        }
        // No such verb in the table: the whole list is better than nothing.
        _verb_usage(noun);
    }

    // Help page

    const CommandShell::HelpGroup* CommandShell::_help_groups(uint8_t& count) {
        static const HelpGroup G[] = {
            { "system",  "driving the unit, and the ways out of it" },
            { "status",  "what the unit is doing right now" },
            { "tables",  "params, overrides, programs, alarms, vars" },
            { "site",    "commands this installation added" },
        };
        count = (uint8_t)(sizeof(G) / sizeof(G[0]));
        return G;
    }

    // A blank line: write() of nothing emits nothing, so it takes a space.
    void CommandShell::_blank() { _output.write(" ", 1); }

    void CommandShell::_rule(const char* title) {
        char out[Config::LOG_FORMATTED_LEN];
        const size_t cols = _terminal.cols() ? _terminal.cols() : 80;
        size_t n = 0;
        const char* lead = "-- ";
        while (*lead != '\0' && n + 1 < sizeof(out)) out[n++] = *lead++;
        while (*title != '\0' && n + 1 < sizeof(out) && n < cols) out[n++] = *title++;
        if (n + 1 < sizeof(out) && n < cols) out[n++] = ' ';
        while (n < cols && n + 1 < sizeof(out)) out[n++] = '-';
        _output.write(out, (uint16_t)n);
    }

    void CommandShell::_help_row(const char* left, size_t lw, const char* right) {
        char out[Config::LOG_FORMATTED_LEN];
        const size_t cols = _terminal.cols() ? _terminal.cols() : 80;
        const size_t head = 2 + lw + 3;             // indent, column, " : "
        if (cols <= head) { _emitf(out, sizeof(out), "  %s", left); return; }
        _emitf(out, sizeof(out), "  %-*s : %.*s",
               (int)lw, left, (int)(cols - head), right);
    }

    size_t CommandShell::_row_width(const char* key, uint16_t klen) const {
        uint8_t n; const Builtin* B = _builtins(n);
        size_t w = 0;
        char lab[40];
        for (uint8_t i = 0; i < n; i++) {
            const bool want = (key == nullptr) ? B[i].main
                                               : arg_eq(key, klen, B[i].group);
            if (!want) continue;
            const size_t l = _label(B[i], lab, sizeof(lab));
            if (l > w) w = l;
        }
        return w;
    }

    void CommandShell::_cmd_help(const char* args, uint16_t alen) {
        char out[Config::LOG_FORMATTED_LEN];
        uint16_t wlen; const char* key = arg_next(args, alen, wlen);

        if (key != nullptr) {
            if (_help_group(key, wlen)) return;
            _emitf(out, sizeof(out), "help: no topic '%.*s'", (int)wlen, key);
        }

        // The shortcut list, then the topics. Listing every command here ran
        // past a 25-row screen once the tables grew verbs of their own.
        char lab[40];
        uint8_t gcount; const HelpGroup* G = _help_groups(gcount);
        // One column for both sections: the topics line up under the commands.
        size_t lw = _row_width(nullptr, 0);
        for (uint8_t i = 0; i < gcount; i++) {
            if (_command_count == 0 && strcmp(G[i].key, "site") == 0) continue;
            const size_t l = strlen(G[i].key);
            if (l > lw) lw = l;
        }
        uint8_t n; const Builtin* B = _builtins(n);
        _rule("Main commands");
        for (uint8_t i = 0; i < n; i++) {
            if (!B[i].main) continue;
            _label(B[i], lab, sizeof(lab));
            _help_row(lab, lw, B[i].help);
        }

        _blank();
        _rule("Topics");
        for (uint8_t i = 0; i < gcount; i++) {
            if (_command_count == 0 && strcmp(G[i].key, "site") == 0) continue;
            _help_row(G[i].key, lw, G[i].help);
        }
    }

    bool CommandShell::_help_group(const char* key, uint16_t klen) {
        char out[Config::LOG_FORMATTED_LEN];
        char lab[40];

        if (arg_eq(key, klen, "site")) {
            if (_command_count == 0) return false;
            size_t lw = 0;
            for (size_t i = 0; i < _command_count; i++)
                if (_commands[i].name != nullptr) {
                    const size_t l = strlen(_commands[i].name);
                    if (l > lw) lw = l;
                }
            _rule("Site commands");
            for (size_t i = 0; i < _command_count; i++) {
                const ShellCommand& c = _commands[i];
                if (c.name == nullptr) continue;
                _help_row(c.name, lw, c.help ? c.help : "");
            }
            return true;
        }

        uint8_t gcount; const HelpGroup* G = _help_groups(gcount);
        const HelpGroup* g = nullptr;
        for (uint8_t i = 0; i < gcount; i++)
            if (arg_eq(key, klen, G[i].key)) { g = &G[i]; break; }
        if (g == nullptr) return false;

        (void)out;
        const size_t lw = _row_width(key, klen);
        uint8_t n; const Builtin* B = _builtins(n);
        _rule(g->key);
        for (uint8_t i = 0; i < n; i++) {
            if (B[i].group == nullptr || !arg_eq(key, klen, B[i].group)) continue;
            _label(B[i], lab, sizeof(lab));
            _help_row(lab, lw, B[i].help);
        }
        _println("`<command> help` spells out a command's verbs");
        return true;
    }

    // "name args", the left column of every listing row.
    size_t CommandShell::_label(const Builtin& b, char* buf, size_t cap) {
        const int n = snprintf(buf, cap, "%s%s%s", b.name,
                               b.args ? " " : "", b.args ? b.args : "");
        return (n < 0) ? 0 : (size_t)n;
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
            if (B[i].verbs != nullptr) {
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
        if (w > 0) _output.write(buf, Utils::min((size_t)w, cap - 1));
    }

    void CommandShell::_line(const char* label, const char* val) {
        char out[2 * Config::HOST_STR_LEN + 32];
        _emitf(out, sizeof(out), "%-22s : %s", label, val);
    }

    void CommandShell::_cmd_clear(const char*, uint16_t) { _output.clear(); }

    void CommandShell::_unknown(const char* cmd, uint16_t len) {
        // Handed to write_lines() as one blob: it wraps at terminal width and
        // splits on any embedded newline, so no manual bookkeeping is needed.
        char out[Config::LOG_FORMATTED_LEN];
        static const char prefix[] = "Unknown command: ";
        uint16_t plen = (uint16_t)(sizeof(prefix) - 1);
        uint16_t clen = Utils::min(len, (uint16_t)(sizeof(out) - plen));
        memcpy(out, prefix, plen);
        memcpy(out + plen, cmd, clen);
        _output.write_lines(out, (uint16_t)(plen + clen));
    }

    // History

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
        // ONE operation. Spelled as move-to-end + backspace + insert this was
        // three edits, two of which change the row count, inside a window the
        // page compensates for exactly one of - so a recall onto fewer rows
        // painted the text off the bottom of the band while the buffer held it,
        // and Enter ran a command that had never been on screen.
        _editor.replace_all(text, len);
    }

    void CommandShell::history_prev() {
        if (_hist_count == 0) return;
        if (_hist_browse < 0) {
            // Starting to browse: stash the in-progress line as the draft.
            _draft_len = Utils::min(_editor.buffer_len(), (uint16_t)sizeof(_draft));
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

        // The detail is cut to what is left of the line rather than allowed to
        // wrap: these reports are read as a column of labels, and one wrapped
        // row breaks the column for every row under it. A task or variable name
        // is site-defined and unbounded, so the width has to be measured here
        // rather than assumed when the format string was written.
        char out[Config::LOG_FORMATTED_LEN];
        if (detail == nullptr || detail[0] == 0) {
            _emitf(out, sizeof(out), "%s : %s", lead, value);
            return;
        }
        size_t vlen = 0;
        while (value[vlen] != 0) vlen++;
        if (vlen < 8) vlen = 8;                       // the %-8s floor
        const size_t cols = _terminal.cols() ? _terminal.cols() : 80;
        const size_t head = LEADER_W + 3 + vlen + 3;  // lead, " : ", value, " ()"
        if (cols <= head) { _emitf(out, sizeof(out), "%s : %-8s", lead, value); return; }
        _emitf(out, sizeof(out), "%s : %-8s (%.*s)", lead, value,
               (int)(cols - head), detail);
    }



}}

#endif // BOTANIK_FEATURE_SHELL_PAGE
