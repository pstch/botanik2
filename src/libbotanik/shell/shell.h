/**
 * @file shell.h
 * @brief Command shell - dispatch, built-in commands, and extensibility hooks.
 */
#ifndef SHELL_SHELL_H
#define SHELL_SHELL_H

#include <cstddef>
#include <cstdint>

#include "libbotanik/console/editor.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/core/context.h"
#include "libbotanik/vars/access.h"
#include "libbotanik/vars/persist.h"
#include "libbotanik/vars/rows.h"

namespace Botanik {
    // logs command (set_diagnostics)
    namespace Logs    { class Logger;     }
    // `log <level>`: declared with its underlying type, which C++11 allows, so
    // this header does not acquire logs/topics.h to name one level.
    namespace Logs    { enum LogLevel : uint8_t; }
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
         *  A non-null @c usage marks a NOUN: `_dispatch()` answers a bare
         *  `<noun>` or `<noun> help` with it rather than calling the handler.
         *  Null for a command with no verbs.
         *
         *  @c group keys the HelpGroup `help <group>` filters on. Per row, so
         *  reordering the table cannot put a row in the wrong group. */
        /** @brief One verb of a noun: its usage line, and the member that
         *  runs it.
         *
         *  The LINE is the spelling. What a command matches is the word up to
         *  the first space; what `help` prints is the whole line. One row, so a
         *  verb cannot be listed and unreachable, or reachable and
         *  undocumented. @c fn is handed the arguments after its own word, and
         *  is null for a verb whose noun still parses it by hand. */
        struct Verb {
            const char* usage;
            void (CommandShell::*fn)(const char* args, uint16_t alen);
        };

        struct Builtin {
            const char* name;
            /** @brief Compact argument spelling for a one-line listing; null
             *  when the command takes none. */
            const char* args;
            const char* help;      ///< the one line a listing shows
            /** @brief This command's verb table, or null for a command with
             *  none - and its presence is what makes a bare call answer usage
             *  instead of acting. */
            const Verb* (*verbs)(uint8_t& count);
            const char* group;     ///< topic key, for `help <topic>`
            bool        main;      ///< also listed under "Main commands"
            void (CommandShell::*fn)(const char* args, uint16_t alen);
        };

        /** @brief Take the next word off @p args and run the verb of @p verbs
         *  that spells it. False when nothing matched, which leaves @p args
         *  where it was so the caller can still answer usage.
         *  @note A row with a null @c fn never matches: it is documentation
         *  for a verb its noun is still parsing by hand. */
        bool _run_verb(const Verb* verbs, uint8_t count,
                       const char*& args, uint16_t& alen);

        /** @name The verb tables, each defined beside the handlers it names
         *  @{ */
        static const Verb* _logs_verbs(uint8_t& count);
        static const Verb* _log_verbs(uint8_t& count);
        static const Verb* _modbus_verbs(uint8_t& count);
        static const Verb* _tasks_verbs(uint8_t& count);
        static const Verb* _params_verbs(uint8_t& count);
        static const Verb* _overrides_verbs(uint8_t& count);
        static const Verb* _control_verbs(uint8_t& count);
        static const Verb* _programs_verbs(uint8_t& count);
        static const Verb* _alarms_verbs(uint8_t& count);
        static const Verb* _vars_verbs(uint8_t& count);
        /** @} */
        /** @brief The built-in table - single source for both dispatch and
         *  `help`, so help text can't drift from the command set. */
        static const Builtin* _builtins(uint8_t& count);
        /** @brief The row @p name names, or null. */
        static const Builtin* _noun(const char* name);

        /** @brief One help topic: the word `help <topic>` takes, and the line
         *  the bare `help` page shows for it. */
        struct HelpGroup { const char* key; const char* help; };
        /** @brief The topics, in the order `help` lists them. */
        static const HelpGroup* _help_groups(uint8_t& count);
        /** @brief List the commands in topic @p key. @return false if no topic
         *  matches, so the caller can say so and print the help page. */
        bool _help_group(const char* key, uint16_t klen);

        /** @brief Whether @p args is the literal `confirm`, saying what to type
         *  and what it costs when it is not. The four lifecycle verbs are the
         *  only callers: each stops or restarts the unit and none can be taken
         *  back. Stateless on purpose - a pending flag would have to survive
         *  Ctrl-C, a page switch and a dropped link.
         *  @param what the consequence, not the syntax: it is the half of the
         *  refusal an operator has not already worked out. */
        bool _confirmed(const char* args, uint16_t alen,
                        const char* verb, const char* what);

        /** @brief A blank separator line. */
        void _blank();
        /** @brief A section rule: "-- Title ------...", out to the screen. */
        void _rule(const char* title);
        /** @brief One "  left : right" row, @p lw wide in the left column and
         *  cut to the screen. */
        void _help_row(const char* left, size_t lw, const char* right);
        /** @brief Widest "name args" across the commands in topic @p key
         *  (null = the main list), so a section's colons line up. */
        size_t _row_width(const char* key, uint16_t klen) const;
        /** @brief "name args" into @p buf; the left column of a listing row. */
        static size_t _label(const Builtin& b, char* buf, size_t cap);

