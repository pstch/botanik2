// REVIEW_35: TODO
/**
 * @file shell.h
 * @brief Command shell - dispatch, built-in commands, and extensibility hooks.
 */
#ifndef SHELL_SHELL_H
#define SHELL_SHELL_H

#include <cstddef>                    // size_t - the command and history buffer capacities
#include <cstdint>                    // uint8_t/uint16_t/int16_t - history cursor and kill-ring slots

#include "libbotanik/console/console.h" // Console::alarms / context - what the built-ins read
#include "libbotanik/console/editor.h" // Terminal::TextEditor - the prompt the shell drives
#include "libbotanik/console/terminal.h" // AnsiTerminal - writing the command output
#include "libbotanik/vars/access.h"   // Vars::Bank / var_group - `list` and `get`
#include "libbotanik/vars/params.h"   // Vars::Params - the `set` command

namespace Botanik {
    // loglevel command (set_diagnostics)
    namespace Logs    { class Logger;     }
    // alarm list/ack command (set_diagnostics)
    namespace Alarms  { class AlarmController; }
    // ntpsync command (set_ntp); null when the NTP feature is off
    namespace Network { class NtpClient;  }
    // get / set / override commands: the variables come from SystemContext.
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

    /** @brief Site-defined shell command: a name, one-line help text, and a
     *  callback. Registered as a table via CommandShell::set_commands() and
     *  tried before the built-ins, so the table can override one. The name
     *  may be followed by arguments: the remainder of the line after the name
     *  (leading spaces trimmed) is handed to the callback as @p args /
     *  @p arglen, and @p ctx is the opaque pointer passed to set_commands(). */
    struct ShellCommand {
        const char* name;
        const char* help;
        void (*fn)(ShellWriter& out, const char* args, uint16_t arglen, void* ctx);
    };

    /** @class CommandShell
     *  @brief Handles command submission, built-in dispatch, and extensibility.
     *
     *  Site commands are added data-driven via set_commands() (a table of
     *  ShellCommand), tried before the built-ins so the table can override one.
     *
     *  Command history (up/down recall with a saved draft) and a single-entry
     *  kill ring (Ctrl-K cut to EOL / Ctrl-Y yank) are backed by the
     *  SHELL_HISTORY_* /
     *  SHELL_KILL_SIZE buffers in libbotanik_config.h. */
    class CommandShell {
    private:
        /** @name State
         *  @{ */

        /** console the shell reads context/alarms from */
        Terminal::Console&      _console;
        /** VT device the shell renders through (cached _console.terminal()) */
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
        /** @brief Mutable diagnostics targets for the loglevel / alarm
         *  commands; null until set_diagnostics() wires them (commands then
         *  report "unavailable"). */
        Logs::Logger*           _logger        = nullptr;
        Alarms::AlarmController*     _alarm_model   = nullptr;
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
        /** @brief Single-entry kill buffer (Ctrl-K cut / Ctrl-Y yank). */
        char     _kill[Config::SHELL_KILL_SIZE];
        uint16_t _kill_len    = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        CommandShell(Terminal::Console&      console,
                     Terminal::TextEditor&  editor,
                     ShellWriter&            output);

        /** @} */

        /** @name Command execution
         *  @{ */

        /** @brief Dispatch a pre-snapshotted command (caller owns snapshot and
         *  reset). A zero-length command is ignored. */
        void run(const char* cmd, uint16_t len);

        /** @brief Wire the mutable diagnostics targets the loglevel / alarm-ack
         *  commands act on (the firmware passes its logger and alarm model). */
        void set_diagnostics(Logs::Logger* logger, Alarms::AlarmController* alarms) {
            _logger = logger; _alarm_model = alarms;
        }

        /** @brief Wire the NTP client the `ntpsync` command resyncs (the
         *  firmware passes it when the NTP feature is on; left null otherwise,
         *  so the command reports "unavailable"). */
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

        /** @name Kill-ring (clipboard) management
         *  @{ */

        /** @brief Cut from the cursor to EOL into the kill buffer (Ctrl-K). */
        void kill_to_line_end();

        /** @brief Store @p len bytes as the kill buffer (clamped to
         *  SHELL_KILL_SIZE). */
        void push_kill(const char* buf, uint16_t len);

