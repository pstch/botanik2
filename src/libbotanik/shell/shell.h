// REVIEW_35: TODO
/**
 * @file shell.h
 * @brief Command shell - dispatch, built-in commands, and extensibility hooks.
 */
#ifndef SHELL_SHELL_H
#define SHELL_SHELL_H

#include <cstddef>                          // size_t
#include <cstdint>                          // uint8_t/uint16_t/int16_t

#include "libbotanik/console/editor.h"      // Terminal::TextEditor
#include "libbotanik/console/terminal.h"    // AnsiTerminal
#include "libbotanik/core/context.h"        // SystemContext
#include "libbotanik/vars/access.h"         // Vars::Bank / var_group
#include "libbotanik/vars/params.h"         // Vars::Params

namespace Botanik {
    // logs command (set_diagnostics)
    namespace Logs    { class Logger;     }
    // alarms list/get/ack (set_diagnostics)
    namespace Alarms  { class AlarmController; }
    // reboot command (set_diagnostics): withholds the watchdog kick
    namespace Health  { class HealthMonitor; }
    // ntpsync command (set_ntp); null when the NTP feature is off
    namespace Network { class NtpClient;  }
    // params / overrides / vars: the variables come from SystemContext.
}

namespace Botanik { namespace Shell {

    /** @class ShellWriter
     *  @brief Minimal write-only interface so CommandShell needn't depend on
     *         Pages. */
    struct ShellWriter {
        /** @brief Write @p len bytes of output; returns the number consumed. */
        virtual size_t write(const char* buf, size_t len) = 0;
        /** @brief Clear all previously written output. Default: no-op. */
        virtual void   clear() {}
        /** @brief Enable/disable live log output in the terminal. Default:
         *  no-op. */
        virtual void   set_logs_enabled(bool) {}
        /** @brief Whether live log output is currently enabled. Default:
         *  true. */
        virtual bool   logs_enabled() const { return true; }
        /** @brief Write @p buf split into terminal-width chunks, one write()
         *  per chunk. */
        virtual void   write_lines(const char* buf, uint16_t len) { write(buf, len); }
    };

    /** @brief Site-defined shell command: name, one-line help, callback.
     *  Registered as a table via CommandShell::set_commands() and tried before
     *  the built-ins, so the table can override one. The callback gets the line
     *  after the name (leading spaces trimmed) as @p args / @p arglen, and the
     *  @p ctx passed to set_commands(). */
    struct ShellCommand {
        const char* name;
        const char* help;
        void (*fn)(ShellWriter& out, const char* args, uint16_t arglen, void* ctx);
    };

    /** @class CommandShell
     *  @brief Handles command submission, built-in dispatch, and extensibility.
     *
     *  Command history (up/down recall with a saved draft) is backed by the
     *  SHELL_HISTORY_* buffers in config/terminal.h.
     *  @see Terminal::TextEditor - cut and paste are the editor's, over the
     *  console-wide kill ring, so a line cut here yanks on another page. */
    class CommandShell {
    private:
        /** @name State
         *  @{ */

        /** @brief The subsystem handles the reporting commands read (scheduler
         *  for `stats`, network/ntp/mqtt for `net`, the bank for the variable
         *  commands). Injected by the firmware, never reached for through the
         *  console. */
        const SystemContext*    _sys;
        /** VT device the shell renders through */
        Terminal::AnsiTerminal& _terminal;
        /** editor supplying the input line */
        Terminal::TextEditor&   _editor;
        /** output channel for command results */
        ShellWriter&            _output;
        /** site command table (set_commands()) */
        const ShellCommand*     _commands      = nullptr;
        size_t                  _command_count = 0;
        /** opaque ctx for each site callback */
        void*                   _command_ctx   = nullptr;
        /** @brief Mutable diagnostics targets for the logs / alarms / reboot
         *  commands; null until set_diagnostics() wires them (commands then
         *  report "unavailable"). */
        Logs::Logger*           _logger        = nullptr;
        Alarms::AlarmController* _alarm_model  = nullptr;
        Health::HealthMonitor*  _health        = nullptr;
        /** @brief NTP client for the `ntpsync` command; null until set_ntp()
         *  wires it (off when the NTP feature is absent). */
        Network::NtpClient*     _ntp_client           = nullptr;

