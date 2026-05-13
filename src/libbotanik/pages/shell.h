#ifndef PAGES_SHELL_H
#define PAGES_SHELL_H

#include "libbotanik_config.h"
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
     *   Rows 1..N-2 — log area (live tail, scrolls on each incoming entry)
     *   Row  N-1    — InputEditor (PS1 + optional continuation row)
     *
     * Implements LogWriter so it can be registered with a Logger.
     * Incoming log entries are queued by BufferedLogWriter and flushed on each
     * tick(). Each entry scrolls the log area (DL at row 1), writes the new
     * line at row N-2, then redraws the editor in place.
     *
     * Implements ShellWriter to provide CommandShell with an output channel.
     * All written lines are stored in a circular buffer for replay on redraw
     * and page-switch (focus).
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

        // Circular log line buffer for redraw/page-switch replay
        char     _log_buf[Config::SHELL_SCROLLBACK_LINES][Config::SHELL_SCROLLBACK_LINE_LEN];
        uint16_t _log_lens[Config::SHELL_SCROLLBACK_LINES] = {};
        uint16_t _log_head  = 0;   // next slot to write
        uint16_t _log_count = 0;   // number of stored lines

        // Deferred log redraw state (driven by tick() to avoid TX buffer overflow)
        uint16_t _redraw_i         = UINT16_MAX;  // UINT16_MAX = not pending
        uint16_t _redraw_count     = 0;
        uint16_t _redraw_start     = 0;
        uint16_t _redraw_first_row = 0;

        void _setup_editor();
        void _full_redraw();

        /** @brief Echo each display row of a submitted command to the log area.
         *  @param old_occ  Number of editor rows before reset(); the first
         *                  (old_occ-1) echo lines are written directly into the
         *                  donated blank rows to avoid spurious DL scrolls. */
        void _echo_command(const char* cmd, uint16_t len, uint16_t old_occ);

        /** @brief Store a line in the circular buffer (does not display it). */
        void _store_line(const char* buf, size_t len);

        /** @brief Start a deferred replay of stored log lines; drains one row per tick(). */
        void _redraw_log();

        /** @brief Drain one row of the pending log redraw (called from tick()). */
        void _drain_redraw_row();

        // BufferedLogWriter + ShellWriter interface (one impl serves both)
        size_t format(const LogEntry_t* entry, char* buf, size_t len) override;
        size_t write(const char* buf, size_t len) override;
        void   write_lines(const char* buf, uint16_t len) override;
        void   clear() override;

    public:
        ShellPage(Terminal::AnsiTerminal& terminal);

        // Page interface
        const char* name() const override { return "SHELL"; }
        void handle_text(char c) override;
        void handle_text_batch(const char* buf, uint16_t len) override;
        void handle_command(Command cmd) override;
        void deactivate() override;
        void focus() override;
        void redraw() override;
        void tick() override;
    };

}}

#endif // PAGES_SHELL_H
