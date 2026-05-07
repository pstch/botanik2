#include <algorithm>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/shell/shell.h"

namespace Botanik { namespace Shell {

    CommandShell::CommandShell(Terminal::AnsiTerminal& terminal,
                               Terminal::InputEditor&  editor,
                               ShellWriter&            output)
        : _terminal(terminal), _editor(editor), _output(output)
    {}

    void CommandShell::submit() {
        char     cmd[Config::SHELL_HISTORY_ENTRY];
        uint16_t len = std::min(_editor.buffer_len(), (uint16_t)(sizeof(cmd) - 1));
        memcpy(cmd, _editor.buffer(), len);
        cmd[len] = '\0';

        _editor.reset();

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
            n = snprintf(out, sizeof(out), "Commands: help, clear");
        } else if (len == 5 && strncmp(cmd, "clear", 5) == 0) {
            for (uint16_t row = 1; row <= _terminal.rows() - 3; row++)
                _terminal.clear_row(row);
            return;
        } else {
            n = snprintf(out, sizeof(out), "Unknown command: %.*s", (int)len, cmd);
        }

        if (n > 0)
            _output.write(out, (size_t)n);
    }

}}
