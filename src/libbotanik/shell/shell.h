/**
 * @file shell.h
 * @brief Command shell - dispatch, built-in commands, and extensibility hooks.
 */
#ifndef SHELL_SHELL_H
#define SHELL_SHELL_H

#include <cstddef>
#include <cstdint>

#include "libbotanik/terminal/terminal.h"
#include "libbotanik/terminal/editor.h"

namespace Botanik { namespace Shell {

    /** @class ShellWriter
     *  @brief Minimal write-only interface so CommandShell can emit output
     *         without depending on the Pages layer. */
    struct ShellWriter {
        /** @brief Write @p len bytes of output; returns the number consumed. */
        virtual size_t write(const char* buf, size_t len) = 0;
        /** @brief Clear all previously written output. Default: no-op. */
        virtual void   clear() {}
        /** @brief Enable/disable live log output in the terminal. Default: no-op. */
        virtual void   set_logs_enabled(bool) {}
        /** @brief Whether live log output is currently enabled. Default: true. */
        virtual bool   logs_enabled() const { return true; }
        /** @brief Write @p buf split into terminal-width chunks, one write() per chunk. */
        virtual void   write_lines(const char* buf, uint16_t len) { write(buf, len); }
    };

    /** @brief Site-defined shell command: a name, one-line help text, and a
     *  callback. Registered as a table via CommandShell::set_commands() and tried
     *  after the built-ins (so a site can't shadow exit/reset). The name may be
     *  followed by arguments: the remainder of the line after the name (leading
     *  spaces trimmed) is handed to the callback as @p args / @p arglen, and @p ctx
     *  is the opaque pointer passed to set_commands(). */
    struct ShellCommand {
        const char* name;
        const char* help;
        void (*fn)(ShellWriter& out, const char* args, uint16_t arglen, void* ctx);
    };

    /** @class CommandShell
     *  @brief Handles command submission, built-in dispatch, and extensibility.
     *
     *  Site commands are added data-driven via set_commands() (a table of
     *  ShellCommand); alternatively, subclass and override dispatch() for commands
     *  that need richer control. Both are tried after the built-ins.
     *
     *  History and kill-ring features are stubbed out; see TODO.md and the
     *  SHELL_HISTORY_* / SHELL_KILL_SIZE constants in libbotanik_config.h. */
    class CommandShell {
    private:
        /** @name State
         *  @{ */

        /** @brief Terminal the shell renders through. */
        Terminal::AnsiTerminal& _terminal;
        /** @brief Editor supplying the input line. */
        Terminal::TextEditor&  _editor;
        /** @brief Output channel for command results. */
        ShellWriter&            _output;
        /** @brief Optional site command table (see set_commands()). */
        const ShellCommand*     _commands      = nullptr;
        /** @brief Number of entries in _commands. */
        size_t                  _command_count = 0;
        /** @brief Opaque context handed to each site command callback. */
        void*                   _command_ctx   = nullptr;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        CommandShell(Terminal::AnsiTerminal& terminal,
                     Terminal::TextEditor&  editor,
                     ShellWriter&            output);

        /** @} */

        /** @name Command execution
         *  @{ */

        /** @brief Dispatch a pre-snapshotted command (caller owns snapshot and reset).
         *         A zero-length command is ignored. */
        void run(const char* cmd, uint16_t len);

        /** @brief Register a site command table, tried after the built-ins. @p table
         *  must outlive the shell (typically a static at the site). @p ctx is passed
         *  to each callback. Pass count 0 (or a null table) to clear. */
        void set_commands(const ShellCommand* table, size_t count, void* ctx = nullptr) {
            // Cap defensively so a bogus count can't walk off the table. A site name that
            // collides with a built-in is silently dead (built-ins match first, by design,
            // so a site can't shadow exit/reset).
            if (count > Config::SHELL_COMMANDS_MAX) count = Config::SHELL_COMMANDS_MAX;
            _commands      = table;
            _command_count = (table != nullptr) ? count : 0;
            _command_ctx   = ctx;
        }

        /** @} */

        /** @name History management
         *  @{ */

        /** @brief Load previous history entry - stub, not yet implemented */
        void history_prev() {}

        /** @brief Load next history entry - stub, not yet implemented */
        void history_next() {}

        /** @} */

        /** @name Kill-ring (clipboard) management
         *  @{ */

        /** @brief Push text onto the kill ring - stub, not yet implemented */
        void push_kill(const char*, uint16_t) {}

        /** @brief Insert kill ring contents into the editor - stub, not yet implemented */
        void paste_kill() {}

        /** @} */

    protected:
        /** @name Extensibility
         *  @{ */

        /** @brief Override to handle application-specific commands.
         *        Default implementation calls _builtin(). */
        virtual void dispatch(const char* cmd, uint16_t len);

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Built-in command handler (run when dispatch() falls through):
         *  matches @p cmd's first word against the built-in table and invokes its handler
         *  with the argument slice. */
        void _builtin(const char* cmd, uint16_t len);

        /** @brief One built-in: name, one-line help, handler taking the argument slice. */
        struct Builtin { const char* name; const char* help;
                         void (CommandShell::*fn)(const char* args, uint16_t alen); };
        /** @brief The built-in table - the single source for both dispatch and `help`,
         *  so the help text can never drift from the command set. */
        static const Builtin* _builtins(uint8_t& count);

        /** @name Built-in command handlers
         *  Each takes the (already leading-space-trimmed) argument slice after the
         *  command word; handlers that take no args ignore it.
         *  @{ */
        void _cmd_help(const char* args, uint16_t alen);
        void _cmd_clear(const char* args, uint16_t alen);
        void _cmd_logs(const char* args, uint16_t alen);
        void _cmd_breakpoint(const char* args, uint16_t alen);
        void _cmd_reset(const char* args, uint16_t alen);
        void _cmd_exit(const char* args, uint16_t alen);
        void _cmd_stats(const char* args, uint16_t alen);
        void _cmd_version(const char* args, uint16_t alen);
        void _cmd_alarms(const char* args, uint16_t alen);
        void _cmd_uptime(const char* args, uint16_t alen);
        void _cmd_plc(const char* args, uint16_t alen);
        /** @brief Handle an unrecognised command (echoes "Unknown command: ..."). */
        void _unknown(const char* cmd, uint16_t len);
        /** @brief Write a NUL-terminated line of command output. */
        void _println(const char* msg);
        /** @} */

        /** @} */
    };

}}

#endif // SHELL_SHELL_H
