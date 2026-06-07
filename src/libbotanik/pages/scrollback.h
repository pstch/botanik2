#ifndef PAGES_SCROLLBACK_H
#define PAGES_SCROLLBACK_H

/**
 * @file scrollback.h
 * @brief Scrollback — circular line buffer with throttled incremental redraw,
 *        shared by the logs and shell pages.
 */

#include <cstddef>
#include <cstdint>

#include "libbotanik_config.h"
#include "libbotanik/terminal/terminal.h"

namespace Botanik { namespace Pages {

    /**
     * @brief Circular scrollback buffer with throttled, incremental redraw.
     *
     * Stores the most recent display lines for replay on redraw/page-switch, and
     * drives a deferred redraw that emits at most
     * @ref Config::SCROLLBACK_REDRAW_ROWS_PER_TICK rows per drain_redraw() call so
     * a full replay can never flood the serial TX buffer in one tick.
     *
     * The visible window is bottom-aligned within rows 1..log_rows; the caller
     * supplies log_rows since pages differ (LogsPage uses rows-1, ShellPage
     * reserves rows for the editor). Shared by @ref LogsPage and @ref ShellPage.
     */
    class Scrollback {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using CursorPos    = Terminal::Ansi::CursorPos;

        // Circular line buffer (newest at _head-1)
        char     _buf[Config::SHELL_SCROLLBACK_LINES][Config::SHELL_SCROLLBACK_LINE_LEN];
        uint16_t _lens[Config::SHELL_SCROLLBACK_LINES] = {};
        uint16_t _head  = 0;   // next slot to write
        uint16_t _count = 0;   // number of stored lines

        // Deferred redraw state (driven by drain_redraw() to throttle TX output)
        uint16_t _redraw_i         = UINT16_MAX;  // UINT16_MAX = not pending
        uint16_t _redraw_count     = 0;
        uint16_t _redraw_start     = 0;
        uint16_t _redraw_first_row = 0;
        CursorPos _redraw_saved    = {0, 0};      // cursor to restore once the replay drains

    public:
        /** @brief Store a line in the circular buffer (does not display it) */
        void store(const char* buf, size_t len);

        /** @brief Store a message as one or more lines no wider than @p cols,
         *  breaking on embedded newlines and on width. Continuation lines get a
         *  "> " marker; each line becomes its own scrollback entry (one entry per
         *  display row).
         *  @return The number of lines stored (always >= 1). */
        uint16_t store_wrapped(const char* buf, size_t len, uint16_t cols);

        /** @brief Forget all stored lines and cancel any pending redraw */
        void clear();

        /** @brief Number of stored lines */
        inline uint16_t count() const { return _count; }

        /** @brief Access a stored line by distance from newest (1 = most recent).
         *  @param from_end Steps back from the newest line (1-based)
         *  @param len Set to the line's length on success
         *  @return Pointer to the line bytes, or nullptr if @p from_end is out of range */
        const char* line(uint16_t from_end, uint16_t& len) const;

        /** @brief Stage a bottom-aligned incremental redraw within rows 1..log_rows */
        void begin_redraw(uint16_t log_rows);

        /** @brief True while a staged redraw still has rows left to emit */
        inline bool redraw_pending() const { return _redraw_i != UINT16_MAX; }

        /** @brief Cancel a staged redraw without emitting its remaining rows */
        inline void cancel_redraw() { _redraw_i = UINT16_MAX; }

        /** @brief Emit up to SCROLLBACK_REDRAW_ROWS_PER_TICK staged rows, stopping
         *         early if the TX buffer can't fit another row. No-op when idle. */
        void drain_redraw(AnsiTerminal& terminal);
    };

}}

#endif // PAGES_SCROLLBACK_H
