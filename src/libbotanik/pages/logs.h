#ifndef PAGES_LOGS_H
#define PAGES_LOGS_H

#include "libbotanik_config.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/pages/scrollback.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik/terminal/terminal.h"

namespace Botanik { namespace Pages {

    /**
     * @brief Full-screen log viewer page.
     *
     * Implements LogWriter so it can be registered with a Logger.
     * Header occupies row 0; rows 1..rows-1 scroll as a live log tail.
     * Incoming entries are queued by BufferedLogWriter and drained on each
     * tick().  Historical entries are replayed from a circular scrollback
     * buffer whenever the page is (re)focused.
     */
    class LogsPage : public Page, public Logs::BufferedLogWriter {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using LogEntry_t   = PlcIO::LogEntry_t;
        using Command      = Terminal::InputEvent::Command;

        AnsiTerminal& _terminal;

        // Circular log line buffer + throttled redraw for page-switch replay
        Scrollback _scrollback;

        // Mirrors the latest tick(active) argument so write() — reached indirectly
        // via drain() — knows whether it may paint. False while in the background.
        bool _active = false;

        // BufferedLogWriter interface
        size_t format(const LogEntry_t* entry, char* buf, size_t len) override;
        size_t write(const char* buf, size_t len) override;

        /** @brief Scroll the tail up one row and write a line at the bottom,
         *         storing it in the scrollback. Shared by write() and the
         *         manual SPACE marker. */
        void _emit_line(const char* buf, size_t len);

        /** @brief Emit a timestamped "--- MARK ---" line into the log tail,
         *         using the same format as log entries. Triggered by SPACE. */
        void _emit_mark();

    public:
        LogsPage(Terminal::AnsiTerminal& terminal);

        // Page interface
        const char* name() const override { return "LOGS"; }
        void handle_text(char c) override;
        void handle_command(Command cmd, uint16_t count = 1) override;
        void deactivate() override;
        void focus() override;
        void tick(bool active) override;
    };

}}

#endif // PAGES_LOGS_H
