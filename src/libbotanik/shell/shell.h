/**
 * @file shell.h
 * @brief Command shell — dispatch, built-in commands, and extensibility hooks.
 */
#ifndef SHELL_SHELL_H
#define SHELL_SHELL_H

#include <cstddef>
#include <cstdint>

#include "libbotanik/terminal/terminal.h"
#include "libbotanik/terminal/editor.h"

namespace Botanik { namespace Shell {

    /**
     * @brief Minimal write-only interface so CommandShell can emit output
     *        without depending on the Pages layer.
     */
    struct ShellWriter {
        virtual size_t write(const char* buf, size_t len) = 0;
        virtual void   clear() {}
        /** @brief Write @p buf split into terminal-width chunks, one write() per chunk. */
        virtual void   write_lines(const char* buf, uint16_t len) { write(buf, len); }
    };

    /**
     * @class CommandShell
     * @brief Handles command submission, built-in dispatch, and extensibility.
     *
     * Subclass and override dispatch() to add application-specific commands
     * that are tried before the built-in handler.
     *
     * History and kill-ring features are stubbed out; see TODO.md and the
     * SHELL_HISTORY_* / SHELL_KILL_SIZE constants in libbotanik_config.h.
     */
    class CommandShell {
    public:
        CommandShell(Terminal::AnsiTerminal& terminal,
                     Terminal::TextEditor&  editor,
                     ShellWriter&            output);

        /** @brief Dispatch a pre-snapshotted command (caller owns snapshot and reset).
         *         A zero-length command is ignored. */
        void run(const char* cmd, uint16_t len);

        /** @brief Load previous history entry — stub, not yet implemented */
        void history_prev() {}

        /** @brief Load next history entry — stub, not yet implemented */
        void history_next() {}

        /** @brief Push text onto the kill ring — stub, not yet implemented */
        void push_kill(const char*, uint16_t) {}

        /** @brief Insert kill ring contents into the editor — stub, not yet implemented */
        void paste_kill() {}

    protected:
        /**
         * @brief Override to handle application-specific commands.
         *        Default implementation calls _builtin().
         */
        virtual void dispatch(const char* cmd, uint16_t len);

    private:
        Terminal::AnsiTerminal& _terminal;
        Terminal::TextEditor&  _editor;
        ShellWriter&            _output;

        void _builtin(const char* cmd, uint16_t len);
    };

}}

#endif // SHELL_SHELL_H
