#ifndef PAGES_LOGS_H
#define PAGES_LOGS_H

/**
 * @file logs.h
 * @brief LogsPage - full-screen live log viewer backed by a circular scrollback.
 */

#include "libbotanik_config.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/pages/scrollback.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik/terminal/terminal.h"

namespace Botanik { namespace Pages {

    /**
     * @brief Full-screen log viewer page.
     *
     * A LogWriter: rows 1..rows-1 scroll as a live log tail. Incoming entries are
     * queued by BufferedLogWriter and drained on tick(); history is replayed from a
     * circular scrollback on (re)focus.
     */
    class LogsPage : public Page, public Logs::BufferedLogWriter {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using LogEntry_t   = PlcIO::LogEntry_t;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Owning terminal. */
        AnsiTerminal& _terminal;
        /** @brief Circular log line buffer + throttled redraw for page-switch replay. */
        LogsScrollback _scrollback;
        /** @brief Mirrors tick(active) so write() knows whether it may paint. */
        bool _active = false;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        LogsPage(Terminal::AnsiTerminal& terminal);

        /** @} */

        /** @name Page interface
         *  @{ */

        const char* name() const override { return "LOGS"; }
        void handle_text(char c) override;
        void handle_command(Command cmd, uint16_t count = 1) override;
        void deactivate() override;
        void focus() override;
        void tick(bool active) override;

        /** @} */

    private:
        /** @name BufferedLogWriter interface
         *  @{ */

        size_t format(const LogEntry_t* entry, char* buf, size_t len) override;
        size_t write(const char* buf, size_t len) override;

        /** @} */

        /** @name Internals
         *  @{ */

        /** @brief Store a timestamped "--- MARK ---" line (SPACE), painted by the next
         *         drain_tail. */
        void _emit_mark();

        /** @} */
    };

}}

#endif // PAGES_LOGS_H
