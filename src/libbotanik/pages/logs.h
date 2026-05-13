#ifndef PAGES_LOGS_H
#define PAGES_LOGS_H

#include "libbotanik/pages/page.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik/terminal/terminal.h"

namespace Botanik { namespace Pages {

    /**
     * @brief Full-screen log viewer page.
     *
     * Implements LogWriter so it can be registered with a Logger.
     * Header occupies row 0; all remaining rows scroll as a live log tail.
     * Incoming entries are queued by BufferedLogWriter and flushed on each
     * tick() call.  Each entry is rendered by scrolling row 1 upward
     * (DL at row 1) and writing the formatted line at the last row.
     */
    class LogsPage : public Page, public Logs::BufferedLogWriter {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using LogEntry_t   = PlcIO::LogEntry_t;
        using Command      = Terminal::InputEvent::Command;

        AnsiTerminal& _terminal;

        uint16_t _scroll_offset = 0;  // not yet used; placeholder for scrollback

        // BufferedLogWriter interface
        size_t format(const LogEntry_t* entry, char* buf, size_t len) override;
        size_t write(const char* buf, size_t len) override;

    public:
        LogsPage(Terminal::AnsiTerminal& terminal);

        // Page interface
        const char* name() const override { return "LOGS"; }
        void handle_text(char c) override;
        void handle_command(Command cmd) override;
        void deactivate() override;
        void focus() override;
        void tick() override;
    };

}}

#endif // PAGES_LOGS_H
