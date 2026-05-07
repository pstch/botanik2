#ifndef PAGES_SHELL_H
#define PAGES_SHELL_H

#include "libbotanik/pages/page.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik/terminal/editor.h"
#include "libbotanik/shell/shell.h"

namespace Botanik { namespace Pages {

    /**
     * @brief Interactive shell page with interleaved log output.
     *
     * Layout (N = terminal rows):
     *   Row 0       — inverted header
     *   Rows 1..N-3 — log area (live tail, scrolls on each incoming entry)
     *   Rows N-2..N-1 — InputEditor (PS1 + optional continuation row)
     *
     * Implements LogWriter so it can be registered with a Logger.
     * Incoming log entries are queued by BufferedLogWriter and flushed on each
     * tick(). Each entry scrolls the log area (DL at row 1), writes the new
     * line at row N-3, then redraws the editor in place.
     *
     * Implements ShellWriter to provide CommandShell with an output channel.
     */
    class ShellPage : public Page, public Logs::BufferedLogWriter, public Shell::ShellWriter {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using InputEditor  = Terminal::InputEditor;
        using LogEntry_t   = PlcIO::LogEntry_t;
        using Command      = Terminal::InputEvent::Command;

        AnsiTerminal&      _terminal;
        InputEditor        _editor;
        Shell::CommandShell _shell;

        void _draw_header();

        // BufferedLogWriter + ShellWriter interface (one impl serves both)
        size_t format(const LogEntry_t* entry, char* buf, size_t len) override;
        size_t write(const char* buf, size_t len) override;

    public:
        ShellPage(Terminal::AnsiTerminal& terminal);

        // Page interface
        void handle_text(char c) override;
        void handle_command(Command cmd) override;
        void deactivate() override;
        void focus() override;
        void tick() override;
    };

}}

#endif // PAGES_SHELL_H