        /** @brief Command-history ring: the most-recent submitted lines. */
        char     _history[Config::SHELL_HISTORY_SIZE][Config::SHELL_HISTORY_ENTRY];
        // bytes stored per slot
        uint16_t _hist_len[Config::SHELL_HISTORY_SIZE] = {};
        uint8_t  _hist_count  = 0;     // entries stored (<= SHELL_HISTORY_SIZE)
        uint8_t  _hist_head   = 0;     // next write slot (ring index)
        // browse position: -1 = draft, 0 = newest, up
        int16_t  _hist_browse = -1;
        /** @brief In-progress line saved when browsing starts, restored at the
         *  bottom. */
        char     _draft[Config::SHELL_INPUT_LEN];
        uint16_t _draft_len   = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        CommandShell(Terminal::AnsiTerminal& terminal,
                     Terminal::TextEditor&   editor,
                     ShellWriter&            output,
                     const SystemContext*    sys = nullptr);

        /** @} */

        /** @name Command execution
         *  @{ */

        /** @brief Dispatch a pre-snapshotted command (caller owns snapshot and
         *  reset). A zero-length command is ignored. */
        void run(const char* cmd, uint16_t len);

        /** @brief Wire the mutable diagnostics targets the logs / alarms / reboot
         *  commands act on (the firmware passes its logger, alarm model and
         *  health monitor). */
        void set_diagnostics(Logs::Logger* logger, Alarms::AlarmController* alarms,
                             Health::HealthMonitor* health = nullptr) {
            _logger = logger; _alarm_model = alarms; _health = health;
        }

        /** @brief Wire the NTP client `ntpsync` resyncs. Null when the NTP
         *  feature is off, so the command reports "unavailable". */
        void set_ntp(Network::NtpClient* ntp) { _ntp_client = ntp; }

        /** @brief Register a site command table, tried before the built-ins.
         *  @p table must outlive the shell (typically a static at the site).
         *  @p ctx is passed to each callback. Pass count 0 (or null) to
         *  clear.
         *  @warning A site name that collides with a built-in overrides it,
         *  including `exit` and `reset`. */
        void set_commands(const ShellCommand* table, size_t count,
                          void* ctx = nullptr) {
            // Cap defensively so a bogus count can't walk off the table.
            if (count > Config::SHELL_COMMANDS_MAX) count = Config::SHELL_COMMANDS_MAX;
            _commands      = table;
            _command_count = (table != nullptr) ? count : 0;
            _command_ctx   = ctx;
        }

        /** @} */

        /** @name History management
         *  @{ */

        /** @brief Recall the previous (older) history entry into the editor.
         *  The first call saves the in-progress line as the draft, restored by
         *  paging back down. */
        void history_prev();

        /** @brief Recall the next (newer) entry; at the newest, restore the
         *  draft. */
        void history_next();

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Match the first word against the site command table (which
         *  may override a built-in) then the built-in table, and run the
         *  handler. */
        void _dispatch(const char* cmd, uint16_t len);

        /** @brief One built-in: name, the one line `help` prints, the full verb
         *  list, and the handler.
         *
         *  A non-null @c usage marks a NOUN - a command whose work is spelled by
         *  a verb after it. `help` then prints only @c help for it, and
         *  _dispatch() answers a bare `<noun>` or `<noun> help` with @c usage
         *  rather than calling the handler: one screen of commands, and the
         *  detail one word away. A command with no verbs leaves it null and
         *  behaves as it always did. */
        struct Builtin { const char* name; const char* help; const char* usage;
                         void (CommandShell::*fn)(const char* args, uint16_t alen); };
        /** @brief The built-in table - single source for both dispatch and
         *  `help`, so help text can't drift from the command set. */
        static const Builtin* _builtins(uint8_t& count);

        /** @brief Print @p noun's usage line, looked up in the built-in table so
         *  the text has ONE home: `help`, a bare noun and a mistyped verb all
         *  reach the same string. */
        void _verb_usage(const char* noun);

        /** @brief Append a submitted line to the history ring (skips a
         *  consecutive duplicate) and ends any in-progress browsing. */
        void _history_add(const char* cmd, uint16_t len);
        /** @brief Replace the editor line with history slot @c _hist_browse (or
         *  the draft when _hist_browse < 0). */
        void _load_browse();