        /** @brief Insert the kill buffer at the cursor (Ctrl-Y). */
        void paste_kill();

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Match the first word against the site command table (which
         *  may override a built-in) then the built-in table, and run the
         *  handler. */
        void _dispatch(const char* cmd, uint16_t len);

        /** @brief One built-in: name, one-line help, handler taking the
         *  args. */
        struct Builtin { const char* name; const char* help;
                         void (CommandShell::*fn)(const char* args, uint16_t alen); };
        /** @brief The built-in table - single source for both dispatch and
         *  `help`, so help text can't drift from the command set. */
        static const Builtin* _builtins(uint8_t& count);

        /** @brief Append a submitted line to the history ring (skips a
         *  consecutive duplicate) and ends any in-progress browsing. */
        void _history_add(const char* cmd, uint16_t len);
        /** @brief Replace the editor line with history slot @c _hist_browse (or
         *  the draft when _hist_browse < 0). */
        void _load_browse();

        /** @name Built-in command handlers
         *  Each takes the (already leading-space-trimmed) argument slice after
         *  the command word; handlers that take no args ignore it.
         *  @{ */
        void _cmd_help(const char* args, uint16_t alen);
        void _cmd_clear(const char* args, uint16_t alen);
        void _cmd_logs(const char* args, uint16_t alen);
        void _cmd_breakpoint(const char* args, uint16_t alen);
        void _cmd_reset(const char* args, uint16_t alen);
        void _cmd_exit(const char* args, uint16_t alen);
        void _cmd_stats(const char* args, uint16_t alen);
        void _cmd_version(const char* args, uint16_t alen);
        void _cmd_features(const char* args, uint16_t alen);
        void _cmd_alarms(const char* args, uint16_t alen);
        void _cmd_net(const char* args, uint16_t alen);
        void _cmd_ntpsync(const char* args, uint16_t alen);
        void _cmd_loglevel(const char* args, uint16_t alen);
        void _cmd_list(const char* args, uint16_t alen);
        void _cmd_get(const char* args, uint16_t alen);
        void _cmd_set(const char* args, uint16_t alen);
        void _cmd_override(const char* args, uint16_t alen);
        void _cmd_control(const char* args, uint16_t alen);
        /** @brief Report control state, naming every hold still in force. */
        void _report_control(Vars::Bank* io);

        /** @brief Shared tail of reset / exit: drop the field to its safe state
         *  and wipe the screen, since the loop will not run again. */
        void _leave_running();

        /** @brief Emit one variable's row (name, value, unit, and the flags a
         *  bare number hides). Shared by `get` and `list` so the same variable
         *  reads identically whichever showed it. */
        void _emit_var(const Vars::Bank& io, Vars::Var h);

        /** @name `get`'s detail rows
         *
         *  Laid out like `stats`: a dotted leader to a fixed column, the value,
         *  then the detail in parentheses.
         *  @{ */
        /** @brief Where the leader ends and " : " begins. Matches the width
         *  `stats` writes into its own labels, so the two line up on screen. */
        static const size_t LEADER_W = 29;
        /** @brief One "Label ....... : value (detail)" row. @p detail may be
         *  null or empty, which drops the parentheses. */
        void _field(const char* label, const char* value, const char* detail);
        /** @brief A value row: @p v rendered with @p unit, and valid/invalid as
         *  the detail. @p h supplies the type and, for a T_STR, the pool text. */
        void _value_field(const char* label, const Vars::Bank& io, Vars::Var h,
                          Vars::Scalar v, bool valid, const char* unit);
        /** @brief What a VarScope abbreviation means, for the detail column. */
        static const char* _scope_phrase(uint8_t scope);
        /** @brief Whether `set` will take @p h. */
        static bool _writable(Vars::Var h);
        /** @brief Where a value physically comes from, into @p buf. */
        static const char* _binding_phrase(const Vars::Binding& b,
                                           char* buf, size_t cap);
        /** @} */

        /** @brief The variables, from the shared SystemContext (null when the
         *  firmware wired none). One lookup point for the variable commands. */
        Vars::Bank* _bank() const { return _console.context().vars; }
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
