#ifndef PAGES_LOGS_H
#define PAGES_LOGS_H

/**
 * @file logs.h
 * @brief LogsPage - full-screen live log viewer backed by a circular
 *        scrollback.
 */

#include "libbotanik/config/base.h"

#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/pages/scrollback.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen log viewer: rows 1..rows-1 are a live tail.
     *
     *  A LogWriter. Entries are queued by BufferedLogWriter and drained on
     *  tick(); history is replayed from a scrollback on (re)focus. */
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
        /** @brief This sink's ring, lent to the BufferedLogWriter base. */
        Logs::LogEntry _slots[Config::LOG_QUEUE_LOGS];
        /** @brief The log lines, painted as a window. */
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

        /** @brief The page's own name, so a loss report says which sink. */
        const char* sink_name() const override { return name(); }
        size_t write(const char* buf, size_t len) override;

        /** @} */

        /** @name Internals
         *  @{ */

        /** @brief Store a timestamped "--- MARK ---" line (SPACE). */
        void _emit_mark();

        /** @} */
    };

}}

#endif // PAGES_LOGS_H