        /** @name Built-in command handlers
         *
         *  Each takes the (already leading-space-trimmed) argument slice after
         *  the command word; handlers that take no args ignore it. The ones
         *  that work on a table (`params`, `overrides`, `alarms`, `vars`,
         *  `tasks`) are NOUNS: they parse their own verb, so the top-level
         *  table stays one row per subject rather than one per verb.
         *  @{ */
        void _cmd_help(const char* args, uint16_t alen);
        void _cmd_clear(const char* args, uint16_t alen);
        void _cmd_logs(const char* args, uint16_t alen);
        void _cmd_breakpoint(const char* args, uint16_t alen);
        void _cmd_reset(const char* args, uint16_t alen);
        /** @brief Drop control authority, then stop kicking the watchdog and let
         *  it reset the unit - the unattended reset path, by hand. */
        void _cmd_reboot(const char* args, uint16_t alen);
        void _cmd_exit(const char* args, uint16_t alen);
        void _cmd_stats(const char* args, uint16_t alen);
        /** @brief The beat's totals, or one task: where `stats`' unattributed
         *  CPU figure went. */
        void _cmd_tasks(const char* args, uint16_t alen);
        void _cmd_version(const char* args, uint16_t alen);
        void _cmd_features(const char* args, uint16_t alen);
        void _cmd_alarms(const char* args, uint16_t alen);
        /** @brief The field bus: plan cost, master state, devices answering. */
        void _cmd_modbus(const char* args, uint16_t alen);
        #if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        /** @brief `modbus devices`: one line per unit, named and counted. */
        void _modbus_devices(const Modbus::RtuClient& rtu);
        #endif
        void _cmd_net(const char* args, uint16_t alen);
        void _cmd_ntpsync(const char* args, uint16_t alen);
        /** @brief `params list|get|set|clear|save` - the editable rows. */
        void _cmd_params(const char* args, uint16_t alen);
        /** @brief `programs list|enable|disable|save` - the control programs. */
        void _cmd_programs(const char* args, uint16_t alen);
        /** @brief `overrides list|get|set|clear|save` - the forced rows. */
        void _cmd_overrides(const char* args, uint16_t alen);
        /** @brief `vars list|get` - every row, whatever its scope. */
        void _cmd_vars(const char* args, uint16_t alen);
        void _cmd_control(const char* args, uint16_t alen);

        /** @name Editing verbs
         *
         *  Editing and committing are two commands because they are two
         *  decisions: a value is live the moment it is set, and durable only
         *  once saved. @see Vars::Bank::set()
         *  @{ */
        /** @brief `params set <name> <value>`. @p nm / @p wn are the name word;
         *  @p args / @p alen carry the value and are consumed here. */
        void _params_set(const char* nm, uint16_t wn,
                         const char* args, uint16_t alen);
        /** @brief `params save <name>|all` - one row, or every unsaved one. */
        void _params_save(const char* nm, uint16_t wn);
        /** @brief `params clear <name>|all` - back to the factory default. */
        void _params_clear(const char* nm, uint16_t wn);
        /** @brief `overrides save <name>|all` - the same, for overrides. */
        void _overrides_save(const char* nm, uint16_t wn);
        /** @brief `overrides list` - what is forced right now. */
        void _overrides_list();
        /** @brief `overrides clear <name>|all` - release one, or the lot. */
        void _overrides_clear(const char* nm, uint16_t wn);
        /** @brief `overrides set <name> [<value>] [valid|invalid]`. @p args /
         *  @p alen carry the value and the quality word and are consumed. */
        void _overrides_set(const char* nm, uint16_t wn,
                            const char* args, uint16_t alen);
        /** @brief Report @p h's override after a write, with what it reads as
         *  and how to make it durable. */
        void _override_reply(Vars::Var h, const char* verb);
        /** @brief `programs enable|disable <name>|all`. @p on is which. */
        void _programs_gate(const char* nm, uint16_t wn, bool on);
        /** @brief `programs save <name>|all`. */
        void _programs_save(const char* nm, uint16_t wn);
        /** @} */

        /** @name Shared verb bodies
         *
         *  `params get` and `vars get` are the same detail view over a
         *  different admission rule, and both list forms are one filtered walk.
         *  Written once, so a row reads the same whichever noun showed it.
         *  @{ */
        /** @brief Resolve @p nm to a row, printing "<noun>: unknown variable"
         *  when it names none. The lookup every table verb starts with, so the
         *  message cannot drift between them.
         *  @return its ref, or one whose index is Vars::VAR_NONE. */
        Vars::VarRef _resolve(const char* noun, const char* nm, uint16_t wn);

