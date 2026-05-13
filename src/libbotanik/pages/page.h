#ifndef PAGES_PAGE_H
#define PAGES_PAGE_H

#include <cstdint>
#include "libbotanik/terminal/parser.h"

namespace Botanik { namespace Pages {
    /** @brief Abstract interface for a terminal page (occupies the full screen) */
    class Page {
    private:
        using Command = Terminal::InputEvent::Command;
    public:
        virtual ~Page() {}

        /** @brief Short name shown in the status bar (e.g. "SHELL") */
        virtual const char* name() const = 0;

        /** @brief Handle a single printable character from the user */
        virtual void handle_text(char c) = 0;
        /** @brief Handle a batch of printable characters (default: iterate handle_text) */
        virtual void handle_text_batch(const char* buf, uint16_t len) {
            for (uint16_t i = 0; i < len; i++) handle_text(buf[i]);
        }
        /** @brief Handle a decoded control command from the user */
        virtual void handle_command(Command cmd) = 0;

        /** @brief Called when this page is about to be replaced by another page */
        virtual void deactivate() = 0;
        /** @brief Called when this page becomes active (screen already cleared) */
        virtual void focus() = 0;
        /** @brief Redraw the page in-place preserving input state (Ctrl+L). Default delegates to focus(). */
        virtual void redraw() { focus(); }
        /** @brief Called every main-loop tick to update display and state */
        virtual void tick() = 0;
    };

}}

#endif // PAGES_PAGE_H