        /** @brief Print @p noun's usage line, looked up in the built-in table so
         *  the text has ONE home: `help`, a bare noun and a mistyped verb all
         *  reach the same string. */
        void _verb_usage(const char* noun);
        /** @brief One verb's line from the same table, so a usage message
         *  cannot drift from the help page. Falls back to the whole list when
         *  @p verb is not in it. */
        void _verb_usage(const char* noun, const char* verb);

        /** @brief Append a submitted line to the history ring (skips a
         *  consecutive duplicate) and ends any in-progress browsing. */
        void _history_add(const char* cmd, uint16_t len);
        /** @brief Replace the editor line with history slot @c _hist_browse (or
         *  the draft when _hist_browse < 0). */
        void _load_browse();

        /** @} */

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
        /** @name `logs` verbs @{ */
        void _logs_on(const char* args, uint16_t alen);
        void _logs_off(const char* args, uint16_t alen);
        void _logs_status(const char* args, uint16_t alen);
        /** @} */
        /** @brief `log <level> <text>` - an operator's own entry, LogTopic::USER. */
        void _cmd_log(const char* args, uint16_t alen);
        /** @name `log` verbs - one per level, all onto _log_at()
         *  @{ */
        void _log_debug(const char* args, uint16_t alen);
        void _log_info(const char* args, uint16_t alen);
        void _log_notice(const char* args, uint16_t alen);
        void _log_warning(const char* args, uint16_t alen);
        void _log_error(const char* args, uint16_t alen);
        void _log_critical(const char* args, uint16_t alen);
        /** @brief Write the rest of the line to the log at @p level. */
        void _log_at(Logs::LogLevel level, const char* args, uint16_t alen);
        /** @} */
        void _cmd_breakpoint(const char* args, uint16_t alen);
        void _cmd_reset(const char* args, uint16_t alen);
        /** @brief Drop control authority, then stop kicking the watchdog and let
         *  it reset the unit - the unattended reset path, by hand. */
        void _cmd_reboot(const char* args, uint16_t alen);
        void _cmd_halt(const char* args, uint16_t alen);
        void _cmd_exit(const char* args, uint16_t alen);
        void _cmd_stats(const char* args, uint16_t alen);
        /** @brief The beat's totals, or one task: where `stats`' unattributed
         *  CPU figure went. */
        void _cmd_tasks(const char* args, uint16_t alen);
        /** @name `tasks` verbs @{ */
        void _tasks_stats(const char* args, uint16_t alen);
        void _tasks_list(const char* args, uint16_t alen);
        void _tasks_get(const char* args, uint16_t alen);
        /** @brief The registry, or null with the reason already printed. */
        const Tasks::Registry* _registry();
        /** @} */
        void _cmd_version(const char* args, uint16_t alen);
        void _cmd_features(const char* args, uint16_t alen);
        void _cmd_alarms(const char* args, uint16_t alen);
        /** @name `alarms` verbs, dispatched from _alarms_verbs()
         *  @{ */
        void _alarms_list(const char* args, uint16_t alen);
        void _alarms_get(const char* args, uint16_t alen);
        void _alarms_ack(const char* args, uint16_t alen);
        void _alarms_unack(const char* args, uint16_t alen);
        /** @brief The body both spellings share. @p ack picks the direction. */
        void _ack(const char* args, uint16_t alen, bool ack);
        /** @} */
        /** @brief `modbus stats` is the plan cost and the master state,
         *  `modbus devices` the roll-call. */
        void _cmd_modbus(const char* args, uint16_t alen);
        /** @name `modbus` verbs @{ */
        void _modbus_stats(const char* args, uint16_t alen);
        void _modbus_devices(const char* args, uint16_t alen);
        /** @} */
        #if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        /** @brief `modbus devices`: one line per unit - tag, address, model and
         *  whether it is answering. No counters. */
        void _show_modbus_devices(const Modbus::RtuMaster& rtu);
        #endif
        void _cmd_net(const char* args, uint16_t alen);
        void _cmd_ntpsync(const char* args, uint16_t alen);
        /** @brief `params list|get|set|clear|save` - the editable rows. */
        void _cmd_params(const char* args, uint16_t alen);
        /** @brief `programs list|start|stop|enable|disable|status` - the
         *  control programs. */
        void _cmd_programs(const char* args, uint16_t alen);
        /** @brief `control start|stop|enable|disable|status` - sys_control, the
         *  operator's standing choice about whether the laws drive at all. */
        void _cmd_control(const char* args, uint16_t alen);
        /** @brief `overrides list|get|set|clear|save` - the forced rows. */
        void _cmd_overrides(const char* args, uint16_t alen);
        /** @brief `vars list|get|info` - every row, whatever its scope. */
        void _cmd_vars(const char* args, uint16_t alen);

        /** @} */

