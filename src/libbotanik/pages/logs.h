#ifndef PAGES_LOGS_H
#define PAGES_LOGS_H

#include "libbotanik_config.h"
#include "libbotanik/pages/page.h"
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

        // Circular log line buffer for redraw/page-switch replay
        char     _log_buf[Config::SHELL_SCROLLBACK_LINES][Config::SHELL_SCROLLBACK_LINE_LEN];
        uint16_t _log_lens[Config::SHELL_SCROLLBACK_LINES] = {};
        uint16_t _log_head  = 0;
        uint16_t _log_count = 0;

        // Deferred log redraw state (driven by tick() to avoid TX buffer overflow)
        uint16_t _redraw_i         = UINT16_MAX;  // UINT16_MAX = not pending
        uint16_t _redraw_count     = 0;
        uint16_t _redraw_start     = 0;
        uint16_t _redraw_first_row = 0;

        void _store_line(const char* buf, size_t len);
        void _redraw_log();
        void _drain_redraw_row();

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
