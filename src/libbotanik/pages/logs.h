// REVIEW_35: TODO
#ifndef PAGES_LOGS_H
#define PAGES_LOGS_H

/**
 * @file logs.h
 * @brief LogsPage - full-screen live log viewer backed by a circular
 *        scrollback.
 */

#include "../../libbotanik_config.h"
#include "page.h"
#include "scrollback.h"
#include "../logs/writer.h"
#include "../console/console.h"
#include "../console/terminal.h"

namespace Botanik { namespace Pages {

    /**
     * @brief Full-screen log viewer page.
     *
     * A LogWriter: rows 1..rows-1 scroll as a live log tail. Incoming entries
     * are queued by BufferedLogWriter and drained on tick(); history is
     * replayed from a circular scrollback on (re)focus.
     */
    class LogsPage : public Page, public Logs::BufferedLogWriter {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Owning console (content-area geometry + clear_content). */
        Console&      _console;
        /** @brief The VT device it paints through (cached _console.terminal()). */
        AnsiTerminal& _terminal;
        /** @brief Circular log line buffer + throttled redraw for page-switch
         *         replay. */
        LogsScrollback _scrollback;
        /** @brief Mirrors tick(active) so write() knows whether it may
         *  paint. */
        bool _active = false;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        LogsPage(Terminal::Console& console);

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

        size_t format(const Logs::LogEntry* entry, char* buf, size_t len) override;
        size_t write(const char* buf, size_t len) override;

        /** @} */

        /** @name Internals
         *  @{ */

        /** @brief Store a timestamped "--- MARK ---" line (SPACE), painted by
         *         the next drain_tail. */
        void _emit_mark();

        /** @} */
    };

}}

#endif // PAGES_LOGS_H
