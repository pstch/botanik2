#ifndef PANES_PANE_H
#define PANES_PANE_H

#include <cstdint>
#include "libbotanix/terminal/parser.h"

namespace Botanix { namespace Panes {

    class Pane {
    public:
        virtual ~Pane() {}

        virtual void handle_text(char c) = 0;
        virtual void handle_command(InputEvent::Command cmd) = 0;

        virtual void focus() = 0;
        virtual void tick() = 0;
    };

}}

#endif // PANES_PANE_H