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

        /** @brief Handle a single printable character from the user */
        virtual void handle_text(char c) = 0;
        /** @brief Handle a decoded control command from the user */
        virtual void handle_command(Command cmd) = 0;

        /** @brief Called when this page becomes active */
        virtual void focus() = 0;
        /** @brief Called every main-loop tick to update display and state */
        virtual void tick() = 0;
    };

}}

#endif // PAGES_PAGE_H