        /** @brief List rows matching @p prefix (null = all), @p params_only
         *  restricting the walk to the parameter block. @p noun names the
         *  command in the empty-result line. */
        void _list_vars(const char* noun, const char* prefix, uint16_t plen,
                        bool params_only);
        /** @brief The detail view for one row. */
        void _show_var(const char* noun, const char* name, uint16_t nlen,
                       bool params_only);
        /** @} */
        /** @brief Report control state, naming every hold still in force. */
        void _report_control(Vars::Bank* io);

        /** @brief `tasks get <name>`: one task's schedule and its cost samples.
         *  @p name is the word slice from the command line, not NUL-terminated. */
        void _task_detail(const Tasks::Registry& reg, const char* name,
                          uint16_t nlen);

        /** @brief `tasks list [<prefix>]`: one line per registered task.
         *
         *  Paced like any other listing, and prefix-filterable, because the
         *  whole table over a 4800-baud console is an index of the TASKS page
         *  rather than an answer. A bare `tasks` still reports the beat's
         *  totals, which is the question a listing cannot answer. */
        /** @brief `tasks stats`: the beat's totals, in `stats`' shape. */
        void _tasks_stats(const Tasks::Registry& reg);
        void _tasks_list(const Tasks::Registry& reg, const char* prefix,
                         uint16_t plen);

        /** @brief Shared tail of reset / exit: drop the field to its safe state
         *  and wipe the screen, since the loop will not run again. */
        void _leave_running();

        /** @brief Emit one variable's row (name, value, unit, and the flags a
         *  bare number hides). Shared by `get` and `list`, so a row reads the
         *  same whichever showed it. */
        void _emit_var(const Vars::Bank& io, Vars::Var h);

        /** @name `get`'s detail rows
         *
         *  Laid out like `stats`: a dotted leader to a fixed column, the value,
         *  then the detail in parentheses.
         *  @{ */
        /** @brief Where the leader ends and " : " begins. 18 fits the longest
         *  label in the tree ("Costliest task", 14) with three dots to spare;
         *  the eleven columns that used to be dots go to the detail, which is
         *  what was being cut to fit an 80-column line. */
        static const size_t LEADER_W = 18;
        /** @brief One "Label ....... : value (detail)" row. @p detail may be
         *  null or empty, which drops the parentheses. */
        void _field(const char* label, const char* value, const char* detail);
        /** @brief A value row: @p v rendered with @p unit, and @p quality as the
         *  detail. @p h supplies the type and, for a T_STR, the pool text. */
        void _value_field(const char* label, const Vars::Bank& io, Vars::Var h,
                          Vars::Scalar v, const char* quality, const char* unit);
        /** @brief What a VarScope abbreviation means, for the detail column. */
        static const char* _scope_phrase(uint8_t scope);
        /** @brief Whether `set` will take @p h. */
        static bool _writable(Vars::Var h);
        /** @brief Where a value physically comes from, into @p buf. */
        static const char* _binding_phrase(const Vars::Binding& b,
                                           char* buf, size_t cap);
        /** @} */

        /** @brief The variables, from the injected SystemContext (null when the
         *  firmware wired none). One lookup point for the variable commands. */
        Vars::Bank* _bank() const { return _sys != nullptr ? _sys->vars : nullptr; }
        /** @brief The task table, or null. Non-const: `programs` writes to it. */
        Tasks::Registry* _registry() const { return _sys != nullptr ? _sys->tasks : nullptr; }
        /** @brief Handle an unrecognised command (echoes "Unknown command:
         *  ..."). */
        void _unknown(const char* cmd, uint16_t len);
        /** @brief Write a NUL-terminated line of command output. */
        void _println(const char* msg);
        /** @brief printf into @p buf (capacity @p cap) and write the result,
         *  clamped to the buffer; empty / encoding-error output is dropped. */
        void _emitf(char* buf, size_t cap, const char* fmt, ...)
            __attribute__((format(printf, 4, 5)));
        /** @brief Emit a dotted-leader "label : value" status line. */
        void _line(const char* label, const char* val);
        /** @} */

        /** @} */
    };

}}

#endif // SHELL_SHELL_H