        /** @name Editing verbs
         *
         *  Editing and committing are two commands because they are two
         *  decisions: a value is live the moment it is set, and durable only
         *  once saved. @see Vars::Var::param_set()
         *  @{ */
        /** @brief `params set <name> <value>`. @p nm / @p wn are the name word;
         *  @p args / @p alen carry the value and are consumed here. */
        void _params_set(const char* nm, uint16_t wn,
                         const char* args, uint16_t alen);
        /** @brief `params save <name>|all` - one row, or every unsaved one. */
        void _params_save(const char* nm, uint16_t wn);
        /** @brief `params clear <name>|all` - back to the factory default. */
        void _params_clear(const char* nm, uint16_t wn);
        /** @brief `params reset`: back to the FACTORY default, stored copy and
         *  all. The destructive sibling of _params_clear(), and it skips a
         *  RETAINED parameter (@see Vars::retained_param). */
        void _params_reset(const char* nm, uint16_t wn);
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
        /** @brief Force @p r's `_qlt` companion. False when it has none -
         *  a parameter, or a row no law qualifies. */
        bool _force_quality(uint16_t r, bool valid);
        /** @brief `programs start|stop|enable|disable <name>|all`.
         *  @param on      whether the programs @p nm globs are to drive
         *  @param persist false leaves the edit unsaved, so a reboot undoes it */
        void _programs_gate(const char* nm, uint16_t wn, bool on, bool persist);
        /** @brief `programs status`: a tally per health, and who is in the ones
         *  worth naming. */
        void _programs_status();
        void _control_set(bool on, bool persist);
        void _control_status();
        /** @} */

        /** @name Shared verb bodies
         *
         *  `params get` and `vars info` are the same detail view over a
         *  different admission rule, and both list forms are one filtered walk.
         *  Written once, so a row reads the same whichever noun showed it.
         *  @{ */
        /** @brief Resolve @p nm to a row, printing "<noun>: unknown variable"
         *  when it names none. The lookup every table verb starts with, so the
         *  message cannot drift between them.
         *  @return its ref, or one whose index is Vars::VAR_NONE. */
        uint16_t _resolve(const char* noun, const char* nm, uint16_t wn);

        /** @brief List rows matching @p prefix (null = all), @p params_only
         *  restricting the walk to the parameter block. @p noun names the
         *  command in the empty-result line. */
        void _list_vars(const char* noun, const char* prefix, uint16_t plen,
                        bool params_only);
        /** @brief The detail view for one row. @p verb is the one that reached
         *  here, since the nouns spell it differently (`vars info`, `params
         *  get`) and the usage line has to name the caller's. */
        void _show_var(const char* noun, const char* verb, const char* name,
                       uint16_t nlen, bool params_only);
        /** @brief `vars get`: the live value on one line, and nothing else. */
        void _show_value(const char* noun, const char* name, uint16_t nlen);
        /** @} */

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
        void _show_tasks_list(const Tasks::Registry& reg, const char* prefix,
                         uint16_t plen);
        /** @brief `tasks stats`: the beat's totals, in `stats`' shape. */
        void _show_tasks_stats(const Tasks::Registry& reg);

        /** @brief Shared tail of reset / exit: drop the field to its safe state
         *  and wipe the screen, since the loop will not run again. */
        void _leave_running();

        /** @brief Emit one variable's row (name, value, unit, and the flags a
         *  bare number hides). Shared by `get` and `list`, so a row reads the
         *  same whichever showed it. */
        void _emit_var(Vars::Var h);

        /** @name `get`'s detail rows
         *
         *  Laid out like `stats`: a dotted leader to a fixed column, the value,
         *  then the detail in parentheses.
         *  @{ */
        /** @brief Where the leader ends and " : " begins. 18 fits the longest
         *  label in the tree ("Costliest task", 14) with three dots to spare.
         *  Every column past that belongs to the detail, which is what an
         *  80-column line cuts first. */
        static const size_t LEADER_W = 18;
        /** @brief One "Label ....... : value (detail)" row. @p detail may be
         *  null or empty, which drops the parentheses. */
        void _field(const char* label, const char* value, const char* detail);
        /** @brief A value row: @p v rendered with @p unit, and @p quality as the
         *  detail. @p h supplies the type and, for a T_STR, the pool text. */
        void _value_field(const char* label, Vars::Var h, Vars::Value val,
                          const char* quality, const char* unit);
        /** @brief What a Kind means, for the detail column. */
        static const char* _kind_phrase(const Vars::Descriptor& d);
        /** @brief What `def` is FOR on this row - it does three different jobs,
         *  and the number alone does not say which. Takes the descriptor, not
         *  the role: a retained parameter's default is one nothing restores. */
        static const char* _default_phrase(const Vars::Descriptor& d);
        /** @} */

        /** @name Context and output
         *  What every handler reaches through: the injected pointers, and the
         *  one place a line goes to the wire. @{ */

        /** @brief The variables, from the injected SystemContext (null when the
         *  firmware wired none). One lookup point for the variable commands. */
        Vars::Bank* _bank() const { return _sys != nullptr ? _sys->vars : nullptr; }
        /** @brief What the durable verbs go through; null in a rig with no
         *  store, where every one of them is a no-op. */
        Vars::Persist* _store() const {
            return _sys != nullptr ? _sys->persist : nullptr;
        }
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
    };

}}

#endif // SHELL_SHELL_H
