#ifndef PANES_PANE_H
#define PANES_PANE_H

#include <cstdint>
#include "libbotanik/terminal/parser.h"

namespace Botanik { namespace Panes {
    class Pane {
    private:
        using Command = Terminal::InputEvent::Command;
    public:
        virtual ~Pane() {}

        virtual void handle_text(char c) = 0;
        virtual void handle_command(Command cmd) = 0;

        virtual void focus() = 0;
        virtual void tick() = 0;
    };

}}

#endif // PANES_PANE_H