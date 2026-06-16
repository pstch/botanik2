#ifndef PAGES_PAGE_H
#define PAGES_PAGE_H

/**
 * @file page.h
 * @brief Page interface - the abstract contract every full-screen terminal
 *        page implements (input handling, focus lifecycle, per-tick update).
 */

#include <cstdint>
#include "libbotanik/terminal/parser.h"

namespace Botanik { namespace Pages {
    /** @brief Abstract interface for a terminal page (occupies the full screen) */
    class Page {
    private:
        using Command = Terminal::InputEvent::Command;
    public:
        /** @name Lifecycle
         *  @{ */

        virtual ~Page() {}

        /** @brief Short name shown in the status bar (e.g. "SHELL") */
        virtual const char* name() const = 0;

        /** @} */

        /** @name Input
         *  @{ */

        /** @brief Handle a single printable character from the user */
        virtual void handle_text(char c) = 0;
        /** @brief Handle a batch of printable characters (default: iterate handle_text) */
        virtual void handle_text_batch(const char* buf, uint16_t len) {
            for (uint16_t i = 0; i < len; i++) handle_text(buf[i]);
        }
        /** @brief Handle a decoded control command from the user.
         *
         * `count` is > 1 only for chunkable commands (BACKSPACE / CTRL_D)
         * that the parser coalesced while output backpressure held the
         * flush window. Implementations should apply the command N times
         * in a single pass when possible (one reconcile, not N).
         */
        virtual void handle_command(Command cmd, uint16_t count = 1) = 0;

        /** @} */

        /** @name Focus & update
         *  @{ */

        /** @brief Called when this page is about to be replaced by another page */
        virtual void deactivate() = 0;
        /** @brief Called when this page becomes active (screen already cleared) */
        virtual void focus() = 0;
        /** @brief Redraw the page in-place preserving input state (Ctrl+L). Default delegates to focus(). */
        virtual void redraw() { focus(); }
        /** @brief Called every main-loop tick to update display and state.
         *
         * Invoked for *every* page on every step, not just the active one, so a
         * page can drain its queued log entries into its own scrollback even while
         * in the background. @p active is true only for the page that currently
         * owns the screen; inactive pages must not emit any terminal output. */
        virtual void tick(bool active) = 0;

        /** @} */
    };

}}

#endif // PAGES_PAGE_H