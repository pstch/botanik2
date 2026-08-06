// REVIEW_35: TODO
#ifndef PAGES_PAGE_H
#define PAGES_PAGE_H

/**
 * @file page.h
 * @brief Page interface - the contract every full-screen terminal page
 *        implements.
 */

#include <cstdint>                    // uint16_t - the page's row/col geometry

#include "libbotanik/console/parser.h" // Terminal::InputEvent / Command - what handle_*() receives

namespace Botanik { namespace Pages {
    /** @brief Abstract interface for a terminal page (occupies the full
     *  screen) */
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

        virtual void handle_text(char c) = 0;
        virtual void handle_text_batch(const char* buf, uint16_t len) {
            for (uint16_t i = 0; i < len; i++) handle_text(buf[i]);
        }
        /** @brief Handle a decoded control command. @p count is > 1 only for
         *  chunkable commands (BACKSPACE / CTRL_D) the parser coalesced under
         *  backpressure. */
        virtual void handle_command(Command cmd, uint16_t count = 1) = 0;
        /** @brief Handle Ctrl+C. Return true if consumed; false lets the
         *  terminal apply its default (quit on the simulator, no-op on
         *  hardware). */
        virtual bool handle_cancel() { return false; }

        /** @} */

        /** @name Focus & update
         *  @{ */

        virtual void deactivate() = 0;
        /** @brief Called when this page becomes active (screen already
         *  cleared). */
        virtual void focus() = 0;
        /** @brief Redraw in-place preserving input state (Ctrl+L). */
        virtual void redraw() { focus(); }
        /** @brief Per-tick update. Invoked for *every* page (not just the
         *  active one) so a page can drain queued logs in the background.
         *  @p active is true only for the page owning the screen; inactive
         *  pages emit no output. */
        virtual void tick(bool active) = 0;

        /** @} */
    };

}}

#endif // PAGES_PAGE_H