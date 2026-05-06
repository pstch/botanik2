#ifndef PANES_PANE_H
#define PANES_PANE_H

#include <cstdint>
#include "libbotanik/terminal/parser.h"

namespace Botanik { namespace Panes {
    /** @brief Abstract interface for a terminal pane (one screen region) */
    class Pane {
    private:
        using Command = Terminal::InputEvent::Command;
    public:
        virtual ~Pane() {}

        /** @brief Handle a single printable character from the user */
        virtual void handle_text(char c) = 0;
        /** @brief Handle a decoded control command from the user */
        virtual void handle_command(Command cmd) = 0;

        /** @brief Called when this pane becomes active */
        virtual void focus() = 0;
        /** @brief Called every main-loop tick to update display and state */
        virtual void tick() = 0;
    };

}}

#endif // PANES_PANE_H